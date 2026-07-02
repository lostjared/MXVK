#include "mxvk/mxvk_point_sprite_batch.hpp"

#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <format>
#include <fstream>

namespace mxvk {

    VK_PointSpriteBatch::~VK_PointSpriteBatch() {
        cleanup();
    }

    void VK_PointSpriteBatch::load(VK_Window *window,
                                   const std::string &texture_path_value,
                                   const std::string &vertex_shader_path_value,
                                   const std::string &fragment_shader_path_value,
                                   size_t max_vertex_count) {
        if (window == nullptr) {
            throw mxvk::Exception("VK_PointSpriteBatch::load called with null window");
        }
        if (max_vertex_count == 0) {
            throw mxvk::Exception("VK_PointSpriteBatch::load requires non-zero vertex capacity");
        }

        cleanup();
        context = {
            .device = window->getDevice(),
            .physical_device = window->getPhysicalDevice(),
            .graphics_queue = window->getGraphicsQueue(),
            .command_pool = window->getCommandPool(),
        };
        pipeline_cache = window->getPipelineCache();
        color_attachment_format = window->getSwapchainFormat();
        depth_attachment_format = window->getDepthFormat();
        image_count = window->getSwapchainImageCount();
        texture_path = texture_path_value;
        vertex_shader_path = vertex_shader_path_value;
        fragment_shader_path = fragment_shader_path_value;
        max_vertices = max_vertex_count;

        if (context.device == VK_NULL_HANDLE || context.physical_device == VK_NULL_HANDLE ||
            context.graphics_queue == VK_NULL_HANDLE || context.command_pool == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot create point-sprite batch before Vulkan render resources are available");
        }
        if (color_attachment_format == VK_FORMAT_UNDEFINED || image_count == 0) {
            throw mxvk::Exception("Cannot create point-sprite batch before swapchain resources are available");
        }

        create_texture_from_png(context, texture_path, texture);
        create_vertex_buffer();
        create_swapchain_resources();
        batch_loaded = true;
    }

    void VK_PointSpriteBatch::resize(VK_Window *window) {
        if (!batch_loaded || window == nullptr) {
            return;
        }

        context.device = window->getDevice();
        context.physical_device = window->getPhysicalDevice();
        context.graphics_queue = window->getGraphicsQueue();
        context.command_pool = window->getCommandPool();
        pipeline_cache = window->getPipelineCache();
        color_attachment_format = window->getSwapchainFormat();
        depth_attachment_format = window->getDepthFormat();
        image_count = window->getSwapchainImageCount();

        cleanup_swapchain_resources();
        create_swapchain_resources();
    }

    void VK_PointSpriteBatch::cleanup() {
        cleanup_swapchain_resources();
        destroy_vertex_buffer();
        destroy_texture(context.device, texture);
        context = {};
        pipeline_cache = VK_NULL_HANDLE;
        color_attachment_format = VK_FORMAT_UNDEFINED;
        depth_attachment_format = VK_FORMAT_UNDEFINED;
        image_count = 0;
        texture_path.clear();
        vertex_shader_path.clear();
        fragment_shader_path.clear();
        max_vertices = 0;
        active_vertices = 0;
        batch_loaded = false;
    }

    void VK_PointSpriteBatch::upload_vertices(const PointSpriteVertex *vertices, size_t count) {
        if (!batch_loaded || vertex_buffer.mapped == nullptr || vertices == nullptr || count == 0) {
            active_vertices = 0;
            return;
        }
        if (count > max_vertices) {
            throw mxvk::Exception("VK_PointSpriteBatch::upload_vertices exceeds batch capacity");
        }

        std::memcpy(vertex_buffer.mapped, vertices, count * sizeof(PointSpriteVertex));
        active_vertices = count;
    }

    void VK_PointSpriteBatch::update_mvp(uint32_t image_index, const glm::mat4 &mvp) {
        if (image_index >= uniform_buffers.size() || uniform_buffers[image_index].mapped == nullptr) {
            return;
        }

        UniformBufferObject ubo{};
        ubo.mvp = mvp;
        std::memcpy(uniform_buffers[image_index].mapped, &ubo, sizeof(ubo));
    }

    void VK_PointSpriteBatch::render(VkCommandBuffer cmd, uint32_t image_index) {
        if (!batch_loaded || active_vertices == 0 || pipeline == VK_NULL_HANDLE || image_index >= descriptor_sets.size()) {
            return;
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkBuffer buffers[] = {vertex_buffer.buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout,
            0,
            1,
            &descriptor_sets[image_index],
            0,
            nullptr);
        vkCmdDraw(cmd, static_cast<uint32_t>(active_vertices), 1, 0, 0);
    }

    void VK_PointSpriteBatch::set_additive_blending(bool enabled) {
        if (additive_blending == enabled) {
            return;
        }
        additive_blending = enabled;
        if (batch_loaded) {
            destroy_pipeline();
            create_pipeline();
        }
    }

    void VK_PointSpriteBatch::set_depth_test_enabled(bool enabled) {
        if (depth_test_enabled == enabled) {
            return;
        }
        depth_test_enabled = enabled;
        if (batch_loaded) {
            destroy_pipeline();
            create_pipeline();
        }
    }

    void VK_PointSpriteBatch::set_depth_write_enabled(bool enabled) {
        if (depth_write_enabled == enabled) {
            return;
        }
        depth_write_enabled = enabled;
        if (batch_loaded) {
            destroy_pipeline();
            create_pipeline();
        }
    }

    void VK_PointSpriteBatch::create_vertex_buffer() {
        create_buffer(
            context,
            sizeof(PointSpriteVertex) * static_cast<VkDeviceSize>(max_vertices),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vertex_buffer);
        map_buffer(context.device, vertex_buffer);
    }

    void VK_PointSpriteBatch::destroy_vertex_buffer() {
        destroy_buffer(context.device, vertex_buffer);
    }

    void VK_PointSpriteBatch::create_swapchain_resources() {
        create_descriptor_set_layout();
        create_uniform_buffers();
        create_descriptor_pool();
        create_descriptor_sets();
        create_pipeline();
    }

    void VK_PointSpriteBatch::cleanup_swapchain_resources() {
        if (context.device == VK_NULL_HANDLE) {
            descriptor_sets.clear();
            uniform_buffers.clear();
            return;
        }
        destroy_pipeline();
        if (descriptor_pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
            descriptor_pool = VK_NULL_HANDLE;
        }
        if (descriptor_set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);
            descriptor_set_layout = VK_NULL_HANDLE;
        }
        destroy_uniform_buffers();
        descriptor_sets.clear();
    }

    void VK_PointSpriteBatch::create_descriptor_set_layout() {
        VkDescriptorSetLayoutBinding sampler_binding{};
        sampler_binding.binding = 0;
        sampler_binding.descriptorCount = 1;
        sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding ubo_binding{};
        ubo_binding.binding = 1;
        ubo_binding.descriptorCount = 1;
        ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings{sampler_binding, ubo_binding};
        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
        layout_info.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr, &descriptor_set_layout) != VK_SUCCESS) {
            throw mxvk::Exception("failed to create point-sprite descriptor set layout");
        }
    }

    void VK_PointSpriteBatch::create_uniform_buffers() {
        uniform_buffers.resize(image_count);
        for (auto &buffer : uniform_buffers) {
            create_buffer(
                context,
                sizeof(UniformBufferObject),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                buffer);
            map_buffer(context.device, buffer);
        }
    }

    void VK_PointSpriteBatch::destroy_uniform_buffers() {
        for (auto &buffer : uniform_buffers) {
            destroy_buffer(context.device, buffer);
        }
        uniform_buffers.clear();
    }

    void VK_PointSpriteBatch::create_descriptor_pool() {
        const uint32_t count = static_cast<uint32_t>(image_count);
        std::array<VkDescriptorPoolSize, 2> pool_sizes{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[0].descriptorCount = count;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[1].descriptorCount = count;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();
        pool_info.maxSets = count;
        if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
            throw mxvk::Exception("failed to create point-sprite descriptor pool");
        }
    }

    void VK_PointSpriteBatch::create_descriptor_sets() {
        std::vector<VkDescriptorSetLayout> layouts(image_count, descriptor_set_layout);
        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = descriptor_pool;
        alloc_info.descriptorSetCount = static_cast<uint32_t>(image_count);
        alloc_info.pSetLayouts = layouts.data();

        descriptor_sets.resize(image_count, VK_NULL_HANDLE);
        if (vkAllocateDescriptorSets(context.device, &alloc_info, descriptor_sets.data()) != VK_SUCCESS) {
            throw mxvk::Exception("failed to allocate point-sprite descriptor sets");
        }

        for (size_t i = 0; i < image_count; ++i) {
            VkDescriptorImageInfo image_info{};
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            image_info.imageView = texture.view;
            image_info.sampler = texture.sampler;

            VkDescriptorBufferInfo buffer_info{};
            buffer_info.buffer = uniform_buffers[i].buffer;
            buffer_info.offset = 0;
            buffer_info.range = sizeof(UniformBufferObject);

            std::array<VkWriteDescriptorSet, 2> writes{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = descriptor_sets[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &image_info;
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = descriptor_sets[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo = &buffer_info;

            vkUpdateDescriptorSets(context.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    void VK_PointSpriteBatch::create_pipeline() {
        destroy_pipeline();

        const std::vector<char> vert_code = read_shader_file(vertex_shader_path);
        const std::vector<char> frag_code = read_shader_file(fragment_shader_path);
        const VkShaderModule vert_module = create_shader_module(vert_code);
        VkShaderModule frag_module = VK_NULL_HANDLE;

        try {
            frag_module = create_shader_module(frag_code);

            VkPipelineShaderStageCreateInfo vert_stage{};
            vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vert_stage.module = vert_module;
            vert_stage.pName = "main";

            VkPipelineShaderStageCreateInfo frag_stage{};
            frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            frag_stage.module = frag_module;
            frag_stage.pName = "main";
            std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {vert_stage, frag_stage};

            VkVertexInputBindingDescription binding{};
            binding.binding = 0;
            binding.stride = sizeof(PointSpriteVertex);
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            std::array<VkVertexInputAttributeDescription, 3> attributes{};
            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributes[0].offset = offsetof(PointSpriteVertex, position);
            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = VK_FORMAT_R32_SFLOAT;
            attributes[1].offset = offsetof(PointSpriteVertex, size);
            attributes[2].binding = 0;
            attributes[2].location = 2;
            attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributes[2].offset = offsetof(PointSpriteVertex, color);

            VkPipelineVertexInputStateCreateInfo vertex_input{};
            vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertex_input.vertexBindingDescriptionCount = 1;
            vertex_input.pVertexBindingDescriptions = &binding;
            vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
            vertex_input.pVertexAttributeDescriptions = attributes.data();

            VkPipelineInputAssemblyStateCreateInfo input_assembly{};
            input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            input_assembly.primitiveRestartEnable = VK_FALSE;

            std::array<VkDynamicState, 2> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic_state{};
            dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
            dynamic_state.pDynamicStates = dynamic_states.data();

            VkPipelineViewportStateCreateInfo viewport_state{};
            viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport_state.viewportCount = 1;
            viewport_state.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depth_stencil{};
            depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth_stencil.depthTestEnable = depth_test_enabled ? VK_TRUE : VK_FALSE;
            depth_stencil.depthWriteEnable = depth_write_enabled ? VK_TRUE : VK_FALSE;
            depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

            VkPipelineColorBlendAttachmentState blend{};
            blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blend.blendEnable = VK_TRUE;
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend.dstColorBlendFactor = additive_blending ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.colorBlendOp = VK_BLEND_OP_ADD;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            blend.alphaBlendOp = VK_BLEND_OP_ADD;

            VkPipelineColorBlendStateCreateInfo color_blending{};
            color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            color_blending.attachmentCount = 1;
            color_blending.pAttachments = &blend;

            VkPipelineLayoutCreateInfo layout_info{};
            layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_info.setLayoutCount = 1;
            layout_info.pSetLayouts = &descriptor_set_layout;
            if (vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
                throw mxvk::Exception("failed to create point-sprite pipeline layout");
            }

            VkPipelineRenderingCreateInfo rendering_info{};
            rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachmentFormats = &color_attachment_format;
            rendering_info.depthAttachmentFormat = depth_attachment_format;
            rendering_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

            VkGraphicsPipelineCreateInfo pipeline_info{};
            pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipeline_info.pNext = &rendering_info;
            pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
            pipeline_info.pStages = shader_stages.data();
            pipeline_info.pVertexInputState = &vertex_input;
            pipeline_info.pInputAssemblyState = &input_assembly;
            pipeline_info.pViewportState = &viewport_state;
            pipeline_info.pRasterizationState = &rasterizer;
            pipeline_info.pMultisampleState = &multisampling;
            pipeline_info.pDepthStencilState = &depth_stencil;
            pipeline_info.pColorBlendState = &color_blending;
            pipeline_info.pDynamicState = &dynamic_state;
            pipeline_info.layout = pipeline_layout;
            pipeline_info.renderPass = VK_NULL_HANDLE;
            if (vkCreateGraphicsPipelines(context.device, pipeline_cache, 1, &pipeline_info, nullptr, &pipeline) != VK_SUCCESS) {
                throw mxvk::Exception("failed to create point-sprite graphics pipeline");
            }
        } catch (...) {
            if (frag_module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(context.device, frag_module, nullptr);
            }
            vkDestroyShaderModule(context.device, vert_module, nullptr);
            throw;
        }

        vkDestroyShaderModule(context.device, frag_module, nullptr);
        vkDestroyShaderModule(context.device, vert_module, nullptr);
    }

    void VK_PointSpriteBatch::destroy_pipeline() {
        if (context.device == VK_NULL_HANDLE) {
            pipeline = VK_NULL_HANDLE;
            pipeline_layout = VK_NULL_HANDLE;
            return;
        }
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(context.device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        if (pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
            pipeline_layout = VK_NULL_HANDLE;
        }
    }

    std::vector<char> VK_PointSpriteBatch::read_shader_file(const std::string &path) const {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw mxvk::Exception(std::format("failed to open point-sprite shader: {}", path));
        }

        const std::streamsize size = file.tellg();
        if (size <= 0) {
            throw mxvk::Exception(std::format("point-sprite shader is empty: {}", path));
        }

        std::vector<char> buffer(static_cast<size_t>(size));
        file.seekg(0);
        file.read(buffer.data(), size);
        return buffer;
    }

    VkShaderModule VK_PointSpriteBatch::create_shader_module(const std::vector<char> &code) const {
        VkShaderModuleCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        create_info.codeSize = code.size();
        create_info.pCode = reinterpret_cast<const uint32_t *>(code.data());

        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(context.device, &create_info, nullptr, &module) != VK_SUCCESS) {
            throw mxvk::Exception("failed to create point-sprite shader module");
        }
        return module;
    }

} // namespace mxvk
