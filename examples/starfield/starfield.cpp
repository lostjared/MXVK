#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_png.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

namespace {

    enum class StarType {
        NORMAL,
        BRIGHT,
        BLUE,
        ORANGE,
        RED,
        YELLOW
    };

    struct Particle {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float vz = 0.0f;
        float life = 0.0f;
        float twinkle = 0.0f;
        float twinkle_phase = 0.0f;
        StarType type = StarType::NORMAL;
        int layer = 0;
        int texture_index = 0;
        float pulse_speed = 0.0f;
        float base_size = 0.0f;
    };

    struct LayerConfig {
        float z_min = 0.0f;
        float z_max = 0.0f;
        float speed_multiplier = 0.0f;
        float size_multiplier = 0.0f;
        int count = 0;
    };

    struct StarVertex {
        float position[3]{};
        float size = 0.0f;
        float color[4]{};
    };

    struct UniformBufferObject {
        alignas(16) glm::mat4 mvp{1.0f};
    };

    struct TextureResource {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    constexpr int NUM_PARTICLES = 50000;
    constexpr int NUM_LAYERS = 3;
    constexpr float PI = 3.14159265358979323846f;

    constexpr std::array<LayerConfig, NUM_LAYERS> LAYERS{{
        {-12.0f, -6.0f, 0.2f, 0.6f, 28000},
        {-6.0f, -3.0f, 0.5f, 1.0f, 14000},
        {-3.0f, -1.0f, 1.0f, 1.6f, 8000},
    }};

    [[nodiscard]] float random_float(float min, float max) {
        static std::random_device rd;
        static std::default_random_engine engine(rd());
        std::uniform_real_distribution<float> dist(min, max);
        return dist(engine);
    }

    [[nodiscard]] StarType random_star_type() {
        const float r = random_float(0.0f, 1.0f);
        if (r < 0.50f) {
            return StarType::NORMAL;
        }
        if (r < 0.65f) {
            return StarType::BLUE;
        }
        if (r < 0.75f) {
            return StarType::YELLOW;
        }
        if (r < 0.85f) {
            return StarType::ORANGE;
        }
        if (r < 0.92f) {
            return StarType::RED;
        }
        return StarType::BRIGHT;
    }

    [[nodiscard]] glm::vec4 star_color(StarType type, float brightness) {
        switch (type) {
        case StarType::BLUE:
            return glm::vec4(0.6f * brightness, 0.8f * brightness, 1.0f * brightness, 1.0f);
        case StarType::ORANGE:
            return glm::vec4(1.0f * brightness, 0.7f * brightness, 0.3f * brightness, 1.0f);
        case StarType::RED:
            return glm::vec4(1.0f * brightness, 0.4f * brightness, 0.4f * brightness, 1.0f);
        case StarType::YELLOW:
            return glm::vec4(1.0f * brightness, 1.0f * brightness, 0.6f * brightness, 1.0f);
        case StarType::BRIGHT:
            return glm::vec4(1.0f * brightness, 1.0f * brightness, 1.0f * brightness, 1.0f);
        case StarType::NORMAL:
        default:
            return glm::vec4(0.9f * brightness, 0.9f * brightness, 1.0f * brightness, 1.0f);
        }
    }

} // namespace

namespace example {

    class StarfieldWindow : public mxvk::VK_Window {
      public:
        StarfieldWindow(const std::string &data_root, int width, int height, bool fullscreen, bool enable_vsync)
            : mxvk::VK_Window("MXVK Starfield", width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              data_root(data_root),
              particles(NUM_PARTICLES) {
            setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            initParticles();
            last_update_time = SDL_GetTicks();
        }

        ~StarfieldWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            cleanupSwapchainResources();
            destroyVertexBuffer();
            destroyTexture(star_texture2);
            destroyTexture(star_texture);
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            }
        }

        void onSwapchainAboutToRecreate() override {
            cleanupSwapchainResources();
        }

        void onSwapchainRecreated() override {
            if (gpu_resources_created) {
                createSwapchainResources();
            }
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t image_index) override {
            if (!ensureStarfieldResources()) {
                return;
            }

            if (pipeline == VK_NULL_HANDLE || image_index >= descriptor_sets.size()) {
                return;
            }

            const Uint32 current_time = SDL_GetTicks();
            float delta_time = static_cast<float>(current_time - last_update_time) / 1000.0f;
            last_update_time = current_time;
            if (delta_time > 0.1f) {
                delta_time = 0.1f;
            }
            global_time += delta_time;

            updateParticles(delta_time);
            updateUniformBuffer(image_index);

            const VkExtent2D extent = getSwapchainExtent();
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

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            VkBuffer vertex_buffers[] = {vertex_buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline_layout,
                0,
                1,
                &descriptor_sets[image_index],
                0,
                nullptr);

            vkCmdDraw(cmd, NUM_PARTICLES, 1, 0, 0);
        }

      private:
        bool ensureStarfieldResources() {
            if (gpu_resources_created) {
                return true;
            }
            if (device == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE || getSwapchainImageCount() == 0U || getSwapchainFormat() == VK_FORMAT_UNDEFINED) {
                return false;
            }

            createTexture(data_root + "/star.png", star_texture);
            createTexture(data_root + "/star2.png", star_texture2);
            createVertexBuffer();
            createSwapchainResources();
            last_update_time = SDL_GetTicks();
            gpu_resources_created = true;
            return true;
        }

        void initParticles() {
            int particle_index = 0;
            for (int layer = 0; layer < NUM_LAYERS; ++layer) {
                for (int i = 0; i < LAYERS[layer].count && particle_index < NUM_PARTICLES; ++i, ++particle_index) {
                    initParticle(particles[static_cast<size_t>(particle_index)], layer);
                    particles[static_cast<size_t>(particle_index)].z = random_float(LAYERS[layer].z_min, 0.0f);
                }
            }
        }

        void initParticle(Particle &particle, int layer) {
            const auto &cfg = LAYERS[static_cast<size_t>(layer)];
            particle.layer = layer;
            particle.x = random_float(-4.0f, 4.0f);
            particle.y = random_float(-4.0f, 4.0f);
            particle.z = random_float(cfg.z_min, cfg.z_max);
            particle.vx = random_float(-0.01f, 0.01f) * cfg.speed_multiplier;
            particle.vy = random_float(-0.01f, 0.01f) * cfg.speed_multiplier;
            particle.vz = random_float(0.15f, 0.35f) * cfg.speed_multiplier;
            particle.life = random_float(0.7f, 1.0f);
            particle.twinkle = random_float(2.0f, 8.0f);
            particle.twinkle_phase = random_float(0.0f, 6.28f);
            particle.type = random_star_type();
            particle.texture_index = random_float(0.0f, 1.0f) > 0.5f ? 1 : 0;
            particle.pulse_speed = random_float(0.5f, 3.0f);

            const float type_multiplier = particle.type == StarType::BRIGHT ? 2.0f : 1.0f;
            particle.base_size = random_float(12.0f, 28.0f) * cfg.size_multiplier * type_multiplier;
        }

        void updateParticles(float delta_time) {
            if (vertex_mapped == nullptr) {
                return;
            }

            auto *vertices = static_cast<StarVertex *>(vertex_mapped);
            for (int i = 0; i < NUM_PARTICLES; ++i) {
                auto &particle = particles[static_cast<size_t>(i)];
                particle.x += particle.vx * delta_time;
                particle.y += particle.vy * delta_time;
                particle.z += particle.vz * delta_time;

                if (particle.z > 0.0f) {
                    initParticle(particle, particle.layer);
                }

                if (particle.x > 4.0f) {
                    particle.x = -4.0f;
                }
                if (particle.x < -4.0f) {
                    particle.x = 4.0f;
                }
                if (particle.y > 4.0f) {
                    particle.y = -4.0f;
                }
                if (particle.y < -4.0f) {
                    particle.y = 4.0f;
                }

                const float twinkle1 = 0.5f * (1.0f + std::sin(global_time * particle.twinkle + particle.twinkle_phase));
                const float twinkle2 = 0.3f * (1.0f + std::sin(global_time * particle.pulse_speed * 2.0f + particle.twinkle_phase * 1.5f));
                const float twinkle_factor = 0.5f + 0.3f * twinkle1 + 0.2f * twinkle2;

                const auto &cfg = LAYERS[static_cast<size_t>(particle.layer)];
                float depth_factor = 1.0f - (particle.z / cfg.z_min);
                depth_factor = glm::clamp(depth_factor, 0.3f, 1.0f);

                const float brightness = particle.life * twinkle_factor * depth_factor;
                const glm::vec4 color = star_color(particle.type, brightness);
                const float size_pulse = 1.0f + 0.2f * std::sin(global_time * particle.pulse_speed + particle.twinkle_phase);
                const float size = particle.base_size * size_pulse * depth_factor;
                const float alpha = particle.life * glm::clamp(depth_factor + 0.2f, 0.0f, 1.0f);

                vertices[i].position[0] = particle.x;
                vertices[i].position[1] = particle.y;
                vertices[i].position[2] = particle.z;
                vertices[i].size = size;
                vertices[i].color[0] = color.r;
                vertices[i].color[1] = color.g;
                vertices[i].color[2] = color.b;
                vertices[i].color[3] = alpha;
            }
        }

        void updateUniformBuffer(uint32_t image_index) {
            if (image_index >= uniform_mapped.size() || uniform_mapped[image_index] == nullptr) {
                return;
            }

            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = extent.height > 0U ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
            glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            projection[1][1] *= -1.0f;

            const glm::vec3 camera_pos(
                camera_zoom * std::sin(glm::radians(camera_rotation)),
                0.0f,
                camera_zoom * std::cos(glm::radians(camera_rotation)));
            const glm::mat4 view = glm::lookAt(camera_pos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

            UniformBufferObject ubo{};
            ubo.mvp = projection * view;
            std::memcpy(uniform_mapped[image_index], &ubo, sizeof(ubo));
        }

        void createSwapchainResources() {
            if (device == VK_NULL_HANDLE || getSwapchainImageCount() == 0U) {
                return;
            }

            createDescriptorSetLayout();
            createUniformBuffers();
            createDescriptorPool();
            createDescriptorSets();
            createPipeline();
        }

        void cleanupSwapchainResources() {
            if (device == VK_NULL_HANDLE) {
                return;
            }

            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
            if (pipeline_layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
                pipeline_layout = VK_NULL_HANDLE;
            }
            if (descriptor_pool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
                descriptor_pool = VK_NULL_HANDLE;
            }
            if (descriptor_set_layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
                descriptor_set_layout = VK_NULL_HANDLE;
            }
            destroyUniformBuffers();
            descriptor_sets.clear();
        }

        void createDescriptorSetLayout() {
            VkDescriptorSetLayoutBinding sampler_layout_binding{};
            sampler_layout_binding.binding = 0;
            sampler_layout_binding.descriptorCount = 1;
            sampler_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            sampler_layout_binding.pImmutableSamplers = nullptr;
            sampler_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding ubo_layout_binding{};
            ubo_layout_binding.binding = 1;
            ubo_layout_binding.descriptorCount = 1;
            ubo_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            ubo_layout_binding.pImmutableSamplers = nullptr;
            ubo_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

            std::array<VkDescriptorSetLayoutBinding, 2> bindings = {sampler_layout_binding, ubo_layout_binding};
            VkDescriptorSetLayoutCreateInfo layout_info{};
            layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
            layout_info.pBindings = bindings.data();

            if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_set_layout) != VK_SUCCESS) {
                throw mxvk::Exception("starfield: failed to create descriptor set layout");
            }
        }

        void createUniformBuffers() {
            const size_t image_count = getSwapchainImageCount();
            uniform_buffers.resize(image_count, VK_NULL_HANDLE);
            uniform_memories.resize(image_count, VK_NULL_HANDLE);
            uniform_mapped.resize(image_count, nullptr);

            for (size_t i = 0; i < image_count; ++i) {
                createBuffer(
                    sizeof(UniformBufferObject),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    uniform_buffers[i],
                    uniform_memories[i]);
                if (vkMapMemory(device, uniform_memories[i], 0, sizeof(UniformBufferObject), 0, &uniform_mapped[i]) != VK_SUCCESS) {
                    throw mxvk::Exception("starfield: failed to map uniform buffer");
                }
            }
        }

        void destroyUniformBuffers() {
            for (size_t i = 0; i < uniform_buffers.size(); ++i) {
                if (i < uniform_mapped.size() && uniform_mapped[i] != nullptr && i < uniform_memories.size() && uniform_memories[i] != VK_NULL_HANDLE) {
                    vkUnmapMemory(device, uniform_memories[i]);
                }
                if (uniform_buffers[i] != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, uniform_buffers[i], nullptr);
                }
                if (i < uniform_memories.size() && uniform_memories[i] != VK_NULL_HANDLE) {
                    vkFreeMemory(device, uniform_memories[i], nullptr);
                }
            }
            uniform_buffers.clear();
            uniform_memories.clear();
            uniform_mapped.clear();
        }

        void createDescriptorPool() {
            const uint32_t image_count = static_cast<uint32_t>(getSwapchainImageCount());
            std::array<VkDescriptorPoolSize, 2> pool_sizes{};
            pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            pool_sizes[0].descriptorCount = image_count;
            pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            pool_sizes[1].descriptorCount = image_count;

            VkDescriptorPoolCreateInfo pool_info{};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
            pool_info.pPoolSizes = pool_sizes.data();
            pool_info.maxSets = image_count;

            if (vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
                throw mxvk::Exception("starfield: failed to create descriptor pool");
            }
        }

        void createDescriptorSets() {
            const size_t image_count = getSwapchainImageCount();
            std::vector<VkDescriptorSetLayout> layouts(image_count, descriptor_set_layout);

            VkDescriptorSetAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc_info.descriptorPool = descriptor_pool;
            alloc_info.descriptorSetCount = static_cast<uint32_t>(image_count);
            alloc_info.pSetLayouts = layouts.data();

            descriptor_sets.resize(image_count, VK_NULL_HANDLE);
            if (vkAllocateDescriptorSets(device, &alloc_info, descriptor_sets.data()) != VK_SUCCESS) {
                throw mxvk::Exception("starfield: failed to allocate descriptor sets");
            }

            for (size_t i = 0; i < image_count; ++i) {
                VkDescriptorImageInfo image_info{};
                image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                image_info.imageView = star_texture.view;
                image_info.sampler = star_texture.sampler;

                VkDescriptorBufferInfo buffer_info{};
                buffer_info.buffer = uniform_buffers[i];
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

                vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            }
        }

        void createPipeline() {
            const std::vector<char> vert_shader_code = loadSpv(data_root + "/starfield.vert.spv");
            const std::vector<char> frag_shader_code = loadSpv(data_root + "/starfield.frag.spv");
            const VkShaderModule vert_shader_module = createShaderModule(device, vert_shader_code);
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
                binding_description.stride = sizeof(StarVertex);
                binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                std::array<VkVertexInputAttributeDescription, 3> attributes{};
                attributes[0].binding = 0;
                attributes[0].location = 0;
                attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
                attributes[0].offset = offsetof(StarVertex, position);
                attributes[1].binding = 0;
                attributes[1].location = 1;
                attributes[1].format = VK_FORMAT_R32_SFLOAT;
                attributes[1].offset = offsetof(StarVertex, size);
                attributes[2].binding = 0;
                attributes[2].location = 2;
                attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                attributes[2].offset = offsetof(StarVertex, color);

                VkPipelineVertexInputStateCreateInfo vertex_input{};
                vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                vertex_input.vertexBindingDescriptionCount = 1;
                vertex_input.pVertexBindingDescriptions = &binding_description;
                vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
                vertex_input.pVertexAttributeDescriptions = attributes.data();

                VkPipelineInputAssemblyStateCreateInfo input_assembly{};
                input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                input_assembly.primitiveRestartEnable = VK_FALSE;

                VkPipelineViewportStateCreateInfo viewport_state{};
                viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewport_state.viewportCount = 1;
                viewport_state.scissorCount = 1;

                std::array<VkDynamicState, 2> dynamic_states = {
                    VK_DYNAMIC_STATE_VIEWPORT,
                    VK_DYNAMIC_STATE_SCISSOR,
                };
                VkPipelineDynamicStateCreateInfo dynamic_state{};
                dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
                dynamic_state.pDynamicStates = dynamic_states.data();

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
                depth_stencil.depthTestEnable = VK_FALSE;
                depth_stencil.depthWriteEnable = VK_FALSE;
                depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
                depth_stencil.depthBoundsTestEnable = VK_FALSE;
                depth_stencil.stencilTestEnable = VK_FALSE;

                VkPipelineColorBlendAttachmentState color_blend_attachment{};
                color_blend_attachment.colorWriteMask =
                    VK_COLOR_COMPONENT_R_BIT |
                    VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT |
                    VK_COLOR_COMPONENT_A_BIT;
                color_blend_attachment.blendEnable = VK_TRUE;
                color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
                color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

                VkPipelineColorBlendStateCreateInfo color_blending{};
                color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                color_blending.logicOpEnable = VK_FALSE;
                color_blending.attachmentCount = 1;
                color_blending.pAttachments = &color_blend_attachment;

                VkPipelineLayoutCreateInfo pipeline_layout_info{};
                pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                pipeline_layout_info.setLayoutCount = 1;
                pipeline_layout_info.pSetLayouts = &descriptor_set_layout;
                if (vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
                    throw mxvk::Exception("starfield: failed to create pipeline layout");
                }

                VkFormat color_format = getSwapchainFormat();
                VkFormat active_depth_format = getDepthFormat();
                VkPipelineRenderingCreateInfo rendering_info{};
                rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                rendering_info.colorAttachmentCount = 1;
                rendering_info.pColorAttachmentFormats = &color_format;
                rendering_info.depthAttachmentFormat = active_depth_format;
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
                pipeline_info.subpass = 0;
                pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
                pipeline_info.basePipelineIndex = -1;

                if (vkCreateGraphicsPipelines(device, pipeline_cache, 1, &pipeline_info, nullptr, &pipeline) != VK_SUCCESS) {
                    throw mxvk::Exception("starfield: failed to create graphics pipeline");
                }
            } catch (...) {
                if (pipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device, pipeline, nullptr);
                    pipeline = VK_NULL_HANDLE;
                }
                if (pipeline_layout != VK_NULL_HANDLE) {
                    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
                    pipeline_layout = VK_NULL_HANDLE;
                }
                if (frag_shader_module != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(device, frag_shader_module, nullptr);
                }
                vkDestroyShaderModule(device, vert_shader_module, nullptr);
                throw;
            }

            vkDestroyShaderModule(device, frag_shader_module, nullptr);
            vkDestroyShaderModule(device, vert_shader_module, nullptr);
        }

        void createVertexBuffer() {
            const VkDeviceSize buffer_size = sizeof(StarVertex) * static_cast<VkDeviceSize>(NUM_PARTICLES);
            createBuffer(
                buffer_size,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                vertex_buffer,
                vertex_memory);

            if (vkMapMemory(device, vertex_memory, 0, buffer_size, 0, &vertex_mapped) != VK_SUCCESS) {
                throw mxvk::Exception("starfield: failed to map vertex buffer");
            }
            updateParticles(0.0f);
        }

        void destroyVertexBuffer() {
            if (vertex_mapped != nullptr && vertex_memory != VK_NULL_HANDLE) {
                vkUnmapMemory(device, vertex_memory);
                vertex_mapped = nullptr;
            }
            if (vertex_buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, vertex_buffer, nullptr);
                vertex_buffer = VK_NULL_HANDLE;
            }
            if (vertex_memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, vertex_memory, nullptr);
                vertex_memory = VK_NULL_HANDLE;
            }
        }

        void createTexture(const std::string &path, TextureResource &texture) {
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(mxvk::LoadPNG(path.c_str()), SDL_DestroySurface);
            if (surface == nullptr || surface->pixels == nullptr || surface->w <= 0 || surface->h <= 0) {
                throw mxvk::Exception(std::format("starfield: failed to load texture {}", path));
            }

            const uint32_t width = static_cast<uint32_t>(surface->w);
            const uint32_t height = static_cast<uint32_t>(surface->h);
            const VkDeviceSize image_size = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4U;
            std::vector<std::byte> tight_pixels(static_cast<size_t>(image_size));

            const auto *src = static_cast<const std::byte *>(surface->pixels);
            auto *dst = tight_pixels.data();
            const size_t tight_row_bytes = static_cast<size_t>(width) * 4U;
            for (uint32_t y = 0; y < height; ++y) {
                std::memcpy(dst + y * tight_row_bytes, src + static_cast<size_t>(y) * static_cast<size_t>(surface->pitch), tight_row_bytes);
            }

            VkBuffer staging_buffer = VK_NULL_HANDLE;
            VkDeviceMemory staging_memory = VK_NULL_HANDLE;
            createBuffer(
                image_size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                staging_buffer,
                staging_memory);

            void *data = nullptr;
            if (vkMapMemory(device, staging_memory, 0, image_size, 0, &data) != VK_SUCCESS || data == nullptr) {
                vkDestroyBuffer(device, staging_buffer, nullptr);
                vkFreeMemory(device, staging_memory, nullptr);
                throw mxvk::Exception("starfield: failed to map texture staging buffer");
            }
            std::memcpy(data, tight_pixels.data(), tight_pixels.size());
            vkUnmapMemory(device, staging_memory);

            createImage(
                width,
                height,
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                texture.image,
                texture.memory);

            transitionImageLayout(texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            copyBufferToImage(staging_buffer, texture.image, width, height);
            transitionImageLayout(texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            vkDestroyBuffer(device, staging_buffer, nullptr);
            vkFreeMemory(device, staging_memory, nullptr);

            texture.view = createImageView(texture.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

            VkSamplerCreateInfo sampler_info{};
            sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sampler_info.magFilter = VK_FILTER_LINEAR;
            sampler_info.minFilter = VK_FILTER_LINEAR;
            sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_info.anisotropyEnable = VK_FALSE;
            sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            sampler_info.unnormalizedCoordinates = VK_FALSE;
            sampler_info.compareEnable = VK_FALSE;
            sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
            sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            if (vkCreateSampler(device, &sampler_info, nullptr, &texture.sampler) != VK_SUCCESS) {
                throw mxvk::Exception("starfield: failed to create texture sampler");
            }
        }

        void destroyTexture(TextureResource &texture) {
            if (device == VK_NULL_HANDLE) {
                texture = {};
                return;
            }
            if (texture.sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, texture.sampler, nullptr);
                texture.sampler = VK_NULL_HANDLE;
            }
            if (texture.view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, texture.view, nullptr);
                texture.view = VK_NULL_HANDLE;
            }
            if (texture.image != VK_NULL_HANDLE) {
                vkDestroyImage(device, texture.image, nullptr);
                texture.image = VK_NULL_HANDLE;
            }
            if (texture.memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, texture.memory, nullptr);
                texture.memory = VK_NULL_HANDLE;
            }
        }

        void createBuffer(VkDeviceSize size,
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
                throw mxvk::Exception("starfield: failed to create buffer");
            }

            VkMemoryRequirements mem_requirements{};
            vkGetBufferMemoryRequirements(device, buffer, &mem_requirements);

            VkMemoryAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc_info.allocationSize = mem_requirements.size;
            alloc_info.memoryTypeIndex = findMemoryType(mem_requirements.memoryTypeBits, properties);

            if (vkAllocateMemory(device, &alloc_info, nullptr, &buffer_memory) != VK_SUCCESS) {
                vkDestroyBuffer(device, buffer, nullptr);
                buffer = VK_NULL_HANDLE;
                throw mxvk::Exception("starfield: failed to allocate buffer memory");
            }
            if (vkBindBufferMemory(device, buffer, buffer_memory, 0) != VK_SUCCESS) {
                vkFreeMemory(device, buffer_memory, nullptr);
                vkDestroyBuffer(device, buffer, nullptr);
                buffer = VK_NULL_HANDLE;
                buffer_memory = VK_NULL_HANDLE;
                throw mxvk::Exception("starfield: failed to bind buffer memory");
            }
        }

        [[nodiscard]] uint32_t findMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
            VkPhysicalDeviceMemoryProperties mem_properties{};
            vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

            for (uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
                if ((type_filter & (1U << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }

            throw mxvk::Exception("starfield: failed to find suitable memory type");
        }

        [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const {
            VkCommandBufferAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc_info.commandPool = command_pool;
            alloc_info.commandBufferCount = 1;

            VkCommandBuffer command_buffer = VK_NULL_HANDLE;
            if (vkAllocateCommandBuffers(device, &alloc_info, &command_buffer) != VK_SUCCESS) {
                throw mxvk::Exception("starfield: failed to allocate command buffer");
            }

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
                vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
                throw mxvk::Exception("starfield: failed to begin command buffer");
            }

            return command_buffer;
        }

        void endSingleTimeCommands(VkCommandBuffer command_buffer) const {
            if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
                vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
                throw mxvk::Exception("starfield: failed to end command buffer");
            }

            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &command_buffer;

            if (vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS) {
                vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
                throw mxvk::Exception("starfield: failed to submit command buffer");
            }
            if (vkQueueWaitIdle(graphics_queue) != VK_SUCCESS) {
                vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
                throw mxvk::Exception("starfield: failed to wait for graphics queue idle");
            }

            vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
        }

        void createImage(uint32_t width,
                         uint32_t height,
                         VkFormat format,
                         VkImageTiling tiling,
                         VkImageUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         VkImage &image,
                         VkDeviceMemory &memory) const {
            VkImageCreateInfo image_info{};
            image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image_info.imageType = VK_IMAGE_TYPE_2D;
            image_info.extent.width = width;
            image_info.extent.height = height;
            image_info.extent.depth = 1;
            image_info.mipLevels = 1;
            image_info.arrayLayers = 1;
            image_info.format = format;
            image_info.tiling = tiling;
            image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_info.usage = usage;
            image_info.samples = VK_SAMPLE_COUNT_1_BIT;
            image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateImage(device, &image_info, nullptr, &image) != VK_SUCCESS) {
                throw mxvk::Exception("starfield: failed to create image");
            }

            VkMemoryRequirements mem_requirements{};
            vkGetImageMemoryRequirements(device, image, &mem_requirements);

            VkMemoryAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc_info.allocationSize = mem_requirements.size;
            alloc_info.memoryTypeIndex = findMemoryType(mem_requirements.memoryTypeBits, properties);

            if (vkAllocateMemory(device, &alloc_info, nullptr, &memory) != VK_SUCCESS) {
                vkDestroyImage(device, image, nullptr);
                image = VK_NULL_HANDLE;
                throw mxvk::Exception("starfield: failed to allocate image memory");
            }
            if (vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
                vkFreeMemory(device, memory, nullptr);
                vkDestroyImage(device, image, nullptr);
                image = VK_NULL_HANDLE;
                memory = VK_NULL_HANDLE;
                throw mxvk::Exception("starfield: failed to bind image memory");
            }
        }

        [[nodiscard]] VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect) const {
            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = image;
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = format;
            view_info.subresourceRange.aspectMask = aspect;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            VkImageView image_view = VK_NULL_HANDLE;
            if (vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS) {
                throw mxvk::Exception("starfield: failed to create image view");
            }
            return image_view;
        }

        void transitionImageLayout(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout) const {
            VkCommandBuffer command_buffer = beginSingleTimeCommands();

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = old_layout;
            barrier.newLayout = new_layout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            VkPipelineStageFlags source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

            if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            } else {
                vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
                throw mxvk::Exception("starfield: unsupported image layout transition");
            }

            vkCmdPipelineBarrier(
                command_buffer,
                source_stage,
                destination_stage,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);

            endSingleTimeCommands(command_buffer);
        }

        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const {
            VkCommandBuffer command_buffer = beginSingleTimeCommands();

            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {width, height, 1};

            vkCmdCopyBufferToImage(command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            endSingleTimeCommands(command_buffer);
        }

        std::string data_root{};
        std::vector<Particle> particles{};
        float camera_zoom = 0.09f;
        float camera_rotation = 356.0f;
        float global_time = 0.0f;
        Uint32 last_update_time = 0;
        bool gpu_resources_created = false;

        TextureResource star_texture{};
        TextureResource star_texture2{};

        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
        void *vertex_mapped = nullptr;

        VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptor_sets{};
        std::vector<VkBuffer> uniform_buffers{};
        std::vector<VkDeviceMemory> uniform_memories{};
        std::vector<void *> uniform_mapped{};
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        const std::string root = args.path.empty() ? std::string(STARFIELD_ASSET_DIR) : args.path;
        example::StarfieldWindow window(root + "/data", args.width, args.height, args.fullscreen, args.enable_vsync);
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
