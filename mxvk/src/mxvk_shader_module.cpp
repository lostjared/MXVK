#include "mxvk/mxvk_shader_module.hpp"

#include "mxvk/mxvk_exception.hpp"

#include <fstream>
#include <iterator>
#include <span>

namespace mxvk {
    namespace {
        constexpr uint32_t SPIRV_MAGIC = 0x07230203U;
        constexpr uint16_t OP_ENTRY_POINT = 15U;
        constexpr uint16_t OP_EXECUTION_MODE = 16U;
        constexpr uint16_t OP_DECORATE = 71U;
        constexpr uint32_t EXECUTION_MODEL_VERTEX = 0U;
        constexpr uint32_t EXECUTION_MODEL_FRAGMENT = 4U;
        constexpr uint32_t EXECUTION_MODEL_COMPUTE = 5U;
        constexpr uint32_t EXECUTION_MODE_LOCAL_SIZE = 17U;
        constexpr uint32_t DECORATION_BINDING = 33U;
        constexpr uint32_t DECORATION_DESCRIPTOR_SET = 34U;
    } // namespace

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

    ShaderModuleInfo inspect_spirv(const std::vector<char> &spv_bytes) {
        if (spv_bytes.size() < 5U * sizeof(uint32_t) ||
            (spv_bytes.size() % sizeof(uint32_t)) != 0U) {
            throw mxvk::Exception("Invalid SPIR-V shader data");
        }

        const auto *word_data =
            reinterpret_cast<const uint32_t *>(spv_bytes.data());
        const std::span<const uint32_t> words(
            word_data, spv_bytes.size() / sizeof(uint32_t));
        if (words.front() != SPIRV_MAGIC) {
            throw mxvk::Exception("Invalid SPIR-V magic word");
        }

        ShaderModuleInfo info{};
        uint32_t entry_point_id = 0;
        const uint32_t id_bound = words[3];
        if (id_bound == 0U || id_bound > words.size()) {
            throw mxvk::Exception("Invalid SPIR-V identifier bound");
        }
        std::vector<uint32_t> descriptor_sets(id_bound, UINT32_MAX);
        std::vector<uint32_t> descriptor_bindings(id_bound, UINT32_MAX);
        for (std::size_t offset = 5; offset < words.size();) {
            const uint16_t word_count =
                static_cast<uint16_t>(words[offset] >> 16U);
            const uint16_t opcode =
                static_cast<uint16_t>(words[offset] & 0xFFFFU);
            if (word_count == 0 || offset + word_count > words.size()) {
                throw mxvk::Exception("Malformed SPIR-V instruction stream");
            }
            if (opcode == OP_ENTRY_POINT && word_count >= 3U &&
                entry_point_id == 0U) {
                entry_point_id = words[offset + 2U];
                switch (words[offset + 1U]) {
                case EXECUTION_MODEL_VERTEX:
                    info.stage = ShaderStage::Vertex;
                    break;
                case EXECUTION_MODEL_FRAGMENT:
                    info.stage = ShaderStage::Fragment;
                    break;
                case EXECUTION_MODEL_COMPUTE:
                    info.stage = ShaderStage::Compute;
                    break;
                default:
                    info.stage = ShaderStage::Unknown;
                    break;
                }
            } else if (opcode == OP_DECORATE && word_count >= 4U &&
                       words[offset + 1U] < id_bound) {
                const uint32_t target_id = words[offset + 1U];
                const uint32_t decoration = words[offset + 2U];
                if (decoration == DECORATION_BINDING) {
                    descriptor_bindings[target_id] = words[offset + 3U];
                } else if (decoration == DECORATION_DESCRIPTOR_SET) {
                    descriptor_sets[target_id] = words[offset + 3U];
                }
            }
            offset += word_count;
        }

        for (uint32_t id = 0; id < id_bound; ++id) {
            if (descriptor_sets[id] != 0U) {
                continue;
            }
            switch (descriptor_bindings[id]) {
            case 2U:
                info.usesHistoryTexture = true;
                break;
            case 3U:
                info.usesSpectrumTexture = true;
                break;
            case 4U:
                info.usesSpectrumHistoryTexture = true;
                break;
            default:
                break;
            }
        }

        if (info.stage == ShaderStage::Compute && entry_point_id != 0U) {
            for (std::size_t offset = 5; offset < words.size();) {
                const uint16_t word_count =
                    static_cast<uint16_t>(words[offset] >> 16U);
                const uint16_t opcode =
                    static_cast<uint16_t>(words[offset] & 0xFFFFU);
                if (opcode == OP_EXECUTION_MODE && word_count >= 6U &&
                    words[offset + 1U] == entry_point_id &&
                    words[offset + 2U] == EXECUTION_MODE_LOCAL_SIZE) {
                    info.localSizeX = words[offset + 3U];
                    info.localSizeY = words[offset + 4U];
                    info.localSizeZ = words[offset + 5U];
                    break;
                }
                offset += word_count;
            }
        }
        return info;
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
