#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_resource.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

namespace {
    struct SceneVertex {
        glm::vec3 position{};
        glm::vec2 texCoord{};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec4 color{1.0f};
    };

    struct MeshData {
        std::vector<SceneVertex> vertices;
        std::vector<std::uint32_t> indices;
    };

    struct MeshResources {
        mxvk::BufferResource vertexBuffer;
        mxvk::BufferResource indexBuffer;
        uint32_t indexCount = 0;
    };

    struct PipelineResources {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
    };

    struct PushConstants {
        alignas(16) glm::mat4 viewProjection{1.0f};
        alignas(16) glm::vec4 cameraTime{0.0f};
        alignas(16) glm::vec4 viewport{1.0f};
    };

    constexpr int WATER_GRID_RESOLUTION = 2048;
    constexpr float WATER_SIZE = 320.0f;
    constexpr float SCENE_REFERENCE_ASPECT = 16.0f / 9.0f;

    void check_vk(VkResult result, const std::string &message) {
        if (result != VK_SUCCESS) {
            throw mxvk::Exception(message);
        }
    }

} // namespace

namespace example {
    class WaterWindow : public mxvk::VK_Window {
      public:
        WaterWindow(const std::string &path, const std::string &title, int width, int height, bool fullscreen, bool enable_vsync)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              shader_root((path.empty() ? std::string(WATER_ASSET_DIR) : path) + "/data") {
            setClearColor(0.60f, 0.78f, 0.96f, 1.0f);
        }

        ~WaterWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            destroyPipeline(sky_pipeline);
            destroyPipeline(water_pipeline);
            destroyMesh(water_mesh);
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
                return;
            }

            if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
                const bool pressed = e.type == SDL_EVENT_KEY_DOWN;
                switch (e.key.key) {
                case SDLK_LEFT:
                    rotate_left = pressed;
                    return;
                case SDLK_RIGHT:
                    rotate_right = pressed;
                    return;
                case SDLK_UP:
                case SDLK_PAGEUP:
                    zoom_in = pressed;
                    return;
                case SDLK_DOWN:
                case SDLK_PAGEDOWN:
                    zoom_out = pressed;
                    return;
                default:
                    break;
                }
            }

            if (e.type == SDL_EVENT_QUIT) {
                exit();
            }
        }

        void onSwapchainAboutToRecreate() override {
            destroyPipeline(sky_pipeline);
            destroyPipeline(water_pipeline);
        }

        void onPrepareFrameRendering([[maybe_unused]] VkCommandBuffer cmd, [[maybe_unused]] uint32_t image_index) override {
            if (water_mesh_uploaded) {
                return;
            }

            uploadMesh(generateWaterMesh(), water_mesh);
            water_mesh_uploaded = true;
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, [[maybe_unused]] uint32_t image_index) override {
            if (!water_mesh_uploaded || !ensurePipelines()) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const float delta_seconds = std::chrono::duration<float>(now - last_frame_time).count();
            last_frame_time = now;
            updateCamera(delta_seconds);

            const float elapsed_seconds = std::chrono::duration<float>(now - start_time).count();
            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = (extent.height > 0U) ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;

            const glm::vec3 camera_target(0.0f, -0.15f, -18.0f);
            const float yaw = glm::radians(camera_yaw_degrees);
            const float pitch = glm::radians(camera_pitch_degrees);
            const glm::vec3 camera_offset(
                std::sin(yaw) * std::cos(pitch) * camera_distance,
                std::sin(pitch) * camera_distance,
                std::cos(yaw) * std::cos(pitch) * camera_distance);
            const glm::vec3 camera_pos = camera_target + camera_offset;
            const glm::mat4 view = glm::lookAt(camera_pos, camera_target, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 projection = glm::perspective(glm::radians(62.0f), aspect, 0.1f, 260.0f);
            projection[1][1] *= -1.0f;

            const PushConstants push_constants{
                projection * view,
                glm::vec4(camera_pos, elapsed_seconds),
                glm::vec4(aspect, SCENE_REFERENCE_ASPECT, 0.0f, 0.0f),
            };

            drawSky(cmd, sky_pipeline, push_constants);
            drawMesh(cmd, water_mesh, water_pipeline, push_constants);
        }

      private:
        std::string shader_root;
        std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point last_frame_time{start_time};
        MeshResources water_mesh;
        PipelineResources water_pipeline;
        PipelineResources sky_pipeline;
        bool water_mesh_uploaded = false;
        float camera_yaw_degrees = 0.0f;
        float camera_pitch_degrees = 10.5f;
        float camera_distance = 27.5f;
        bool rotate_left = false;
        bool rotate_right = false;
        bool zoom_in = false;
        bool zoom_out = false;

        void updateCamera(float delta_seconds) {
            constexpr float ROTATION_SPEED_DEGREES = 42.0f;
            constexpr float ZOOM_SPEED = 14.0f;

            if (rotate_left) {
                camera_yaw_degrees -= ROTATION_SPEED_DEGREES * delta_seconds;
            }
            if (rotate_right) {
                camera_yaw_degrees += ROTATION_SPEED_DEGREES * delta_seconds;
            }
            if (zoom_in) {
                camera_distance -= ZOOM_SPEED * delta_seconds;
            }
            if (zoom_out) {
                camera_distance += ZOOM_SPEED * delta_seconds;
            }

            camera_pitch_degrees = std::clamp(camera_pitch_degrees, -4.0f, 46.0f);
            camera_distance = std::clamp(camera_distance, 10.0f, 72.0f);
        }

        static MeshData generateWaterMesh() {
            MeshData mesh;
            mesh.vertices.reserve(static_cast<std::size_t>(WATER_GRID_RESOLUTION + 1) * static_cast<std::size_t>(WATER_GRID_RESOLUTION + 1));
            mesh.indices.reserve(static_cast<std::size_t>(WATER_GRID_RESOLUTION) * static_cast<std::size_t>(WATER_GRID_RESOLUTION) * 6U);

            for (int z = 0; z <= WATER_GRID_RESOLUTION; ++z) {
                const float vz = static_cast<float>(z) / static_cast<float>(WATER_GRID_RESOLUTION);
                for (int x = 0; x <= WATER_GRID_RESOLUTION; ++x) {
                    const float vx = static_cast<float>(x) / static_cast<float>(WATER_GRID_RESOLUTION);
                    mesh.vertices.push_back({
                        glm::vec3((vx - 0.5f) * WATER_SIZE, 0.0f, (vz - 0.5f) * WATER_SIZE),
                        glm::vec2(vx * 72.0f, vz * 72.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f),
                        glm::vec4(0.30f, 0.72f, 0.92f, 1.0f),
                    });
                }
            }

            const auto vertex_index = [](int x, int z) {
                return static_cast<std::uint32_t>(z * (WATER_GRID_RESOLUTION + 1) + x);
            };
            for (int z = 0; z < WATER_GRID_RESOLUTION; ++z) {
                for (int x = 0; x < WATER_GRID_RESOLUTION; ++x) {
                    const std::uint32_t a = vertex_index(x, z);
                    const std::uint32_t b = vertex_index(x + 1, z);
                    const std::uint32_t c = vertex_index(x, z + 1);
                    const std::uint32_t d = vertex_index(x + 1, z + 1);
                    mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
                }
            }

            return mesh;
        }

        void uploadMesh(const MeshData &data, MeshResources &mesh) const {
            const mxvk::VulkanContext context{
                device,
                physical_device,
                graphics_queue,
                command_pool,
            };
            mesh.indexCount = static_cast<uint32_t>(data.indices.size());

            const VkDeviceSize vertex_size = sizeof(SceneVertex) * data.vertices.size();
            uploadDeviceBuffer(
                context,
                data.vertices.data(),
                vertex_size,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                mesh.vertexBuffer);

            const VkDeviceSize index_size = sizeof(std::uint32_t) * data.indices.size();
            uploadDeviceBuffer(
                context,
                data.indices.data(),
                index_size,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                mesh.indexBuffer);
        }

        void drawMesh(VkCommandBuffer cmd, const MeshResources &mesh, const PipelineResources &pipeline, const PushConstants &push_constants) const {
            if (mesh.vertexBuffer.buffer == VK_NULL_HANDLE || mesh.indexBuffer.buffer == VK_NULL_HANDLE || pipeline.pipeline == VK_NULL_HANDLE) {
                return;
            }

            const VkDeviceSize offsets[] = {0};
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
            vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer.buffer, offsets);
            vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdPushConstants(
                cmd,
                pipeline.layout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(PushConstants),
                &push_constants);
            vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
        }

        void drawSky(VkCommandBuffer cmd, const PipelineResources &pipeline, const PushConstants &push_constants) const {
            if (pipeline.pipeline == VK_NULL_HANDLE) {
                return;
            }

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
            vkCmdPushConstants(
                cmd,
                pipeline.layout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(PushConstants),
                &push_constants);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        }

        void uploadDeviceBuffer(const mxvk::VulkanContext &context,
                                const void *data,
                                VkDeviceSize size,
                                VkBufferUsageFlags usage,
                                mxvk::BufferResource &buffer) const {
            mxvk::BufferResource staging;
            mxvk::create_buffer(
                context,
                size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                staging);

            try {
                mxvk::map_buffer(device, staging);
                std::memcpy(staging.mapped, data, static_cast<std::size_t>(size));
                mxvk::unmap_buffer(device, staging);

                mxvk::create_buffer(
                    context,
                    size,
                    usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    buffer);
                mxvk::copy_buffer(context, staging.buffer, buffer.buffer, size);
            } catch (...) {
                mxvk::destroy_buffer(device, staging);
                throw;
            }

            mxvk::destroy_buffer(device, staging);
        }

        void destroyMesh(MeshResources &mesh) const {
            mxvk::destroy_buffer(device, mesh.vertexBuffer);
            mxvk::destroy_buffer(device, mesh.indexBuffer);
            mesh.indexCount = 0;
        }

        bool ensurePipelines() {
            if (sky_pipeline.pipeline == VK_NULL_HANDLE) {
                createPipeline("sky.vert.spv", "sky.frag.spv", false, false, false, sky_pipeline);
            }
            if (water_pipeline.pipeline == VK_NULL_HANDLE) {
                createPipeline("water.vert.spv", "water.frag.spv", true, false, true, water_pipeline);
            }
            return sky_pipeline.pipeline != VK_NULL_HANDLE && water_pipeline.pipeline != VK_NULL_HANDLE;
        }

        void createPipeline(const std::string &vertex_shader, const std::string &fragment_shader, bool useVertexInput, bool alphaBlend, bool depthTest, PipelineResources &resources) {
            if (device == VK_NULL_HANDLE || swapchain_format == VK_FORMAT_UNDEFINED) {
                return;
            }

            const std::vector<char> vert_bytes = loadSpv(shader_root + "/" + vertex_shader);
            const std::vector<char> frag_bytes = loadSpv(shader_root + "/" + fragment_shader);

            const VkShaderModule vert_module = createShaderModule(device, vert_bytes);
            VkShaderModule frag_module = VK_NULL_HANDLE;
            try {
                frag_module = createShaderModule(device, frag_bytes);

                std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{};
                shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                shader_stages[0].module = vert_module;
                shader_stages[0].pName = "main";
                shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                shader_stages[1].module = frag_module;
                shader_stages[1].pName = "main";

                VkVertexInputBindingDescription binding_description{};
                std::array<VkVertexInputAttributeDescription, 4> attribute_descriptions{};
                if (useVertexInput) {
                    binding_description = vertexBindingDescription();
                    attribute_descriptions = vertexAttributeDescriptions();
                }

                VkPipelineVertexInputStateCreateInfo vertex_input{};
                vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                vertex_input.vertexBindingDescriptionCount = useVertexInput ? 1U : 0U;
                vertex_input.pVertexBindingDescriptions = useVertexInput ? &binding_description : nullptr;
                vertex_input.vertexAttributeDescriptionCount = useVertexInput ? static_cast<uint32_t>(attribute_descriptions.size()) : 0U;
                vertex_input.pVertexAttributeDescriptions = useVertexInput ? attribute_descriptions.data() : nullptr;

                VkPipelineInputAssemblyStateCreateInfo input_assembly{};
                input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

                VkPipelineViewportStateCreateInfo viewport_state{};
                viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewport_state.viewportCount = 1;
                viewport_state.scissorCount = 1;

                const VkDynamicState dynamic_states[] = {
                    VK_DYNAMIC_STATE_VIEWPORT,
                    VK_DYNAMIC_STATE_SCISSOR,
                };
                VkPipelineDynamicStateCreateInfo dynamic_state{};
                dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamic_state.dynamicStateCount = 2;
                dynamic_state.pDynamicStates = dynamic_states;

                VkPipelineRasterizationStateCreateInfo rasterizer{};
                rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
                rasterizer.lineWidth = 1.0f;
                rasterizer.cullMode = VK_CULL_MODE_NONE;
                rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

                VkPipelineMultisampleStateCreateInfo multisampling{};
                multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

                VkPipelineDepthStencilStateCreateInfo depth_stencil{};
                depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depth_stencil.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
                depth_stencil.depthWriteEnable = (depthTest && !alphaBlend) ? VK_TRUE : VK_FALSE;
                depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

                VkPipelineColorBlendAttachmentState color_blend_attachment{};
                color_blend_attachment.colorWriteMask =
                    VK_COLOR_COMPONENT_R_BIT |
                    VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT |
                    VK_COLOR_COMPONENT_A_BIT;
                color_blend_attachment.blendEnable = alphaBlend ? VK_TRUE : VK_FALSE;
                color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
                color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

                VkPipelineColorBlendStateCreateInfo color_blending{};
                color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                color_blending.attachmentCount = 1;
                color_blending.pAttachments = &color_blend_attachment;

                VkPushConstantRange push_constant_range{};
                push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                push_constant_range.size = sizeof(PushConstants);

                VkPipelineLayoutCreateInfo pipeline_layout_info{};
                pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                pipeline_layout_info.pushConstantRangeCount = 1;
                pipeline_layout_info.pPushConstantRanges = &push_constant_range;
                check_vk(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &resources.layout), "water: failed to create pipeline layout");

                VkPipelineRenderingCreateInfo pipeline_rendering_info{};
                pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                pipeline_rendering_info.colorAttachmentCount = 1;
                pipeline_rendering_info.pColorAttachmentFormats = &swapchain_format;
                if (depth_format != VK_FORMAT_UNDEFINED) {
                    pipeline_rendering_info.depthAttachmentFormat = depth_format;
                }

                VkGraphicsPipelineCreateInfo pipeline_info{};
                pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipeline_info.pNext = &pipeline_rendering_info;
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
                pipeline_info.layout = resources.layout;
                pipeline_info.renderPass = VK_NULL_HANDLE;

                check_vk(vkCreateGraphicsPipelines(device, pipeline_cache, 1, &pipeline_info, nullptr, &resources.pipeline), "water: failed to create graphics pipeline");
            } catch (...) {
                destroyPipeline(resources);
                if (frag_module != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(device, frag_module, nullptr);
                }
                vkDestroyShaderModule(device, vert_module, nullptr);
                throw;
            }

            vkDestroyShaderModule(device, frag_module, nullptr);
            vkDestroyShaderModule(device, vert_module, nullptr);
        }

        static VkVertexInputBindingDescription vertexBindingDescription() {
            VkVertexInputBindingDescription binding{};
            binding.binding = 0;
            binding.stride = sizeof(SceneVertex);
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return binding;
        }

        static std::array<VkVertexInputAttributeDescription, 4> vertexAttributeDescriptions() {
            std::array<VkVertexInputAttributeDescription, 4> attributes{};
            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributes[0].offset = offsetof(SceneVertex, position);
            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
            attributes[1].offset = offsetof(SceneVertex, texCoord);
            attributes[2].binding = 0;
            attributes[2].location = 2;
            attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributes[2].offset = offsetof(SceneVertex, normal);
            attributes[3].binding = 0;
            attributes[3].location = 3;
            attributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributes[3].offset = offsetof(SceneVertex, color);
            return attributes;
        }

        void destroyPipeline(PipelineResources &resources) const {
            if (device == VK_NULL_HANDLE) {
                resources.pipeline = VK_NULL_HANDLE;
                resources.layout = VK_NULL_HANDLE;
                return;
            }

            if (resources.pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, resources.pipeline, nullptr);
                resources.pipeline = VK_NULL_HANDLE;
            }
            if (resources.layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, resources.layout, nullptr);
                resources.layout = VK_NULL_HANDLE;
            }
        }
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::WaterWindow window(args.path, "MXVK - Bluesky", args.width, args.height, args.fullscreen, args.enable_vsync);
        window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
