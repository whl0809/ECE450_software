#pragma once

#include <string>
#include <utility>

namespace odor::hardware {

struct HardwareResult {
    bool ok = false;
    int errorCode = 0;
    std::string message;

    static HardwareResult success()
    {
        return {true, 0, {}};
    }

    static HardwareResult failure(int code, std::string text)
    {
        return {false, code, std::move(text)};
    }
};

}  // namespace odor::hardware
