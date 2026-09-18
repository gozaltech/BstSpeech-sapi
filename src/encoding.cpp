#include <algorithm>
#include <string>
#include <vector>
#include <windows.h>

#include "engine.hpp"

namespace Bestspeech {
namespace engine {

namespace {

void fold(std::wstring& s)
{
    for (wchar_t& c : s) {
        switch (c) {
        case 0x2013: case 0x2014: case 0x2212: c = L'-';  break;
        case 0x2018: case 0x2019: case 0x201B: c = L'\''; break;
        case 0x201C: case 0x201D: case 0x201F: c = L'"';  break;
        case 0x00A0: case 0x2007: case 0x202F: c = L' ';  break;
        case L'\r': case L'\n': case L'\t':    c = L' ';  break;
        default: break;
        }
    }

    for (std::size_t pos = 0; (pos = s.find(0x2026, pos)) != std::wstring::npos; pos += 3) {
        s.replace(pos, 1, L"...");
    }
}
}

std::string encode(const build_def& b, const wchar_t* text, std::size_t len)
{
    if (!text || len == 0) {
        return {};
    }

    std::wstring wide(text, len);
    fold(wide);

    const char space = ' ';
    const int size = WideCharToMultiByte(b.code_page, 0, wide.c_str(),
                                         static_cast<int>(wide.size()),
                                         nullptr, 0, &space, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(b.code_page, 0, wide.c_str(), static_cast<int>(wide.size()),
                        out.data(), size, &space, nullptr);

    out.erase(std::remove_if(out.begin(), out.end(),
                             [](char c) { return static_cast<unsigned char>(c) < 32; }),
              out.end());

    for (std::size_t pos = 0; (pos = out.find("  ", pos)) != std::string::npos;) {
        out.replace(pos, 2, " ");
    }

    return out;
}
}
}
