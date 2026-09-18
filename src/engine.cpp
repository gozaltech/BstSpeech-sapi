#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "engine.hpp"
#include "debug_log.h"

extern "C" {
#include "bst.h"
#include "sonic.h"
}

namespace Bestspeech {
namespace engine {

namespace {

constexpr build_def BUILDS[] = {
    { "1995",    "English",    "1995", L"409", 1252, 11025, 7 },

    { "1998ENG", "English",    "1998", L"409", 1252, 11025, 7 },
    { "1998DUT", "Dutch",      "1998", L"413", 1252, 11025, 7 },
    { "1998FRN", "French",     "1998", L"40c", 1252, 11025, 7 },
    { "1998GRM", "German",     "1998", L"407", 1252, 11025, 7 },
    { "1998ITL", "Italian",    "1998", L"410", 1252, 11025, 7 },
    { "1998SPN", "Spanish",    "1998", L"40a", 1252, 11025, 7 },

    { "2006ARA", "Arabic",     "2006", L"401", 1256, 10000, 1 },
    { "2006DUT", "Dutch",      "2006", L"413", 1252, 10000, 1 },
    { "2006ENG", "English",    "2006", L"409", 1252, 10000, 1 },
    { "2006FRE", "French",     "2006", L"40c", 1252, 10000, 1 },
    { "2006GER", "German",     "2006", L"407", 1252, 10000, 1 },
    { "2006GRE", "Greek",      "2006", L"408", 1253, 10000, 1 },
    { "2006HEB", "Hebrew",     "2006", L"40d", 1255, 10000, 1 },
    { "2006ITA", "Italian",    "2006", L"410", 1252, 10000, 1 },
    { "2006JPN", "Japanese",   "2006", L"411",  932, 10000, 1 },
    { "2006POL", "Polish",     "2006", L"415", 1250, 10000, 1 },
    { "2006POR", "Portuguese", "2006", L"416", 1252, 10000, 1 },
    { "2006RUS", "Russian",    "2006", L"419", 1251, 10800, 1 },
    { "2006SPA", "Spanish",    "2006", L"40a", 1252, 10000, 1 }
};

constexpr int BUILD_COUNT = static_cast<int>(sizeof(BUILDS) / sizeof(BUILDS[0]));

struct classic_def
{
    const char* name;
    bool female;
    int  larynx;
    int  base;
    int  top;
    int  rate;
};

constexpr classic_def CLASSIC[] = {
    { "Fred",        false,  1,       80,  160,   0 },
    { "Sara",        true,   2,      175,  315,   0 },
    { "Hary",        false,  3,       65,  136,   5 },
    { "Wendy",       true,   2,      150,  375,  -5 },
    { "Dexter",      false,  6,       90,  180,   7 },
    { "Alien",       false,  4,      115,  172, -20 },
    { "Kit",         true,   5,      230,  552, -10 },
    { "Bruno",       false,  3,       60,  150,   8 },
    { "Ghost",       false,  3,       60,  150,   8 },
    { "Peeper",      false,  2,       80,  160,   0 },
    { "Dracula",     false,  3,       47,  115,  10 },
    { "Granny",      true,   4,      350,  490,  20 },
    { "Martha",      true,   6,      300,  600, -10 },
    { "Tim",         false,  3,       60,  114, -10 }
};

constexpr int CLASSIC_COUNT = static_cast<int>(sizeof(CLASSIC) / sizeof(CLASSIC[0]));

constexpr int PLAIN_BASE = 80;
constexpr int PLAIN_TOP = 160;
constexpr int PLAIN_FEMALE_BASE = 175;
constexpr int PLAIN_FEMALE_TOP = 350;

constexpr int FEMALE_LARYNX = 2;

constexpr long CHUNK = 2048;

constexpr std::size_t MAX_PIECE = 512;

constexpr int SAPI_RATE_MIN = -10;
constexpr int SAPI_RATE_MAX = 10;

constexpr double RATE_GAIN = 1.13;
constexpr int ENGINE_RATE_MIN = -60;
constexpr int ENGINE_RATE_MAX = 400;

constexpr int FREQ_MIN = 43;
constexpr int FREQ_MAX = 600;

constexpr int PITCH_FLOOR = 45;
constexpr int PITCH_CEILING = 400;

constexpr double PITCH_SPAN = 25.0;

std::string plain_name(const build_def& b, int larynx, bool female)
{
    std::string name = "BeSTspeech ";
    name += b.language;
    name += ' ';
    name += b.generation;
    if (female) {
        name += " Female";
    } else if (b.larynxes > 1) {
        name += ' ';
        name += std::to_string(larynx + 1);
    }
    return name;
}

std::vector<voice_def> build_catalogue()
{
    std::vector<voice_def> all;
    all.reserve(CLASSIC_COUNT + BUILD_COUNT * 8);

    for (int b = 0; b < BUILD_COUNT; ++b) {
        const build_def& build = BUILDS[b];

        if (std::strcmp(build.id, "1995") == 0) {
            for (int i = 0; i < CLASSIC_COUNT; ++i) {
                const classic_def& c = CLASSIC[i];
                all.push_back({ c.name, &build, c.female, c.larynx,
                                c.base, c.top, c.rate });
            }
            continue;
        }

        for (int larynx = 0; larynx < build.larynxes; ++larynx) {
            all.push_back({ plain_name(build, larynx, false), &build, false,
                            larynx, PLAIN_BASE, PLAIN_TOP, 0 });
        }

        const int female_larynx = build.larynxes > FEMALE_LARYNX ? FEMALE_LARYNX : 0;
        all.push_back({ plain_name(build, female_larynx, true), &build, true,
                        female_larynx, PLAIN_FEMALE_BASE, PLAIN_FEMALE_TOP, 0 });
    }

    return all;
}

[[nodiscard]] std::size_t break_at(const std::string& s, std::size_t from)
{
    const std::size_t limit = from + MAX_PIECE;
    if (limit >= s.size()) {
        return s.size();
    }

    for (std::size_t i = limit; i > from; --i) {
        const char c = s[i - 1];
        if ((c == '.' || c == '!' || c == '?' || c == ';' || c == ':') &&
            (i == s.size() || s[i] == ' ')) {
            return i;
        }
    }

    const std::size_t space = s.rfind(' ', limit);
    if (space != std::string::npos && space > from) {
        return space;
    }

    return limit;
}

[[nodiscard]] int floor_for(const voice_def& v, int adj) noexcept
{
    if (adj == 0) {
        return v.base;
    }

    double hz;
    if (adj < 0) {
        const double ratio = std::clamp((adj + PITCH_SPAN) / PITCH_SPAN, 0.0, 1.0);
        hz = PITCH_FLOOR * std::pow(static_cast<double>(v.base) / PITCH_FLOOR, ratio);
    } else {
        const double ratio = std::clamp(adj / PITCH_SPAN, 0.0, 1.0);
        hz = v.base * std::pow(static_cast<double>(PITCH_CEILING) / v.base,
                               std::pow(ratio, 1.5));
    }

    return std::clamp(static_cast<int>(std::lround(hz)), FREQ_MIN, FREQ_MAX);
}

[[nodiscard]] int ceiling_for(const voice_def& v, int base) noexcept
{
    const long scaled = std::lround(static_cast<double>(v.top) * base / v.base);
    return static_cast<int>(std::clamp<long>(scaled, base, FREQ_MAX));
}

struct speed_split
{
    int   engine_rate;
    float sonic;
};

[[nodiscard]] int engine_rate_for(double duration_factor) noexcept
{
    const long r = std::lround(RATE_GAIN * (duration_factor * 100.0 - 100.0));
    return static_cast<int>(std::clamp<long>(r, ENGINE_RATE_MIN, ENGINE_RATE_MAX));
}

[[nodiscard]] speed_split split_speed(const voice_def& v, int sapi_rate) noexcept
{
    const int asked = std::clamp(sapi_rate, SAPI_RATE_MIN, SAPI_RATE_MAX);
    const double speed = std::pow(2.0, asked / 8.0);

    const double wanted = (100.0 + v.rate) / 100.0 / speed;

    const int rate = engine_rate_for(wanted);
    const double got = 1.0 + (rate / RATE_GAIN) / 100.0;

    float sonic = static_cast<float>(got / wanted);
    if (sonic > 0.99f && sonic < 1.01f) {
        sonic = 1.0f;
    }
    return { rate, sonic };
}

class emitter
{
public:
    emitter(pcm_callback cb, void* user, int volume) noexcept
        : cb_(cb), user_(user), gain_(std::clamp(volume, 0, 100) / 100.0)
    {
    }

    bool operator()(std::int16_t* pcm, long count) noexcept
    {
        if (gain_ < 1.0) {
            for (long i = 0; i < count; ++i) {
                const long v = std::lround(pcm[i] * gain_);
                pcm[i] = static_cast<std::int16_t>(std::clamp<long>(v, -32768, 32767));
            }
        }

        for (long off = 0; off < count; off += CHUNK) {
            const long take = (std::min)(CHUNK, count - off);
            if (!cb_(pcm + off, take, user_)) {
                return false;
            }
        }
        return true;
    }

private:
    pcm_callback cb_;
    void* user_;
    double gain_;
};

class sonic_stage
{
public:
    sonic_stage() = default;

    ~sonic_stage()
    {
        if (stream_) {
            sonicDestroyStream(stream_);
        }
    }

    sonic_stage(const sonic_stage&) = delete;
    sonic_stage& operator=(const sonic_stage&) = delete;

    bool open(int sample_rate, float speed) noexcept
    {
        stream_ = sonicCreateStream(sample_rate, 1);
        if (!stream_) {
            return false;
        }
        sonicSetSpeed(stream_, speed);
        sonicSetQuality(stream_, 1);
        return true;
    }

    explicit operator bool() const noexcept { return stream_ != nullptr; }

    bool push(const std::int16_t* pcm, long count, emitter& out)
    {
        sonicWriteShortToStream(stream_, pcm, static_cast<int>(count));
        return drain(out);
    }

    bool flush(emitter& out)
    {
        sonicFlushStream(stream_);
        return drain(out);
    }

private:
    bool drain(emitter& out)
    {
        for (;;) {
            const int available = sonicSamplesAvailable(stream_);
            if (available <= 0) {
                return true;
            }
            buf_.resize(static_cast<std::size_t>(available));
            const int got = sonicReadShortFromStream(stream_, buf_.data(), available);
            if (got <= 0) {
                return true;
            }
            if (!out(buf_.data(), got)) {
                return false;
            }
        }
    }

    sonicStream stream_ = nullptr;
    std::vector<std::int16_t> buf_;
};
}

const std::vector<voice_def>& voices()
{
    static const std::vector<voice_def> all = build_catalogue();
    return all;
}

int voice_count()
{
    return static_cast<int>(voices().size());
}

const voice_def& voice(int index)
{
    const std::vector<voice_def>& all = voices();
    if (index < 0 || index >= static_cast<int>(all.size())) {
        index = 0;
    }
    return all[static_cast<std::size_t>(index)];
}

int index_of(const char* name)
{
    if (!name) {
        return -1;
    }
    const std::vector<voice_def>& all = voices();
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (_stricmp(all[i].name.c_str(), name) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void closer::operator()(bst* h) const noexcept
{
    bst_close(h);
}

handle open(const build_def& b) noexcept
{
    handle h(bst_open(b.id));
    DEBUG_LOG("engine::open: build %s -> %s", b.id, h ? "ok" : "FAILED");
    return h;
}

void speak(bst* h, const voice_def& v, const char* text, const params& p,
           pcm_callback cb, void* user) noexcept
{
    if (!h || !text || !cb) {
        return;
    }

    try {
        const int base = floor_for(v, p.pitch);
        const int top = ceiling_for(v, base);
        const speed_split sp = split_speed(v, p.rate);

        bst_set(h, "voice", v.larynx);
        bst_set(h, "pitch", base);
        bst_set(h, "top", top);
        bst_set(h, "rate", sp.engine_rate);

        DEBUG_LOG("engine::speak: %s (%s) larynx=%d base=%dHz top=%dHz rate=%d sonic=%.2fx volume=%d",
                  v.name.c_str(), v.build->id, v.larynx, base, top,
                  sp.engine_rate, sp.sonic, p.volume);

        emitter out(cb, user, p.volume);

        sonic_stage sonic;
        if (sp.sonic != 1.0f && !sonic.open(v.build->sample_rate, sp.sonic)) {
            DEBUG_LOG("engine::speak: no sonic stream, speaking at the engine's own rate");
        }

        const std::string clean(text);
        std::vector<std::int16_t> pcm;

        for (std::size_t from = 0; from < clean.size();) {
            const std::size_t to = break_at(clean, from);
            const std::string piece = clean.substr(from, to - from);
            from = to;
            while (from < clean.size() && clean[from] == ' ') {
                ++from;
            }

            const long want = bst_length(h, piece.c_str());
            if (want <= 0) {
                continue;
            }

            pcm.resize(static_cast<std::size_t>(want));
            const long got = bst_say(h, piece.c_str(), pcm.data(), want);
            if (got <= 0) {
                continue;
            }

            const bool more = sonic ? sonic.push(pcm.data(), got, out)
                                    : out(pcm.data(), got);
            if (!more) {
                DEBUG_LOG("engine::speak: stopped by the caller");
                return;
            }
        }

        if (sonic) {
            sonic.flush(out);
        }
    }
    catch (...) {
        DEBUG_LOG("engine::speak: out of memory");
    }
}
}
}
