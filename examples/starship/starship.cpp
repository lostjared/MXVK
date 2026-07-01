#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_png.hpp"

namespace {

    struct StarUniformBufferObject {
        alignas(16) glm::mat4 model{1.0f};
        alignas(16) glm::mat4 view{1.0f};
        alignas(16) glm::mat4 proj{1.0f};
        alignas(16) glm::vec4 params{0.0f};
        alignas(16) glm::vec4 color{1.0f};
    };

    struct StarVertex {
        float pos[3];
        float size;
        float color[4];
    };

    struct Star {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float vz = 0.0f;
        float magnitude = 0.0f;
        float temperature = 0.0f;
        float twinkle = 0.0f;
        float size = 0.0f;
        int starType = 0;
        bool isConstellation = false;
    };

    struct FlameVertex {
        glm::vec3 pos{};
        glm::vec4 color{};
    };

    struct FlamePushConstants {
        glm::mat4 mvp{1.0f};
        glm::vec4 params{0.0f};
    };

    constexpr float PI = 3.14159265358979323846f;

} // namespace

namespace example {

    class StarshipWindow : public mxvk::VK_Window {
      public:
        StarshipWindow(const std::string filename, const std::string &title, int width, int height, bool fullscreen, bool enable_vsync)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION, enable_vsync) {
            const std::string modelVertPath = std::string(STARSHIP_EXAMPLE_SHADER_DIR) + "/model.vert.spv";
            const std::string modelFragPath = std::string(STARSHIP_EXAMPLE_SHADER_DIR) + "/model.frag.spv";

            model.load(this, filename, "", "", 1.0f);
            model.setShaders(this, modelVertPath, modelFragPath);

            initStarfield(12000);
            createStarResources();
            createFlameResources();
        }

        ~StarshipWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            cleanupFlameResources();
            cleanupStarResources();
            model.cleanup(this);
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                mouseDragging = true;
                lastMouseX = static_cast<int>(e.button.x);
                lastMouseY = static_cast<int>(e.button.y);
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                mouseDragging = false;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_MOTION && mouseDragging) {
                const int x = static_cast<int>(e.motion.x);
                const int y = static_cast<int>(e.motion.y);
                const int deltaX = x - lastMouseX;
                const int deltaY = y - lastMouseY;

                mouseYawDegrees += static_cast<float>(deltaX) * mouseSensitivity;
                mousePitchDegrees += static_cast<float>(deltaY) * mouseSensitivity;
                mousePitchDegrees = std::clamp(mousePitchDegrees, -80.0f, 80.0f);

                lastMouseX = x;
                lastMouseY = y;
                return;
            }
        }

        void onSwapchainRecreated() override {
            model.resize(this);
            cleanupStarSwapchainResources();
            cleanupFlameSwapchainResources();
            createStarSwapchainResources();
            createFlameSwapchainResources();
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const float elapsedSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            drawStarfield(cmd, imageIndex, extent, elapsedSeconds);

            mxvk::UniformBufferObject ubo{};
            ubo.model = glm::mat4(1.0f);
            ubo.model = glm::rotate(ubo.model, glm::radians(mousePitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model,
                                    elapsedSeconds * autoSpinSpeed + glm::radians(mouseYawDegrees),
                                    glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, glm::vec3(model.modelRenderScale()));
            ubo.model = glm::translate(ubo.model, model.modelCenterOffset());
            ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 4.2f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;

            model.updateUBO(imageIndex, ubo);
            model.render(cmd, imageIndex, false);
            drawEngineFlame(cmd, extent, elapsedSeconds, ubo.model, ubo.view, ubo.proj);
        }

      private:
        void initStarfield(int numStarsParam) {
            if (starfieldInitialized) {
                return;
            }

            numStars = numStarsParam;
            stars.resize(static_cast<std::size_t>(numStars));

            for (int i = 0; i < numStars; ++i) {
                respawnStar(stars[static_cast<std::size_t>(i)]);
            }

            starfieldInitialized = true;
        }

        void respawnStar(Star &star) {
            const float theta = randomFloat(0.0f, 2.0f * PI);
            const float phi = std::acos(randomFloat(-1.0f, 1.0f));
            const float radius = randomFloat(50.0f, 200.0f);

            star.x = radius * std::sin(phi) * std::cos(theta);
            star.y = radius * std::sin(phi) * std::sin(theta);
            star.z = radius * std::cos(phi);

            star.vx = randomFloat(-0.060f, 0.060f);
            star.vy = randomFloat(-0.060f, 0.060f);
            star.vz = randomFloat(-0.060f, 0.060f);

            const float r = randomFloat(0.0f, 1.0f);
            if (r < 0.05f) {
                star.magnitude = randomFloat(-1.0f, 2.0f);
                star.starType = 1;
            } else if (r < 0.3f) {
                star.magnitude = randomFloat(2.0f, 4.0f);
                star.starType = 0;
            } else {
                star.magnitude = randomFloat(4.0f, 6.5f);
                star.starType = 2;
            }

            if (star.starType == 1) {
                star.temperature = randomFloat(3000.0f, 5000.0f);
            } else if (star.starType == 0) {
                star.temperature = randomFloat(4000.0f, 8000.0f);
            } else {
                star.temperature = randomFloat(2500.0f, 4000.0f);
            }

            star.twinkle = randomFloat(0.5f, 3.0f);
            star.size = magnitudeToSize(star.magnitude);
            star.isConstellation = (star.magnitude < 3.0f) && (randomFloat(0.0f, 1.0f) < 0.3f);
        }

        void createStarResources() {
            createStarTexture();
            createStarVertexBuffer();
            createStarSwapchainResources();
        }

        void cleanupStarResources() {
            cleanupStarSwapchainResources();

            if (starVertexBufferMapped != nullptr && starVertexBufferMemory != VK_NULL_HANDLE) {
                vkUnmapMemory(device, starVertexBufferMemory);
                starVertexBufferMapped = nullptr;
            }
            if (starVertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, starVertexBuffer, nullptr);
                starVertexBuffer = VK_NULL_HANDLE;
            }
            if (starVertexBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, starVertexBufferMemory, nullptr);
                starVertexBufferMemory = VK_NULL_HANDLE;
            }

            if (starSampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, starSampler, nullptr);
                starSampler = VK_NULL_HANDLE;
            }
            if (starTextureView != VK_NULL_HANDLE) {
                vkDestroyImageView(device, starTextureView, nullptr);
                starTextureView = VK_NULL_HANDLE;
            }
            if (starTexture != VK_NULL_HANDLE) {
                vkDestroyImage(device, starTexture, nullptr);
                starTexture = VK_NULL_HANDLE;
            }
            if (starTextureMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, starTextureMemory, nullptr);
                starTextureMemory = VK_NULL_HANDLE;
            }
        }

        void cleanupStarSwapchainResources() {
            if (starPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, starPipeline, nullptr);
                starPipeline = VK_NULL_HANDLE;
            }
            if (starPipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, starPipelineLayout, nullptr);
                starPipelineLayout = VK_NULL_HANDLE;
            }
            if (starDescriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, starDescriptorPool, nullptr);
                starDescriptorPool = VK_NULL_HANDLE;
            }
            if (starDescriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, starDescriptorSetLayout, nullptr);
                starDescriptorSetLayout = VK_NULL_HANDLE;
            }

            destroyStarUniformBuffers();
            starDescriptorSets.clear();
        }

        void createStarSwapchainResources() {
            if (!starfieldInitialized || device == VK_NULL_HANDLE) {
                return;
            }

            createStarDescriptorSetLayout();
            createStarUniformBuffers();
            createStarDescriptorPool();
            createStarDescriptorSets();
            createStarPipeline();
        }

        void createStarTexture() {
            const std::string starTexturePath = std::string(STARSHIP_EXAMPLE_RUNTIME_DATA_DIR) + "/star.png";
            SDL_Surface *starImg = mxvk::LoadPNG(starTexturePath.c_str());
            if (starImg == nullptr) {
                throw mxvk::Exception("Failed to load star.png texture");
            }

            const VkDeviceSize imageSize = static_cast<VkDeviceSize>(starImg->w) * static_cast<VkDeviceSize>(starImg->h) * 4U;

            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
            createBuffer(
                imageSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer,
                stagingBufferMemory);

            void *data = nullptr;
            if (vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data) != VK_SUCCESS || data == nullptr) {
                vkDestroyBuffer(device, stagingBuffer, nullptr);
                vkFreeMemory(device, stagingBufferMemory, nullptr);
                SDL_DestroySurface(starImg);
                throw mxvk::Exception("Failed to map star texture staging buffer");
            }
            std::memcpy(data, starImg->pixels, static_cast<std::size_t>(imageSize));
            vkUnmapMemory(device, stagingBufferMemory);

            createImage(
                static_cast<uint32_t>(starImg->w),
                static_cast<uint32_t>(starImg->h),
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                starTexture,
                starTextureMemory);

            transitionImageLayout(starTexture, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            copyBufferToImage(stagingBuffer, starTexture, static_cast<uint32_t>(starImg->w), static_cast<uint32_t>(starImg->h));
            transitionImageLayout(starTexture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingBufferMemory, nullptr);

            starTextureView = createImageView(starTexture, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.anisotropyEnable = VK_FALSE;
            samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            samplerInfo.unnormalizedCoordinates = VK_FALSE;
            samplerInfo.compareEnable = VK_FALSE;
            samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            if (vkCreateSampler(device, &samplerInfo, nullptr, &starSampler) != VK_SUCCESS) {
                SDL_DestroySurface(starImg);
                throw mxvk::Exception("Failed to create star texture sampler");
            }

            SDL_DestroySurface(starImg);
        }

        void createStarVertexBuffer() {
            const VkDeviceSize bufferSize = sizeof(StarVertex) * static_cast<VkDeviceSize>(numStars);
            createBuffer(
                bufferSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                starVertexBuffer,
                starVertexBufferMemory);

            if (vkMapMemory(device, starVertexBufferMemory, 0, bufferSize, 0, &starVertexBufferMapped) != VK_SUCCESS || starVertexBufferMapped == nullptr) {
                throw mxvk::Exception("Failed to map star vertex buffer");
            }
        }

        void createStarDescriptorSetLayout() {
            VkDescriptorSetLayoutBinding samplerBinding{};
            samplerBinding.binding = 0;
            samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            samplerBinding.descriptorCount = 1;
            samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding uboBinding{};
            uboBinding.binding = 1;
            uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uboBinding.descriptorCount = 1;
            uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

            std::array<VkDescriptorSetLayoutBinding, 2> bindings = {samplerBinding, uboBinding};
            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &starDescriptorSetLayout) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create star descriptor set layout");
            }
        }

        void createStarUniformBuffers() {
            const size_t imageCount = getSwapchainImageCount();
            const VkDeviceSize bufferSize = sizeof(StarUniformBufferObject);

            starUniformBuffers.resize(imageCount, VK_NULL_HANDLE);
            starUniformBufferMemories.resize(imageCount, VK_NULL_HANDLE);
            starUniformBufferMapped.resize(imageCount, nullptr);

            for (size_t i = 0; i < imageCount; ++i) {
                createBuffer(
                    bufferSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    starUniformBuffers[i],
                    starUniformBufferMemories[i]);

                if (vkMapMemory(device, starUniformBufferMemories[i], 0, bufferSize, 0, &starUniformBufferMapped[i]) != VK_SUCCESS || starUniformBufferMapped[i] == nullptr) {
                    throw mxvk::Exception("Failed to map star uniform buffer");
                }
            }
        }

        void destroyStarUniformBuffers() {
            for (size_t i = 0; i < starUniformBuffers.size(); ++i) {
                if (starUniformBufferMapped[i] != nullptr && starUniformBufferMemories[i] != VK_NULL_HANDLE) {
                    vkUnmapMemory(device, starUniformBufferMemories[i]);
                    starUniformBufferMapped[i] = nullptr;
                }
                if (starUniformBuffers[i] != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, starUniformBuffers[i], nullptr);
                    starUniformBuffers[i] = VK_NULL_HANDLE;
                }
                if (starUniformBufferMemories[i] != VK_NULL_HANDLE) {
                    vkFreeMemory(device, starUniformBufferMemories[i], nullptr);
                    starUniformBufferMemories[i] = VK_NULL_HANDLE;
                }
            }

            starUniformBuffers.clear();
            starUniformBufferMemories.clear();
            starUniformBufferMapped.clear();
        }

        void createStarDescriptorPool() {
            const uint32_t imageCount = static_cast<uint32_t>(getSwapchainImageCount());

            std::array<VkDescriptorPoolSize, 2> poolSizes{};
            poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            poolSizes[0].descriptorCount = imageCount;
            poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            poolSizes[1].descriptorCount = imageCount;

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            poolInfo.maxSets = imageCount;

            if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &starDescriptorPool) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create star descriptor pool");
            }
        }

        void createStarDescriptorSets() {
            const size_t imageCount = getSwapchainImageCount();
            std::vector<VkDescriptorSetLayout> layouts(imageCount, starDescriptorSetLayout);

            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = starDescriptorPool;
            allocInfo.descriptorSetCount = static_cast<uint32_t>(imageCount);
            allocInfo.pSetLayouts = layouts.data();

            starDescriptorSets.resize(imageCount, VK_NULL_HANDLE);
            if (vkAllocateDescriptorSets(device, &allocInfo, starDescriptorSets.data()) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to allocate star descriptor sets");
            }

            for (size_t i = 0; i < imageCount; ++i) {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = starTextureView;
                imageInfo.sampler = starSampler;

                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = starUniformBuffers[i];
                bufferInfo.offset = 0;
                bufferInfo.range = sizeof(StarUniformBufferObject);

                std::array<VkWriteDescriptorSet, 2> writes{};
                writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[0].dstSet = starDescriptorSets[i];
                writes[0].dstBinding = 0;
                writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[0].descriptorCount = 1;
                writes[0].pImageInfo = &imageInfo;

                writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[1].dstSet = starDescriptorSets[i];
                writes[1].dstBinding = 1;
                writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writes[1].descriptorCount = 1;
                writes[1].pBufferInfo = &bufferInfo;

                vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            }
        }

        void createStarPipeline() {
            const std::vector<char> vertShaderCode = loadSpv(std::string(STARSHIP_EXAMPLE_SHADER_DIR) + "/star.vert.spv");
            const std::vector<char> fragShaderCode = loadSpv(std::string(STARSHIP_EXAMPLE_SHADER_DIR) + "/star.frag.spv");

            VkShaderModule vertShaderModule = createShaderModule(device, vertShaderCode);
            VkShaderModule fragShaderModule = VK_NULL_HANDLE;

            try {
                fragShaderModule = createShaderModule(device, fragShaderCode);

                VkPipelineShaderStageCreateInfo vertStage{};
                vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
                vertStage.module = vertShaderModule;
                vertStage.pName = "main";

                VkPipelineShaderStageCreateInfo fragStage{};
                fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                fragStage.module = fragShaderModule;
                fragStage.pName = "main";

                std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertStage, fragStage};

                VkVertexInputBindingDescription bindingDescription{};
                bindingDescription.binding = 0;
                bindingDescription.stride = sizeof(StarVertex);
                bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                std::array<VkVertexInputAttributeDescription, 3> attributes{};
                attributes[0].binding = 0;
                attributes[0].location = 0;
                attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
                attributes[0].offset = offsetof(StarVertex, pos);

                attributes[1].binding = 0;
                attributes[1].location = 1;
                attributes[1].format = VK_FORMAT_R32_SFLOAT;
                attributes[1].offset = offsetof(StarVertex, size);

                attributes[2].binding = 0;
                attributes[2].location = 2;
                attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                attributes[2].offset = offsetof(StarVertex, color);

                VkPipelineVertexInputStateCreateInfo vertexInput{};
                vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                vertexInput.vertexBindingDescriptionCount = 1;
                vertexInput.pVertexBindingDescriptions = &bindingDescription;
                vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
                vertexInput.pVertexAttributeDescriptions = attributes.data();

                VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
                inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                inputAssembly.primitiveRestartEnable = VK_FALSE;

                VkPipelineViewportStateCreateInfo viewportState{};
                viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewportState.viewportCount = 1;
                viewportState.scissorCount = 1;

                std::array<VkDynamicState, 2> dynamicStates = {
                    VK_DYNAMIC_STATE_VIEWPORT,
                    VK_DYNAMIC_STATE_SCISSOR,
                };
                VkPipelineDynamicStateCreateInfo dynamicInfo{};
                dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamicInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
                dynamicInfo.pDynamicStates = dynamicStates.data();

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

                VkPipelineDepthStencilStateCreateInfo depthStencil{};
                depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depthStencil.depthTestEnable = VK_FALSE;
                depthStencil.depthWriteEnable = VK_FALSE;
                depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
                depthStencil.depthBoundsTestEnable = VK_FALSE;
                depthStencil.stencilTestEnable = VK_FALSE;

                VkPipelineColorBlendAttachmentState colorBlendAttachment{};
                colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                colorBlendAttachment.blendEnable = VK_TRUE;
                colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
                colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

                VkPipelineColorBlendStateCreateInfo colorBlending{};
                colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                colorBlending.logicOpEnable = VK_FALSE;
                colorBlending.attachmentCount = 1;
                colorBlending.pAttachments = &colorBlendAttachment;

                VkPipelineLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                layoutInfo.setLayoutCount = 1;
                layoutInfo.pSetLayouts = &starDescriptorSetLayout;

                if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &starPipelineLayout) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create star pipeline layout");
                }

                const VkFormat colorFormat = getSwapchainFormat();
                const VkFormat depthFormat = getDepthFormat();

                VkPipelineRenderingCreateInfo renderingInfo{};
                renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &colorFormat;
                if (depthFormat != VK_FORMAT_UNDEFINED) {
                    renderingInfo.depthAttachmentFormat = depthFormat;
                }
                renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

                VkGraphicsPipelineCreateInfo pipelineInfo{};
                pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
                pipelineInfo.pStages = shaderStages.data();
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicInfo;
                pipelineInfo.layout = starPipelineLayout;
                pipelineInfo.renderPass = VK_NULL_HANDLE;
                pipelineInfo.subpass = 0;

                if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &starPipeline) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create star pipeline");
                }
            } catch (...) {
                if (fragShaderModule != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(device, fragShaderModule, nullptr);
                }
                vkDestroyShaderModule(device, vertShaderModule, nullptr);
                if (starPipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device, starPipeline, nullptr);
                    starPipeline = VK_NULL_HANDLE;
                }
                if (starPipelineLayout != VK_NULL_HANDLE) {
                    vkDestroyPipelineLayout(device, starPipelineLayout, nullptr);
                    starPipelineLayout = VK_NULL_HANDLE;
                }
                throw;
            }

            vkDestroyShaderModule(device, fragShaderModule, nullptr);
            vkDestroyShaderModule(device, vertShaderModule, nullptr);
        }

        void updateStarfield(float deltaTime) {
            if (!starfieldInitialized || starVertexBufferMapped == nullptr) {
                return;
            }

            deltaTime = std::clamp(deltaTime, 0.0f, 0.1f) * 4.0f;
            const float time = SDL_GetTicks() * 0.001f;
            auto *vertices = static_cast<StarVertex *>(starVertexBufferMapped);

            for (int i = 0; i < numStars; ++i) {
                auto &star = stars[static_cast<std::size_t>(i)];

                star.x += star.vx * deltaTime;
                star.y += star.vy * deltaTime;
                star.z += star.vz * deltaTime;

                const float radiusSquared = star.x * star.x + star.y * star.y + star.z * star.z;
                if (radiusSquared < 20.0f * 20.0f || radiusSquared > 260.0f * 260.0f) {
                    respawnStar(star);
                }

                vertices[i].pos[0] = star.x;
                vertices[i].pos[1] = star.y;
                vertices[i].pos[2] = star.z;

                float twinkleFactor = 1.0f;
                if (atmosphericTwinkle > 0.0f) {
                    twinkleFactor = 0.7f + 0.3f * std::sin(time * star.twinkle) * atmosphericTwinkle;
                }

                float size = star.size * twinkleFactor;
                if (star.isConstellation) {
                    size *= 1.2f;
                }
                vertices[i].size = size;

                const glm::vec3 starColor = getStarColor(star.temperature);
                const float alpha = magnitudeToAlpha(star.magnitude) * twinkleFactor;

                vertices[i].color[0] = starColor.r;
                vertices[i].color[1] = starColor.g;
                vertices[i].color[2] = starColor.b;
                vertices[i].color[3] = alpha;
            }
        }

        void updateStarUniform(uint32_t imageIndex,
                               [[maybe_unused]] const VkExtent2D &extent,
                               const glm::mat4 &view,
                               const glm::mat4 &proj,
                               float timeSeconds) {
            if (imageIndex >= starUniformBufferMapped.size() || starUniformBufferMapped[imageIndex] == nullptr) {
                return;
            }

            StarUniformBufferObject ubo{};
            ubo.model = glm::mat4(1.0f);
            ubo.view = view;
            ubo.proj = proj;
            ubo.params = glm::vec4(timeSeconds, 0.0f, 0.0f, 0.0f);
            ubo.color = glm::vec4(1.0f);
            std::memcpy(starUniformBufferMapped[imageIndex], &ubo, sizeof(ubo));
        }

        void drawStarfield(VkCommandBuffer cmd, uint32_t imageIndex, const VkExtent2D &extent, float elapsedSeconds) {
            if (!starfieldInitialized || starPipeline == VK_NULL_HANDLE || imageIndex >= starDescriptorSets.size()) {
                return;
            }

            const Uint32 currentTime = SDL_GetTicks();
            const float deltaTime = static_cast<float>(currentTime - lastStarUpdateTime) / 1000.0f;
            lastStarUpdateTime = currentTime;
            updateStarfield(deltaTime);

            const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 4.2f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 proj = glm::perspective(
                glm::radians(60.0f),
                static_cast<float>(extent.width) / static_cast<float>(extent.height),
                0.1f,
                1000.0f);
            proj[1][1] *= -1.0f;

            updateStarUniform(imageIndex, extent, view, proj, elapsedSeconds);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, starPipeline);

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

            VkBuffer vertexBuffers[] = {starVertexBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                starPipelineLayout,
                0,
                1,
                &starDescriptorSets[imageIndex],
                0,
                nullptr);

            vkCmdDraw(cmd, static_cast<uint32_t>(numStars), 1, 0, 0);
        }

        void createFlameResources() {
            createFlameMesh();
            createFlameSwapchainResources();
        }

        void cleanupFlameResources() {
            cleanupFlameSwapchainResources();

            if (flameVertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, flameVertexBuffer, nullptr);
                flameVertexBuffer = VK_NULL_HANDLE;
            }
            if (flameVertexBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, flameVertexBufferMemory, nullptr);
                flameVertexBufferMemory = VK_NULL_HANDLE;
            }
        }

        void cleanupFlameSwapchainResources() {
            if (flamePipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, flamePipeline, nullptr);
                flamePipeline = VK_NULL_HANDLE;
            }
            if (flamePipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, flamePipelineLayout, nullptr);
                flamePipelineLayout = VK_NULL_HANDLE;
            }
        }

        void createFlameSwapchainResources() {
            if (flameVertexCount == 0 || device == VK_NULL_HANDLE) {
                return;
            }

            createFlamePipeline();
        }

        void createFlameMesh() {
            constexpr int segments = 40;
            constexpr float baseZ = 0.555f;
            constexpr float tipZ = 1.02f;
            constexpr float baseY = 0.040f;
            constexpr float outerRadius = 0.052f;
            constexpr float innerRadius = 0.026f;

            std::vector<FlameVertex> vertices{};
            vertices.reserve(static_cast<std::size_t>(segments) * 6U);

            const glm::vec4 outerBaseColor{1.0f, 0.42f, 0.08f, 0.50f};
            const glm::vec4 outerTipColor{0.7f, 0.08f, 0.0f, 0.0f};
            const glm::vec4 innerBaseColor{1.0f, 0.92f, 0.45f, 0.72f};
            const glm::vec4 innerTipColor{1.0f, 0.32f, 0.04f, 0.0f};

            auto addCone = [&](float radius, const glm::vec4 &baseColor, const glm::vec4 &tipColor) {
                const glm::vec3 tip{0.0f, baseY, tipZ};
                for (int i = 0; i < segments; ++i) {
                    const float a0 = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * PI;
                    const float a1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * 2.0f * PI;
                    const glm::vec3 p0{std::cos(a0) * radius, baseY + std::sin(a0) * radius, baseZ};
                    const glm::vec3 p1{std::cos(a1) * radius, baseY + std::sin(a1) * radius, baseZ};
                    vertices.push_back({p0, baseColor});
                    vertices.push_back({p1, baseColor});
                    vertices.push_back({tip, tipColor});
                }
            };

            addCone(outerRadius, outerBaseColor, outerTipColor);
            addCone(innerRadius, innerBaseColor, innerTipColor);

            flameVertexCount = static_cast<uint32_t>(vertices.size());
            const VkDeviceSize bufferSize = sizeof(FlameVertex) * static_cast<VkDeviceSize>(vertices.size());
            createBuffer(
                bufferSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                flameVertexBuffer,
                flameVertexBufferMemory);

            void *data = nullptr;
            if (vkMapMemory(device, flameVertexBufferMemory, 0, bufferSize, 0, &data) != VK_SUCCESS || data == nullptr) {
                throw mxvk::Exception("Failed to map flame vertex buffer");
            }
            std::memcpy(data, vertices.data(), static_cast<std::size_t>(bufferSize));
            vkUnmapMemory(device, flameVertexBufferMemory);
        }

        void createFlamePipeline() {
            const std::vector<char> vertShaderCode = loadSpv(std::string(STARSHIP_EXAMPLE_SHADER_DIR) + "/flame.vert.spv");
            const std::vector<char> fragShaderCode = loadSpv(std::string(STARSHIP_EXAMPLE_SHADER_DIR) + "/flame.frag.spv");

            VkShaderModule vertShaderModule = createShaderModule(device, vertShaderCode);
            VkShaderModule fragShaderModule = VK_NULL_HANDLE;

            try {
                fragShaderModule = createShaderModule(device, fragShaderCode);

                VkPipelineShaderStageCreateInfo vertStage{};
                vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
                vertStage.module = vertShaderModule;
                vertStage.pName = "main";

                VkPipelineShaderStageCreateInfo fragStage{};
                fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                fragStage.module = fragShaderModule;
                fragStage.pName = "main";

                std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertStage, fragStage};

                VkVertexInputBindingDescription bindingDescription{};
                bindingDescription.binding = 0;
                bindingDescription.stride = sizeof(FlameVertex);
                bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                std::array<VkVertexInputAttributeDescription, 2> attributes{};
                attributes[0].binding = 0;
                attributes[0].location = 0;
                attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
                attributes[0].offset = offsetof(FlameVertex, pos);
                attributes[1].binding = 0;
                attributes[1].location = 1;
                attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                attributes[1].offset = offsetof(FlameVertex, color);

                VkPipelineVertexInputStateCreateInfo vertexInput{};
                vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                vertexInput.vertexBindingDescriptionCount = 1;
                vertexInput.pVertexBindingDescriptions = &bindingDescription;
                vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
                vertexInput.pVertexAttributeDescriptions = attributes.data();

                VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
                inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                inputAssembly.primitiveRestartEnable = VK_FALSE;

                VkPipelineViewportStateCreateInfo viewportState{};
                viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewportState.viewportCount = 1;
                viewportState.scissorCount = 1;

                const std::array<VkDynamicState, 2> dynamicStates = {
                    VK_DYNAMIC_STATE_VIEWPORT,
                    VK_DYNAMIC_STATE_SCISSOR,
                };
                VkPipelineDynamicStateCreateInfo dynamicInfo{};
                dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamicInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
                dynamicInfo.pDynamicStates = dynamicStates.data();

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

                VkPipelineDepthStencilStateCreateInfo depthStencil{};
                depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depthStencil.depthTestEnable = VK_TRUE;
                depthStencil.depthWriteEnable = VK_FALSE;
                depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
                depthStencil.depthBoundsTestEnable = VK_FALSE;
                depthStencil.stencilTestEnable = VK_FALSE;

                VkPipelineColorBlendAttachmentState colorBlendAttachment{};
                colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                colorBlendAttachment.blendEnable = VK_TRUE;
                colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
                colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

                VkPipelineColorBlendStateCreateInfo colorBlending{};
                colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                colorBlending.logicOpEnable = VK_FALSE;
                colorBlending.attachmentCount = 1;
                colorBlending.pAttachments = &colorBlendAttachment;

                VkPushConstantRange pushRange{};
                pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                pushRange.offset = 0;
                pushRange.size = sizeof(FlamePushConstants);

                VkPipelineLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                layoutInfo.pushConstantRangeCount = 1;
                layoutInfo.pPushConstantRanges = &pushRange;

                if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &flamePipelineLayout) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create flame pipeline layout");
                }

                const VkFormat colorFormat = getSwapchainFormat();
                const VkFormat depthFormat = getDepthFormat();

                VkPipelineRenderingCreateInfo renderingInfo{};
                renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &colorFormat;
                if (depthFormat != VK_FORMAT_UNDEFINED) {
                    renderingInfo.depthAttachmentFormat = depthFormat;
                }

                VkGraphicsPipelineCreateInfo pipelineInfo{};
                pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
                pipelineInfo.pStages = shaderStages.data();
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicInfo;
                pipelineInfo.layout = flamePipelineLayout;
                pipelineInfo.renderPass = VK_NULL_HANDLE;
                pipelineInfo.subpass = 0;

                if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &flamePipeline) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create flame pipeline");
                }
            } catch (...) {
                if (fragShaderModule != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(device, fragShaderModule, nullptr);
                }
                vkDestroyShaderModule(device, vertShaderModule, nullptr);
                cleanupFlameSwapchainResources();
                throw;
            }

            vkDestroyShaderModule(device, fragShaderModule, nullptr);
            vkDestroyShaderModule(device, vertShaderModule, nullptr);
        }

        void drawEngineFlame(VkCommandBuffer cmd, const VkExtent2D &extent, float elapsedSeconds, const glm::mat4 &modelMatrix, const glm::mat4 &view, const glm::mat4 &proj) {
            if (flamePipeline == VK_NULL_HANDLE || flameVertexBuffer == VK_NULL_HANDLE || flameVertexCount == 0) {
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

            FlamePushConstants pc{};
            pc.mvp = proj * view * modelMatrix;
            pc.params = glm::vec4(elapsedSeconds, 0.0f, 0.0f, 0.0f);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flamePipeline);
            vkCmdPushConstants(
                cmd,
                flamePipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(pc),
                &pc);

            VkBuffer vertexBuffers[] = {flameVertexBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdDraw(cmd, flameVertexCount, 1, 0, 0);
        }

        float randomFloat(float minv, float maxv) {
            static std::random_device rd;
            static std::default_random_engine eng(rd());
            std::uniform_real_distribution<float> dist(minv, maxv);
            return dist(eng);
        }

        glm::vec3 getStarColor(float temperature) const {
            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;

            if (temperature < 3700.0f) {
                r = 1.0f;
                g = temperature / 3700.0f * 0.6f;
                b = 0.0f;
            } else if (temperature < 5200.0f) {
                r = 1.0f;
                g = 0.6f + (temperature - 3700.0f) / 1500.0f * 0.4f;
                b = (temperature - 3700.0f) / 1500.0f * 0.3f;
            } else if (temperature < 6000.0f) {
                r = 1.0f;
                g = 1.0f;
                b = (temperature - 5200.0f) / 800.0f * 0.7f;
            } else if (temperature < 7500.0f) {
                r = 1.0f;
                g = 1.0f;
                b = 0.7f + (temperature - 6000.0f) / 1500.0f * 0.3f;
            } else {
                r = 0.7f - (temperature - 7500.0f) / 10000.0f * 0.4f;
                g = 0.8f + (temperature - 7500.0f) / 10000.0f * 0.2f;
                b = 1.0f;
            }

            return glm::vec3(r, g, b);
        }

        float magnitudeToSize(float magnitude) const {
            return glm::clamp(6.5f - magnitude * 0.85f, 0.75f, 7.0f);
        }

        float magnitudeToAlpha(float magnitude) const {
            const float alpha = (6.5f - magnitude) / 6.5f;
            return glm::clamp(alpha - lightPollution, 0.0f, 1.0f);
        }

        void createBuffer(VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          VkBuffer &buffer,
                          VkDeviceMemory &bufferMemory) const {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = size;
            bufferInfo.usage = usage;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create buffer");
            }

            VkMemoryRequirements memRequirements{};
            vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

            if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
                vkDestroyBuffer(device, buffer, nullptr);
                buffer = VK_NULL_HANDLE;
                throw mxvk::Exception("Failed to allocate buffer memory");
            }
            if (vkBindBufferMemory(device, buffer, bufferMemory, 0) != VK_SUCCESS) {
                vkFreeMemory(device, bufferMemory, nullptr);
                vkDestroyBuffer(device, buffer, nullptr);
                buffer = VK_NULL_HANDLE;
                bufferMemory = VK_NULL_HANDLE;
                throw mxvk::Exception("Failed to bind buffer memory");
            }
        }

        [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
            VkPhysicalDeviceMemoryProperties memProperties{};
            vkGetPhysicalDeviceMemoryProperties(physical_device, &memProperties);

            for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
                if ((typeFilter & (1U << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }

            throw mxvk::Exception("Failed to find suitable memory type");
        }

        [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = command_pool;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to allocate command buffer");
            }

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
                vkFreeCommandBuffers(device, command_pool, 1, &commandBuffer);
                throw mxvk::Exception("Failed to begin command buffer");
            }

            return commandBuffer;
        }

        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
            if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
                vkFreeCommandBuffers(device, command_pool, 1, &commandBuffer);
                throw mxvk::Exception("Failed to end command buffer");
            }

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            if (vkQueueSubmit(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
                vkFreeCommandBuffers(device, command_pool, 1, &commandBuffer);
                throw mxvk::Exception("Failed to submit command buffer");
            }
            if (vkQueueWaitIdle(graphics_queue) != VK_SUCCESS) {
                vkFreeCommandBuffers(device, command_pool, 1, &commandBuffer);
                throw mxvk::Exception("Failed to wait for graphics queue idle");
            }

            vkFreeCommandBuffers(device, command_pool, 1, &commandBuffer);
        }

        void createImage(uint32_t width,
                         uint32_t height,
                         VkFormat format,
                         VkImageTiling tiling,
                         VkImageUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         VkImage &image,
                         VkDeviceMemory &memory) const {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = width;
            imageInfo.extent.height = height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = format;
            imageInfo.tiling = tiling;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = usage;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create image");
            }

            VkMemoryRequirements memRequirements{};
            vkGetImageMemoryRequirements(device, image, &memRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

            if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
                vkDestroyImage(device, image, nullptr);
                image = VK_NULL_HANDLE;
                throw mxvk::Exception("Failed to allocate image memory");
            }
            if (vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
                vkFreeMemory(device, memory, nullptr);
                vkDestroyImage(device, image, nullptr);
                image = VK_NULL_HANDLE;
                memory = VK_NULL_HANDLE;
                throw mxvk::Exception("Failed to bind image memory");
            }
        }

        [[nodiscard]] VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect) const {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format;
            viewInfo.subresourceRange.aspectMask = aspect;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            VkImageView imageView = VK_NULL_HANDLE;
            if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create image view");
            }
            return imageView;
        }

        void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) const {
            VkCommandBuffer commandBuffer = beginSingleTimeCommands();

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

            if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            } else {
                throw mxvk::Exception("Unsupported image layout transition");
            }

            vkCmdPipelineBarrier(
                commandBuffer,
                sourceStage,
                destinationStage,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);

            endSingleTimeCommands(commandBuffer);
        }

        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const {
            VkCommandBuffer commandBuffer = beginSingleTimeCommands();

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

            vkCmdCopyBufferToImage(
                commandBuffer,
                buffer,
                image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &region);

            endSingleTimeCommands(commandBuffer);
        }

        bool starfieldInitialized = false;
        int numStars = 0;
        std::vector<Star> stars{};
        float atmosphericTwinkle = 1.0f;
        float lightPollution = 0.0f;

        VkImage starTexture = VK_NULL_HANDLE;
        VkDeviceMemory starTextureMemory = VK_NULL_HANDLE;
        VkImageView starTextureView = VK_NULL_HANDLE;
        VkSampler starSampler = VK_NULL_HANDLE;

        VkBuffer starVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory starVertexBufferMemory = VK_NULL_HANDLE;
        void *starVertexBufferMapped = nullptr;

        VkDescriptorSetLayout starDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool starDescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> starDescriptorSets{};
        std::vector<VkBuffer> starUniformBuffers{};
        std::vector<VkDeviceMemory> starUniformBufferMemories{};
        std::vector<void *> starUniformBufferMapped{};
        VkPipeline starPipeline = VK_NULL_HANDLE;
        VkPipelineLayout starPipelineLayout = VK_NULL_HANDLE;
        Uint32 lastStarUpdateTime = 0;

        VkBuffer flameVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory flameVertexBufferMemory = VK_NULL_HANDLE;
        VkPipeline flamePipeline = VK_NULL_HANDLE;
        VkPipelineLayout flamePipelineLayout = VK_NULL_HANDLE;
        uint32_t flameVertexCount = 0;

        mxvk::VKAbstractModel model{};
        std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
        bool mouseDragging = false;
        int lastMouseX = 0;
        int lastMouseY = 0;
        float mouseYawDegrees = 0.0f;
        float mousePitchDegrees = 0.0f;
        float mouseSensitivity = 0.35f;
        float autoSpinSpeed = 0.65f;
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        std::string filename = args.filename;
        if (filename.empty()) {
            filename = args.path + "/data/starship.obj";
        }
        example::StarshipWindow window(filename, "MXVK Starship Example", args.width, args.height, args.fullscreen, args.enable_vsync);
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
