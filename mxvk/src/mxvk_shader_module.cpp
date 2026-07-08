#include "mxvk/mxvk_shader_module.hpp"

#include "mxvk/mxvk_exception.hpp"

#include <fstream>
#include <iterator>

namespace mxvk {
    std::vector<char> load_spv(const std::string &path) {
        if (path.empty()) {
            throw mxvk::Exception("SPIR-V path is empty");
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw mxvk::Exception("Failed to open SPIR-V file: " + path);
        }

        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (bytes.empty()) {
            throw mxvk::Exception("SPIR-V file is empty: " + path);
        }
        if ((bytes.size() % 4U) != 0U) {
            throw mxvk::Exception("SPIR-V file size is not 4-byte aligned: " + path);
        }

        return bytes;
    }

    VkShaderModule create_shader_module(VkDevice device, const std::vector<char> &spv_bytes) {
        if (device == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot create shader module with a null device");
        }
        if (spv_bytes.empty() || (spv_bytes.size() % 4U) != 0U) {
            throw mxvk::Exception("Invalid SPIR-V shader data");
        }

        VkShaderModuleCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        create_info.codeSize = spv_bytes.size();
        create_info.pCode = reinterpret_cast<const uint32_t *>(spv_bytes.data());

        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &create_info, nullptr, &module) != VK_SUCCESS) {
            throw mxvk::Exception("Failed to create shader module");
        }

        return module;
    }
} // namespace mxvk
