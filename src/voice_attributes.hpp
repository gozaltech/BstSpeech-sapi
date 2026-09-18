#pragma once

#include <string>
#include "engine.hpp"
#include "utils.hpp"

namespace Bestspeech {
namespace sapi {

class voice_attributes
{
public:
    explicit voice_attributes(int voice_index = 0) noexcept
        : index_(voice_index)
    {
        if (index_ < 0 || index_ >= engine::voice_count()) {
            index_ = 0;
        }
    }

    [[nodiscard]] std::wstring get_name() const
    {
        return utils::string_to_wstring(def().name.c_str());
    }

    [[nodiscard]] int get_index() const noexcept
    {
        return index_;
    }

    [[nodiscard]] std::wstring get_age() const
    {
        return L"Adult";
    }

    [[nodiscard]] std::wstring get_gender() const
    {
        return def().female ? L"Female" : L"Male";
    }

    [[nodiscard]] std::wstring get_language() const
    {
        return def().build->lcid;
    }

private:
    [[nodiscard]] const engine::voice_def& def() const
    {
        return engine::voice(index_);
    }

    int index_;
};
}
}
