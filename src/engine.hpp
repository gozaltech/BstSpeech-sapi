#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct bst;

namespace Bestspeech {
namespace engine {

struct build_def
{
    const char*    id;
    const char*    language;
    const char*    generation;
    const wchar_t* lcid;
    unsigned       code_page;
    int            sample_rate;
    int            larynxes;
};

struct voice_def
{
    std::string      name;
    const build_def* build;
    bool             female;
    int              larynx;
    int              base;
    int              top;
    int              rate;
};

const std::vector<voice_def>& voices();

[[nodiscard]] int voice_count();
[[nodiscard]] const voice_def& voice(int index);

[[nodiscard]] int index_of(const char* name);

using pcm_callback = bool (*)(const std::int16_t* samples, long count, void* user);

struct closer
{
    void operator()(bst* h) const noexcept;
};

using handle = std::unique_ptr<bst, closer>;

[[nodiscard]] handle open(const build_def& b) noexcept;

[[nodiscard]] std::string encode(const build_def& b, const wchar_t* text, std::size_t len);

struct params
{
    int rate = 0;
    int pitch = 0;
    int volume = 100;
};

void speak(bst* h, const voice_def& v, const char* text, const params& p,
           pcm_callback cb, void* user) noexcept;
}
}
