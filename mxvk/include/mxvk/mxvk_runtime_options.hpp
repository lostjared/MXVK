#ifndef MXVK_RUNTIME_OPTIONS_HPP
#define MXVK_RUNTIME_OPTIONS_HPP

#include <string>

namespace mxvk {
    [[nodiscard]] inline bool &defaultEnableScreenshotStorage() {
        static bool enabled = false;
        return enabled;
    }

    inline void setDefaultEnableScreenshot(bool enabled) { defaultEnableScreenshotStorage() = enabled; }

    [[nodiscard]] inline bool defaultEnableScreenshot() { return defaultEnableScreenshotStorage(); }

    [[nodiscard]] inline std::string &defaultExecutableNameStorage() {
        static std::string name = "mxvk";
        return name;
    }

    inline void setDefaultExecutableName(const std::string &name) { defaultExecutableNameStorage() = name.empty() ? "mxvk" : name; }

    [[nodiscard]] inline const std::string &defaultExecutableName() { return defaultExecutableNameStorage(); }

    [[nodiscard]] inline std::string &defaultShaderDirectoryStorage() {
        static std::string directory{};
        return directory;
    }

    inline void setDefaultShaderDirectory(const std::string &directory) { defaultShaderDirectoryStorage() = directory; }

    [[nodiscard]] inline const std::string &defaultShaderDirectory() { return defaultShaderDirectoryStorage(); }
} // namespace mxvk

#endif
