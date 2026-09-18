#include <new>
#include <string>
#include <algorithm>
#include "utils.hpp"
#include "ISpTTSEngineImpl.hpp"
#include "debug_log.h"

namespace Bestspeech {
namespace sapi {

namespace {

constexpr WORD AUDIO_CHANNELS = 1;
constexpr WORD AUDIO_BITS_PER_SAMPLE = 16;

struct SpeakContext {
    ISpTTSEngineSite* caller = nullptr;
    ULONGLONG bytes_written = 0;
    bool aborted = false;
};

bool speak_callback(const std::int16_t* samples, long count, void* user) {
    auto* ctx = static_cast<SpeakContext*>(user);
    if (!ctx || !ctx->caller) {
        DEBUG_LOG("SAPI Callback: ERROR - No context or caller");
        return false;
    }

    auto ptr = reinterpret_cast<const BYTE*>(samples);
    ULONG remaining = static_cast<ULONG>(count) * sizeof(std::int16_t);

    DEBUG_LOG("SAPI Callback: Writing %lu bytes to SAPI", remaining);

    while (remaining > 0) {
        const DWORD actions = ctx->caller->GetActions();
        if (actions & SPVES_ABORT) {
            DEBUG_LOG("SAPI Callback: ABORT requested");
            ctx->aborted = true;
            return false;
        }
        if (actions & SPVES_SKIP) {
            DEBUG_LOG("SAPI Callback: SKIP requested");
            ctx->caller->CompleteSkip(0);
            ctx->aborted = true;
            return false;
        }

        ULONG written = remaining;
        HRESULT hr = ctx->caller->Write(ptr, remaining, &written);
        if (FAILED(hr)) {
            DEBUG_LOG("SAPI Callback: Write FAILED with HRESULT 0x%08X", hr);
            return false;
        }
        if (written > remaining) {
            DEBUG_LOG("SAPI Callback: Write error - written (%lu) > remaining (%lu)", written, remaining);
            return false;
        }
        ctx->bytes_written += written;
        remaining -= written;
        ptr += written;
    }

    return true;
}
}

ISpTTSEngineImpl::ISpTTSEngineImpl()
    : voice_index_(0)
{
}

ISpTTSEngineImpl::~ISpTTSEngineImpl() = default;

STDMETHODIMP ISpTTSEngineImpl::SetObjectToken(ISpObjectToken* pToken)
{
    DEBUG_LOG("=== SetObjectToken Called ===");

    if (!pToken) {
        DEBUG_LOG("SetObjectToken: ERROR - pToken is NULL");
        return E_INVALIDARG;
    }

    try {
        ISpDataKeyPtr attr;
        if (FAILED(pToken->OpenKey(L"Attributes", &attr))) {
            DEBUG_LOG("SetObjectToken: ERROR - Failed to open Attributes key");
            return E_INVALIDARG;
        }

        utils::out_ptr<wchar_t> name(CoTaskMemFree);
        if (FAILED(attr->GetStringValue(L"Name", name.address()))) {
            DEBUG_LOG("SetObjectToken: ERROR - Failed to get Name attribute");
            return E_INVALIDARG;
        }

        const std::string voice_name = utils::wstring_to_string(name.get());
        DEBUG_LOG("SetObjectToken: Voice name = %s", voice_name.c_str());

        const int found = engine::index_of(voice_name.c_str());
        voice_index_ = found >= 0 ? found : 0;

        token_ = pToken;
        DEBUG_LOG("SetObjectToken: SUCCESS - Voice index = %d (%s)", voice_index_,
                  engine::voice(voice_index_).build->id);
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        DEBUG_LOG("SetObjectToken: ERROR - Out of memory");
        return E_OUTOFMEMORY;
    }
    catch (...) {
        DEBUG_LOG("SetObjectToken: ERROR - Unexpected exception");
        return E_UNEXPECTED;
    }
}

STDMETHODIMP ISpTTSEngineImpl::GetObjectToken(ISpObjectToken** ppToken)
{
    DEBUG_LOG("=== GetObjectToken Called ===");

    if (!ppToken) {
        DEBUG_LOG("GetObjectToken: ERROR - ppToken is NULL");
        return E_POINTER;
    }
    *ppToken = nullptr;

    if (token_) {
        token_.AddRef();
        *ppToken = token_.GetInterfacePtr();
        DEBUG_LOG("GetObjectToken: SUCCESS - Returned token");
        return S_OK;
    }
    DEBUG_LOG("GetObjectToken: ERROR - No token set");
    return E_UNEXPECTED;
}

STDMETHODIMP ISpTTSEngineImpl::GetOutputFormat(
    const GUID* /*pTargetFmtId*/,
    const WAVEFORMATEX* /*pTargetWaveFormatEx*/,
    GUID* pOutputFormatId,
    WAVEFORMATEX** ppCoMemOutputWaveFormatEx)
{
    DEBUG_LOG("=== GetOutputFormat Called ===");

    if (!pOutputFormatId) {
        DEBUG_LOG("GetOutputFormat: ERROR - pOutputFormatId is NULL");
        return E_POINTER;
    }
    if (!ppCoMemOutputWaveFormatEx) {
        DEBUG_LOG("GetOutputFormat: ERROR - ppCoMemOutputWaveFormatEx is NULL");
        return E_POINTER;
    }

    *pOutputFormatId = SPDFID_WaveFormatEx;
    *ppCoMemOutputWaveFormatEx = nullptr;

    auto* pwfex = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    if (!pwfex) {
        DEBUG_LOG("GetOutputFormat: ERROR - Out of memory");
        return E_OUTOFMEMORY;
    }

    pwfex->wFormatTag = WAVE_FORMAT_PCM;
    pwfex->nChannels = AUDIO_CHANNELS;
    pwfex->nSamplesPerSec = engine::voice(voice_index_).build->sample_rate;
    pwfex->wBitsPerSample = AUDIO_BITS_PER_SAMPLE;
    pwfex->nBlockAlign = pwfex->nChannels * pwfex->wBitsPerSample / 8;
    pwfex->nAvgBytesPerSec = pwfex->nSamplesPerSec * pwfex->nBlockAlign;
    pwfex->cbSize = 0;

    *ppCoMemOutputWaveFormatEx = pwfex;
    DEBUG_LOG("GetOutputFormat: SUCCESS - Channels=%d, Rate=%lu, Bits=%d",
              pwfex->nChannels, pwfex->nSamplesPerSec, pwfex->wBitsPerSample);
    return S_OK;
}

STDMETHODIMP ISpTTSEngineImpl::Speak(
    DWORD dwSpeakFlags,
    REFGUID /*rguidFormatId*/,
    const WAVEFORMATEX* /*pWaveFormatEx*/,
    const SPVTEXTFRAG* pTextFragList,
    ISpTTSEngineSite* pOutputSite)
{
    DEBUG_LOG("=== Speak Called ===");
    DEBUG_LOG("Speak Flags: 0x%08X", dwSpeakFlags);

    if (!pTextFragList) {
        DEBUG_LOG("Speak: ERROR - pTextFragList is NULL");
        return E_INVALIDARG;
    }
    if (!pOutputSite) {
        DEBUG_LOG("Speak: ERROR - pOutputSite is NULL");
        return E_INVALIDARG;
    }

    try {
        const engine::voice_def& voice = engine::voice(voice_index_);

        if (!engine_ || open_build_ != voice.build) {
            engine_ = engine::open(*voice.build);
            if (!engine_) {
                DEBUG_LOG("Speak: ERROR - Unable to open build %s", voice.build->id);
                return E_FAIL;
            }
            open_build_ = voice.build;
        }

        long sapi_rate = 0;
        pOutputSite->GetRate(&sapi_rate);

        unsigned short sapi_volume = 100;
        pOutputSite->GetVolume(&sapi_volume);

        DEBUG_LOG("=== New Speech Request ===");
        DEBUG_LOG("Voice: %s on %s", voice.name.c_str(), voice.build->id);
        DEBUG_LOG("SAPI Rate: %d, SAPI Volume: %u", (int)sapi_rate, sapi_volume);

        ULONGLONG event_interest = 0;
        pOutputSite->GetEventInterest(&event_interest);
        const bool send_sentence_events = (event_interest & (1ULL << SPEI_SENTENCE_BOUNDARY)) != 0;
        const bool send_word_events = (event_interest & (1ULL << SPEI_WORD_BOUNDARY)) != 0;
        DEBUG_LOG("Event interest: 0x%llX (sentence: %d, word: %d)", event_interest, send_sentence_events, send_word_events);

        SpeakContext ctx;
        ctx.caller = pOutputSite;
        ctx.bytes_written = 0;
        ctx.aborted = false;

        int frag_count = 0;
        for (const SPVTEXTFRAG* f = pTextFragList; f; f = f->pNext) {
            frag_count++;
        }
        DEBUG_LOG("Fragment count: %d", frag_count);

        int frag_num = 0;
        for (const SPVTEXTFRAG* frag = pTextFragList; frag; frag = frag->pNext) {
            frag_num++;
            DEBUG_LOG("--- Processing Fragment %d/%d ---", frag_num, frag_count);
            const DWORD actions = pOutputSite->GetActions();

            if (actions & SPVES_ABORT) {
                break;
            }
            if (actions & SPVES_SKIP) {
                pOutputSite->CompleteSkip(0);
                break;
            }

            if (actions & SPVES_RATE) {
                pOutputSite->GetRate(&sapi_rate);
            }
            if (actions & SPVES_VOLUME) {
                pOutputSite->GetVolume(&sapi_volume);
            }

            DEBUG_LOG("Fragment eAction: %d (SPVA_Speak=0, SPVA_Silence=1, SPVA_Pronounce=2, SPVA_Bookmark=3, SPVA_SpellOut=4)",
                      frag->State.eAction);
            DEBUG_LOG("Fragment ulTextSrcOffset: %lu, ulTextLen: %lu", frag->ulTextSrcOffset, frag->ulTextLen);

            if (frag->State.eAction == SPVA_Bookmark) {
                DEBUG_LOG("Fragment is a BOOKMARK");
                if (frag->ulTextLen > 0 && frag->pTextStart) {
                    std::wstring bookmark_text(frag->pTextStart, frag->ulTextLen);
                    DEBUG_LOG("Bookmark text: \"%S\"", bookmark_text.c_str());

                    long bookmark_id = 0;
                    try {
                        bookmark_id = std::stol(bookmark_text);
                    } catch (...) {
                    }

                    SPEVENT event = {};
                    event.eEventId = SPEI_TTS_BOOKMARK;
                    event.elParamType = SPET_LPARAM_IS_STRING;
                    event.ullAudioStreamOffset = ctx.bytes_written;
                    event.ulStreamNum = 0;
                    event.lParam = reinterpret_cast<LPARAM>(bookmark_text.c_str());
                    event.wParam = bookmark_id;
                    HRESULT hr = pOutputSite->AddEvents(&event, 1);
                    DEBUG_LOG("SAPI Event: Bookmark at byte offset %llu, id=%ld, text=\"%S\" - Result: 0x%08X",
                              ctx.bytes_written, bookmark_id, bookmark_text.c_str(), hr);
                } else {
                    DEBUG_LOG("Bookmark has no text, sending with id=0");
                    SPEVENT event = {};
                    event.eEventId = SPEI_TTS_BOOKMARK;
                    event.elParamType = SPET_LPARAM_IS_UNDEFINED;
                    event.ullAudioStreamOffset = ctx.bytes_written;
                    event.ulStreamNum = 0;
                    event.lParam = 0;
                    event.wParam = 0;
                    HRESULT hr = pOutputSite->AddEvents(&event, 1);
                    DEBUG_LOG("SAPI Event: Bookmark (empty) at byte offset %llu - Result: 0x%08X",
                              ctx.bytes_written, hr);
                }
                continue;
            }

            if (frag->State.eAction != SPVA_Speak && frag->State.eAction != SPVA_SpellOut) {
                DEBUG_LOG("Fragment skipped - not Speak or SpellOut action");
                continue;
            }

            if (frag->ulTextLen == 0 || !frag->pTextStart) {
                DEBUG_LOG("Fragment skipped - no text");
                continue;
            }

            const std::string text = engine::encode(*voice.build, frag->pTextStart,
                                                    frag->ulTextLen);
            DEBUG_LOG("Fragment text: \"%s\" (code page %u)", text.c_str(),
                      voice.build->code_page);
            if (text.empty()) {
                DEBUG_LOG("Fragment skipped - empty after conversion");
                continue;
            }

            if (send_sentence_events) {
                SPEVENT event = {};
                event.eEventId = SPEI_SENTENCE_BOUNDARY;
                event.elParamType = SPET_LPARAM_IS_UNDEFINED;
                event.ullAudioStreamOffset = ctx.bytes_written;
                event.ulStreamNum = 0;
                event.lParam = frag->ulTextSrcOffset;
                event.wParam = frag->ulTextLen;
                HRESULT hr = pOutputSite->AddEvents(&event, 1);
                DEBUG_LOG("SAPI Event: Sentence boundary at byte offset %llu, position %lu, length %lu - Result: 0x%08X",
                          ctx.bytes_written, frag->ulTextSrcOffset, frag->ulTextLen, hr);
            }

            if (send_word_events) {
                const wchar_t* text_start = frag->pTextStart;
                ULONG text_len = frag->ulTextLen;

                bool in_word = false;
                ULONG word_start = 0;

                for (ULONG i = 0; i <= text_len; ++i) {
                    bool is_word_char = (i < text_len) &&
                                       (iswalnum(text_start[i]) || text_start[i] == L'\'' || text_start[i] == L'-');

                    if (is_word_char && !in_word) {
                        word_start = i;
                        in_word = true;
                    } else if (!is_word_char && in_word) {
                        ULONG word_len = i - word_start;
                        SPEVENT event = {};
                        event.eEventId = SPEI_WORD_BOUNDARY;
                        event.elParamType = SPET_LPARAM_IS_UNDEFINED;
                        event.ullAudioStreamOffset = ctx.bytes_written;
                        event.ulStreamNum = 0;
                        event.lParam = frag->ulTextSrcOffset + word_start;
                        event.wParam = word_len;
                        HRESULT hr = pOutputSite->AddEvents(&event, 1);

                        std::wstring word_text(text_start + word_start, word_len);
                        DEBUG_LOG("SAPI Event: Word boundary at byte offset %llu, position %lu, length %lu (\"%S\") - Result: 0x%08X",
                                  ctx.bytes_written, frag->ulTextSrcOffset + word_start, word_len,
                                  word_text.c_str(), hr);

                        in_word = false;
                    }
                }
            }

            engine::params p;
            p.rate = static_cast<int>(sapi_rate) + frag->State.RateAdj;
            p.pitch = frag->State.PitchAdj.MiddleAdj;
            p.volume = std::clamp(static_cast<int>(sapi_volume) *
                                  static_cast<int>(frag->State.Volume) / 100, 0, 100);

            DEBUG_LOG("--- Fragment Settings ---");
            DEBUG_LOG("  Rate: %d (site %d + adj %d)", p.rate, (int)sapi_rate, frag->State.RateAdj);
            DEBUG_LOG("  Pitch: %d, Volume: %d", p.pitch, p.volume);

            engine::speak(engine_.get(), voice, text.c_str(), p, speak_callback, &ctx);

            if (ctx.aborted) {
                break;
            }
        }

        DEBUG_LOG("=== Speak Completed Successfully ===");
        DEBUG_LOG("=== RETURNING from Speak() - Engine ready for next call ===\n");
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        DEBUG_LOG("=== Speak Failed: Out of memory ===\n");
        return E_OUTOFMEMORY;
    }
    catch (...) {
        DEBUG_LOG("=== Speak Failed: Unexpected exception ===\n");
        return E_UNEXPECTED;
    }
}
}
}
