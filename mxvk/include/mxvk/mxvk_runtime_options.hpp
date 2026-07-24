#pragma once

#include <string>

namespace mxvk {
    [[nodiscard]] inline bool &defaultEnableScreenshotStorage() {
        static bool enabled = false;
        return enabled;
    }

    inline void setDefaultEnableScreenshot(bool enabled) {
        defaultEnableScreenshotStorage() = enabled;
    }

    [[nodiscard]] inline bool defaultEnableScreenshot() {
        return defaultEnableScreenshotStorage();
    }

    [[nodiscard]] inline std::string &defaultExecutableNameStorage() {
        static std::string name = "mxvk";
        return name;
    }

    inline void setDefaultExecutableName(const std::string &name) {
        defaultExecutableNameStorage() = name.empty() ? "mxvk" : name;
    }

    [[nodiscard]] inline const std::string &defaultExecutableName() {
        return defaultExecutableNameStorage();
    }
} // namespace mxvk
