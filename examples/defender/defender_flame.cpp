#include "defender_window.hpp"

#include "mxvk/mxvk_exception.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace defender {

    void DefenderWindow::create_flame_resources() {
        create_flame_mesh();
        create_flame_swapchain_resources();
    }

    void DefenderWindow::cleanup_flame_resources() {
        cleanup_flame_swapchain_resources();
        if (flame_vertex_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, flame_vertex_buffer, nullptr);
            flame_vertex_buffer = VK_NULL_HANDLE;
        }
        if (flame_vertex_buffer_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, flame_vertex_buffer_memory, nullptr);
            flame_vertex_buffer_memory = VK_NULL_HANDLE;
        }
        flame_vertex_count = 0;
    }

    void DefenderWindow::cleanup_flame_swapchain_resources() {
        if (flame_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, flame_pipeline, nullptr);
            flame_pipeline = VK_NULL_HANDLE;
        }
        if (flame_pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, flame_pipeline_layout, nullptr);
            flame_pipeline_layout = VK_NULL_HANDLE;
        }
    }

    void DefenderWindow::create_flame_swapchain_resources() {
        if (flame_vertex_count == 0 || device == VK_NULL_HANDLE) {
            return;
        }
        create_flame_pipeline();
    }

    void DefenderWindow::create_flame_mesh() {
        constexpr int segments = 40;
        constexpr float base_z = 0.555f;
        constexpr float tip_z = 1.18f;
        constexpr float base_y = 0.040f;
        constexpr float outer_radius = 0.072f;
        constexpr float inner_radius = 0.034f;

        std::vector<space::FlameVertex> vertices{};
        vertices.reserve(static_cast<std::size_t>(segments) * 6U);

        const glm::vec4 outer_base_color{1.0f, 0.42f, 0.08f, 0.62f};
        const glm::vec4 outer_tip_color{0.7f, 0.08f, 0.0f, 0.0f};
        const glm::vec4 inner_base_color{1.0f, 0.94f, 0.52f, 0.86f};
        const glm::vec4 inner_tip_color{1.0f, 0.32f, 0.04f, 0.0f};

        auto add_cone = [&](float radius, const glm::vec4 &base_color, const glm::vec4 &tip_color) {
            const glm::vec3 tip{0.0f, base_y, tip_z};
            for (int i = 0; i < segments; ++i) {
                const float a0 = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * space::PI;
                const float a1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * 2.0f * space::PI;
                const glm::vec3 p0{std::cos(a0) * radius, base_y + std::sin(a0) * radius, base_z};
                const glm::vec3 p1{std::cos(a1) * radius, base_y + std::sin(a1) * radius, base_z};
                vertices.push_back({p0, base_color});
                vertices.push_back({p1, base_color});
                vertices.push_back({tip, tip_color});
            }
        };

        add_cone(outer_radius, outer_base_color, outer_tip_color);
        add_cone(inner_radius, inner_base_color, inner_tip_color);

        flame_vertex_count = static_cast<uint32_t>(vertices.size());
        const VkDeviceSize buffer_size = sizeof(space::FlameVertex) * static_cast<VkDeviceSize>(vertices.size());
        create_buffer(buffer_size,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      flame_vertex_buffer,
                      flame_vertex_buffer_memory);

        void *data = nullptr;
        if (vkMapMemory(device, flame_vertex_buffer_memory, 0, buffer_size, 0, &data) != VK_SUCCESS || data == nullptr) {
            throw mxvk::Exception("Failed to map defender flame vertex buffer");
        }
        std::memcpy(data, vertices.data(), static_cast<std::size_t>(buffer_size));
        vkUnmapMemory(device, flame_vertex_buffer_memory);
    }

    void DefenderWindow::create_flame_pipeline() {
        cleanup_flame_swapchain_resources();

        const std::vector<char> vert_shader_code = loadSpv(std::string(DEFENDER_SHADER_DIR) + "/flame.vert.spv");
        const std::vector<char> frag_shader_code = loadSpv(std::string(DEFENDER_SHADER_DIR) + "/flame.frag.spv");

        VkShaderModule vert_shader_module = createShaderModule(device, vert_shader_code);
        VkShaderModule frag_shader_module = VK_NULL_HANDLE;

        try {
            frag_shader_module = createShaderModule(device, frag_shader_code);

            VkPipelineShaderStageCreateInfo vert_stage{};
            vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vert_stage.module = vert_shader_module;
            vert_stage.pName = "main";

            VkPipelineShaderStageCreateInfo frag_stage{};
            frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            frag_stage.module = frag_shader_module;
            frag_stage.pName = "main";

            std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {vert_stage, frag_stage};

            VkVertexInputBindingDescription binding_description{};
            binding_description.binding = 0;
            binding_description.stride = sizeof(space::FlameVertex);
            binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            std::array<VkVertexInputAttributeDescription, 2> attributes{};
            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributes[0].offset = offsetof(space::FlameVertex, pos);
            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributes[1].offset = offsetof(space::FlameVertex, color);

            VkPipelineVertexInputStateCreateInfo vertex_input{};
            vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertex_input.vertexBindingDescriptionCount = 1;
            vertex_input.pVertexBindingDescriptions = &binding_description;
            vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
            vertex_input.pVertexAttributeDescriptions = attributes.data();

            VkPipelineInputAssemblyStateCreateInfo input_assembly{};
            input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            input_assembly.primitiveRestartEnable = VK_FALSE;

            VkPipelineViewportStateCreateInfo viewport_state{};
            viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport_state.viewportCount = 1;
            viewport_state.scissorCount = 1;

            const std::array<VkDynamicState, 2> dynamic_states = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            };
            VkPipelineDynamicStateCreateInfo dynamic_info{};
            dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
            dynamic_info.pDynamicStates = dynamic_states.data();

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;

            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depth_stencil{};
            depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth_stencil.depthTestEnable = VK_TRUE;
            depth_stencil.depthWriteEnable = VK_FALSE;
            depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
            depth_stencil.depthBoundsTestEnable = VK_FALSE;
            depth_stencil.stencilTestEnable = VK_FALSE;

            VkPipelineColorBlendAttachmentState color_blend_attachment{};
            color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            color_blend_attachment.blendEnable = VK_TRUE;
            color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
            color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

            VkPipelineColorBlendStateCreateInfo color_blending{};
            color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            color_blending.logicOpEnable = VK_FALSE;
            color_blending.attachmentCount = 1;
            color_blending.pAttachments = &color_blend_attachment;

            VkPushConstantRange push_range{};
            push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            push_range.offset = 0;
            push_range.size = sizeof(space::FlamePushConstants);

            VkPipelineLayoutCreateInfo layout_info{};
            layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_info.pushConstantRangeCount = 1;
            layout_info.pPushConstantRanges = &push_range;

            if (vkCreatePipelineLayout(device, &layout_info, nullptr, &flame_pipeline_layout) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create defender flame pipeline layout");
            }

            const VkFormat color_format = getSwapchainFormat();
            const VkFormat depth_format = getDepthFormat();

            VkPipelineRenderingCreateInfo rendering_info{};
            rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachmentFormats = &color_format;
            if (depth_format != VK_FORMAT_UNDEFINED) {
                rendering_info.depthAttachmentFormat = depth_format;
            }

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
            pipeline_info.pDynamicState = &dynamic_info;
            pipeline_info.layout = flame_pipeline_layout;
            pipeline_info.renderPass = VK_NULL_HANDLE;
            pipeline_info.subpass = 0;

            if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &flame_pipeline) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create defender flame pipeline");
            }
        } catch (...) {
            if (frag_shader_module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, frag_shader_module, nullptr);
            }
            vkDestroyShaderModule(device, vert_shader_module, nullptr);
            cleanup_flame_swapchain_resources();
            throw;
        }

        vkDestroyShaderModule(device, frag_shader_module, nullptr);
        vkDestroyShaderModule(device, vert_shader_module, nullptr);
    }

    void DefenderWindow::draw_engine_flame(VkCommandBuffer cmd, const VkExtent2D &extent, const glm::mat4 &view, const glm::mat4 &projection) {
        const bool boost_active = boost_pressed || controller_boost_pressed;
        const bool propulsion_active = propulsion_pressed || controller_propulsion_pressed || boost_active;
        if (!propulsion_active || ship.current_speed <= 1.0f || flame_pipeline == VK_NULL_HANDLE || flame_vertex_buffer == VK_NULL_HANDLE || flame_vertex_count == 0) {
            return;
        }

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        space::FlamePushConstants pc{};
        pc.mvp = projection * view * last_ship_model_matrix;
        pc.params = glm::vec4(elapsed_seconds, std::clamp(ship.current_speed / ship.max_speed, 0.0f, 2.0f), boost_active ? 1.0f : 0.0f, 0.0f);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flame_pipeline);
        vkCmdPushConstants(cmd,
                           flame_pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(pc),
                           &pc);

        VkBuffer vertex_buffers[] = {flame_vertex_buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);
        vkCmdDraw(cmd, flame_vertex_count, 1, 0, 0);
    }

    void DefenderWindow::create_buffer(VkDeviceSize size,
                                       VkBufferUsageFlags usage,
                                       VkMemoryPropertyFlags properties,
                                       VkBuffer &buffer,
                                       VkDeviceMemory &buffer_memory) const {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &buffer_info, nullptr, &buffer) != VK_SUCCESS) {
            throw mxvk::Exception("Failed to create defender buffer");
        }

        VkMemoryRequirements mem_requirements{};
        vkGetBufferMemoryRequirements(device, buffer, &mem_requirements);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_requirements.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device, &alloc_info, nullptr, &buffer_memory) != VK_SUCCESS) {
            vkDestroyBuffer(device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            throw mxvk::Exception("Failed to allocate defender buffer memory");
        }
        if (vkBindBufferMemory(device, buffer, buffer_memory, 0) != VK_SUCCESS) {
            vkFreeMemory(device, buffer_memory, nullptr);
            vkDestroyBuffer(device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            buffer_memory = VK_NULL_HANDLE;
            throw mxvk::Exception("Failed to bind defender buffer memory");
        }
    }

    [[nodiscard]] uint32_t DefenderWindow::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties mem_properties{};
        vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

        for (uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
            if ((type_filter & (1U << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw mxvk::Exception("Failed to find defender memory type");
    }

} // namespace defender
