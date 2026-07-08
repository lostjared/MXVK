#include "mxvk/mxvk_stencil.hpp"

#include "mxvk/mxvk_exception.hpp"

#include <array>
#include <fstream>
#include <iterator>
#include <vector>

namespace mxvk {
    namespace {
        /**
         * @brief Load a SPIR-V file and validate basic shader bytecode constraints.
         * @param path Path to a .spv file.
         * @return Raw SPIR-V bytes.
         */
        std::vector<char> load_spv(const std::string &path) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                throw mxvk::Exception("failed to open stencil SPIR-V shader");
            }
            const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (bytes.empty() || (bytes.size() % 4U) != 0U) {
                throw mxvk::Exception("invalid stencil SPIR-V shader");
            }
            return bytes;
        }

        /**
         * @brief Create a Vulkan shader module from validated SPIR-V bytes.
         * @param device Logical device that will own the shader module.
         * @param bytes 4-byte-aligned SPIR-V bytecode.
         * @return Created shader module handle.
         */
        VkShaderModule create_shader_module(VkDevice device, const std::vector<char> &bytes) {
            VkShaderModuleCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            info.codeSize = bytes.size();
            info.pCode = reinterpret_cast<const uint32_t *>(bytes.data());

            VkShaderModule module = VK_NULL_HANDLE;
            if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
                throw mxvk::Exception("failed to create stencil shader module");
            }
            return module;
        }
    } // namespace

    VK_Stencil::~VK_Stencil() {
        destroy();
    }

    void VK_Stencil::initialize(const VulkanContext &context,
                                VkExtent2D extent,
                                VkFormat color_format,
                                VkFormat depth_format,
                                VkPipelineCache pipeline_cache,
                                const std::string &mask_vertex_shader,
                                const std::string &mask_fragment_shader,
                                const std::string &content_vertex_shader,
                                const std::string &content_fragment_shader) {
        destroy();
        vk_context = context;
        stencil_extent = extent;
        render_color_format = color_format;
        render_depth_format = depth_format;
        cache = pipeline_cache;
        mask_vertex_path = mask_vertex_shader;
        mask_fragment_path = mask_fragment_shader;
        content_vertex_path = content_vertex_shader;
        content_fragment_path = content_fragment_shader;
        stencil_image_format = choose_stencil_format();
        create_resources();
        create_pipelines();
    }

    void VK_Stencil::destroy() {
        destroy_pipelines();
        destroy_image();
        vk_context = {};
        stencil_extent = {};
        render_color_format = VK_FORMAT_UNDEFINED;
        render_depth_format = VK_FORMAT_UNDEFINED;
        stencil_image_format = VK_FORMAT_UNDEFINED;
        cache = VK_NULL_HANDLE;
        mask_vertex_path.clear();
        mask_fragment_path.clear();
        content_vertex_path.clear();
        content_fragment_path.clear();
    }

    void VK_Stencil::resize(VkExtent2D extent) {
        if (extent.width == stencil_extent.width && extent.height == stencil_extent.height) {
            return;
        }
        stencil_extent = extent;
        destroy_image();
        create_resources();
    }

    void VK_Stencil::prepare_for_rendering(VkCommandBuffer cmd) {
        if (!valid()) {
            return;
        }

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = image_initialized ? VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_2_NONE;
        barrier.srcAccessMask = image_initialized ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_NONE;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = image_initialized ? VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependency);
        image_initialized = true;
    }

    void VK_Stencil::configure_attachment(VkRenderingAttachmentInfo &attachment) const {
        if (!valid()) {
            return;
        }
        attachment.imageView = view;
        attachment.imageLayout = VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.clearValue.depthStencil = {1.0f, 0U};
    }

    void VK_Stencil::draw_mask(VkCommandBuffer cmd, const PushConstants &push_constants) const {
        if (mask_pipeline == VK_NULL_HANDLE || mask_layout == VK_NULL_HANDLE) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mask_pipeline);
        vkCmdPushConstants(cmd, mask_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push_constants);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    void VK_Stencil::draw_content(VkCommandBuffer cmd, const PushConstants &push_constants) const {
        if (content_pipeline == VK_NULL_HANDLE || content_layout == VK_NULL_HANDLE) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, content_pipeline);
        vkCmdPushConstants(cmd, content_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push_constants);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    bool VK_Stencil::valid() const noexcept {
        return vk_context.device != VK_NULL_HANDLE && image != VK_NULL_HANDLE && view != VK_NULL_HANDLE &&
               stencil_extent.width > 0U && stencil_extent.height > 0U;
    }

    void VK_Stencil::create_resources() {
        if (vk_context.device == VK_NULL_HANDLE || stencil_extent.width == 0U || stencil_extent.height == 0U) {
            return;
        }
        create_image(vk_context,
                     stencil_extent.width,
                     stencil_extent.height,
                     stencil_image_format,
                     VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     image,
                     memory);
        view = create_image_view(vk_context.device, image, stencil_image_format, VK_IMAGE_ASPECT_STENCIL_BIT);
        image_initialized = false;
    }

    void VK_Stencil::create_pipelines() {
        VkPushConstantRange push_constant{};
        push_constant.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push_constant.offset = 0;
        push_constant.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_constant;
        if (vkCreatePipelineLayout(vk_context.device, &layout_info, nullptr, &mask_layout) != VK_SUCCESS ||
            vkCreatePipelineLayout(vk_context.device, &layout_info, nullptr, &content_layout) != VK_SUCCESS) {
            throw mxvk::Exception("failed to create stencil pipeline layout");
        }

        mask_pipeline = create_pipeline(mask_vertex_path, mask_fragment_path, mask_layout, true);
        content_pipeline = create_pipeline(content_vertex_path, content_fragment_path, content_layout, false);
    }

    void VK_Stencil::destroy_pipelines() {
        if (vk_context.device == VK_NULL_HANDLE) {
            mask_layout = VK_NULL_HANDLE;
            content_layout = VK_NULL_HANDLE;
            mask_pipeline = VK_NULL_HANDLE;
            content_pipeline = VK_NULL_HANDLE;
            return;
        }
        if (mask_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(vk_context.device, mask_pipeline, nullptr);
        }
        if (content_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(vk_context.device, content_pipeline, nullptr);
        }
        if (mask_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(vk_context.device, mask_layout, nullptr);
        }
        if (content_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(vk_context.device, content_layout, nullptr);
        }
        mask_layout = VK_NULL_HANDLE;
        content_layout = VK_NULL_HANDLE;
        mask_pipeline = VK_NULL_HANDLE;
        content_pipeline = VK_NULL_HANDLE;
    }

    void VK_Stencil::destroy_image() {
        if (vk_context.device == VK_NULL_HANDLE) {
            image = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            view = VK_NULL_HANDLE;
            image_initialized = false;
            return;
        }
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(vk_context.device, view, nullptr);
        }
        if (image != VK_NULL_HANDLE) {
            vkDestroyImage(vk_context.device, image, nullptr);
        }
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(vk_context.device, memory, nullptr);
        }
        image = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        view = VK_NULL_HANDLE;
        image_initialized = false;
    }

    VkFormat VK_Stencil::choose_stencil_format() const {
        const std::array<VkFormat, 3> candidates{
            VK_FORMAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
        };

        for (const VkFormat format : candidates) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(vk_context.physical_device, format, &properties);
            if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U) {
                return format;
            }
        }
        throw mxvk::Exception("failed to find a supported Vulkan stencil format");
    }

    VkPipeline VK_Stencil::create_pipeline(const std::string &vertex_shader,
                                           const std::string &fragment_shader,
                                           VkPipelineLayout layout,
                                           bool writes_stencil) const {
        const std::vector<char> vert_bytes = load_spv(vertex_shader);
        const std::vector<char> frag_bytes = load_spv(fragment_shader);
        const VkShaderModule vert_module = create_shader_module(vk_context.device, vert_bytes);
        VkShaderModule frag_module = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        try {
            frag_module = create_shader_module(vk_context.device, frag_bytes);

            std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vert_module;
            stages[0].pName = "main";
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = frag_module;
            stages[1].pName = "main";

            VkPipelineVertexInputStateCreateInfo vertex_input{};
            vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

            VkPipelineInputAssemblyStateCreateInfo input_assembly{};
            input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            VkPipelineViewportStateCreateInfo viewport_state{};
            viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport_state.viewportCount = 1;
            viewport_state.scissorCount = 1;

            const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic_state{};
            dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic_state.dynamicStateCount = 2;
            dynamic_state.pDynamicStates = dynamic_states;

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkStencilOpState stencil_op{};
            stencil_op.failOp = VK_STENCIL_OP_KEEP;
            stencil_op.passOp = writes_stencil ? VK_STENCIL_OP_REPLACE : VK_STENCIL_OP_KEEP;
            stencil_op.depthFailOp = VK_STENCIL_OP_KEEP;
            stencil_op.compareOp = writes_stencil ? VK_COMPARE_OP_ALWAYS : VK_COMPARE_OP_EQUAL;
            stencil_op.compareMask = 0xffU;
            stencil_op.writeMask = writes_stencil ? 0xffU : 0U;
            stencil_op.reference = 1U;

            VkPipelineDepthStencilStateCreateInfo depth_stencil{};
            depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth_stencil.depthTestEnable = VK_FALSE;
            depth_stencil.depthWriteEnable = VK_FALSE;
            depth_stencil.stencilTestEnable = VK_TRUE;
            depth_stencil.front = stencil_op;
            depth_stencil.back = stencil_op;

            VkPipelineColorBlendAttachmentState blend_attachment{};
            blend_attachment.colorWriteMask = writes_stencil ? 0U : (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT);

            VkPipelineColorBlendStateCreateInfo color_blend{};
            color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            color_blend.attachmentCount = 1;
            color_blend.pAttachments = &blend_attachment;

            VkPipelineRenderingCreateInfo rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachmentFormats = &render_color_format;
            rendering.depthAttachmentFormat = render_depth_format;
            rendering.stencilAttachmentFormat = stencil_image_format;

            VkGraphicsPipelineCreateInfo pipeline_info{};
            pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipeline_info.pNext = &rendering;
            pipeline_info.stageCount = static_cast<uint32_t>(stages.size());
            pipeline_info.pStages = stages.data();
            pipeline_info.pVertexInputState = &vertex_input;
            pipeline_info.pInputAssemblyState = &input_assembly;
            pipeline_info.pViewportState = &viewport_state;
            pipeline_info.pRasterizationState = &rasterizer;
            pipeline_info.pMultisampleState = &multisample;
            pipeline_info.pDepthStencilState = &depth_stencil;
            pipeline_info.pColorBlendState = &color_blend;
            pipeline_info.pDynamicState = &dynamic_state;
            pipeline_info.layout = layout;

            if (vkCreateGraphicsPipelines(vk_context.device, cache, 1, &pipeline_info, nullptr, &pipeline) != VK_SUCCESS) {
                throw mxvk::Exception("failed to create stencil graphics pipeline");
            }
        } catch (...) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(vk_context.device, pipeline, nullptr);
            }
            if (frag_module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(vk_context.device, frag_module, nullptr);
            }
            vkDestroyShaderModule(vk_context.device, vert_module, nullptr);
            throw;
        }

        vkDestroyShaderModule(vk_context.device, frag_module, nullptr);
        vkDestroyShaderModule(vk_context.device, vert_module, nullptr);
        return pipeline;
    }

} // namespace mxvk
