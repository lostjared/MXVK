#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <volk/volk.h>

namespace mxvk {
    enum class ShaderStage {
        Unknown,
        Vertex,
        Fragment,
        Compute,
    };

    struct ShaderModuleInfo {
        ShaderStage stage = ShaderStage::Unknown;
        uint32_t localSizeX = 1;
        uint32_t localSizeY = 1;
        uint32_t localSizeZ = 1;
        bool usesHistoryTexture = false;
    };

    /**
     * @brief Load a SPIR-V file from disk.
     * @param path Absolute or relative path to a .spv file.
     * @return Raw bytes loaded from file.
     * @throws mxvk::Exception when the file cannot be opened, is empty, or is not 4-byte aligned.
     */
    [[nodiscard]] std::vector<char> load_spv(const std::string &path);

    /** @brief Inspect a SPIR-V entry point and compute local workgroup size. */
    [[nodiscard]] ShaderModuleInfo inspect_spirv(const std::vector<char> &spv_bytes);

    /**
     * @brief Create a shader module from SPIR-V bytecode.
     * @param device Logical Vulkan device used to create the module.
     * @param spv_bytes SPIR-V bytecode payload.
     * @return Created shader module handle.
     * @throws mxvk::Exception when inputs are invalid or module creation fails.
     */
    [[nodiscard]] VkShaderModule create_shader_module(VkDevice device, const std::vector<char> &spv_bytes);
} // namespace mxvk
