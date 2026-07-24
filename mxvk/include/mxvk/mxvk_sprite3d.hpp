/**
 * @file mxvk_sprite3d.hpp
 * @brief World-space textured billboard renderer for MXVK dynamic rendering.
 *
 * VK_Sprite3D manages a textured quad in world space, with per-frame camera
 * uniforms, depth-tested rendering, and a draw queue for billboard sprites.
 */
#pragma once

#include <volk/volk.h>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace mxvk {

    /**
     * @brief Forward declaration of the MXVK window wrapper.
     */
    class VK_Window;

    /**
     * @class VK_Sprite3D
     * @brief Depth-tested 3D billboard sprite batch.
     *
     * VK_Sprite3D renders textured quads in world space.  Each queued sprite is
     * camera-facing, uses the view/projection matrix supplied with updateCamera(),
     * and participates in the same dynamic-rendering pass as models.
     */
    class VK_Sprite3D {
      public:
        /**
         * @brief Construct an empty 3D sprite batch.
         */
        VK_Sprite3D() = default;

        /**
         * @brief Destroy owned Vulkan resources.
         */
        ~VK_Sprite3D();

        VK_Sprite3D(const VK_Sprite3D &) = delete;
        VK_Sprite3D &operator=(const VK_Sprite3D &) = delete;
        VK_Sprite3D(VK_Sprite3D &&) = delete;
        VK_Sprite3D &operator=(VK_Sprite3D &&) = delete;

        /**
         * @brief Load sprite texture and build the 3D billboard pipeline from a PNG file.
         * @param window Active MXVK window.
         * @param pngPath Path to the PNG file.
         * @param vertexShaderPath Optional custom vertex shader SPIR-V path.
         * @param fragmentShaderPath Optional custom fragment shader SPIR-V path.
         */
        void load(VK_Window *window,
                  const std::string &pngPath,
                  const std::string &vertexShaderPath = "",
                  const std::string &fragmentShaderPath = "");

        /**
         * @brief Load sprite texture and build the 3D billboard pipeline from an SDL surface.
         * @param window Active MXVK window.
         * @param surface Source surface pointer.
         * @param vertexShaderPath Optional custom vertex shader SPIR-V path.
         * @param fragmentShaderPath Optional custom fragment shader SPIR-V path.
         */
        void load(VK_Window *window,
                  SDL_Surface *surface,
                  const std::string &vertexShaderPath = "",
                  const std::string &fragmentShaderPath = "");

        /**
         * @brief Upload the current camera matrices for one swapchain image.
         * @param imageIndex Swapchain image index.
         * @param view View matrix.
         * @param proj Projection matrix.
         */
        void updateCamera(uint32_t imageIndex, const glm::mat4 &view, const glm::mat4 &proj);

        /**
         * @brief Queue a billboard sprite for rendering.
         * @param position World-space center position.
         * @param size Billboard size in world units.
         * @param color Per-sprite tint color.
         * @param rotationRadians Rotation around the camera-facing axis.
         */
        void drawSprite(const glm::vec3 &position,
                        const glm::vec2 &size,
                        const glm::vec4 &color = glm::vec4(1.0f),
                        float rotationRadians = 0.0f);

        /**
         * @brief Record all queued billboard draws into the given command buffer.
         * @param cmd Active command buffer.
         * @param imageIndex Current swapchain image index.
         */
        void render(VkCommandBuffer cmd, uint32_t imageIndex);

        /**
         * @brief Discard all queued sprite draws without rendering them.
         */
        void clearQueue();

        /**
         * @brief Enable or disable depth testing for the 3D sprite pipeline.
         * @param enabled @c true to enable depth testing.
         */
        void setDepthTestEnabled(bool enabled);

        /**
         * @brief Enable or disable depth writes for the 3D sprite pipeline.
         * @param enabled @c true to write depth values.
         */
        void setDepthWriteEnabled(bool enabled);

        /**
         * @brief Set the alpha threshold used to discard transparent texels.
         * @param threshold Alpha cutoff value.
         */
        void setAlphaDiscardThreshold(float threshold) { alphaDiscardThreshold = threshold; }

        /**
         * @brief Rebuild swapchain-dependent resources after resize.
         * @param window Active MXVK window.
         */
        void resize(VK_Window *window);

        /**
         * @brief Destroy all owned Vulkan resources.
         */
        void cleanup();

        /** @return @c true if the sprite texture and pipeline are loaded. */
        [[nodiscard]] bool loaded() const { return spriteLoaded; }
        /** @return Sprite texture width in pixels. */
        [[nodiscard]] int getWidth() const { return spriteWidth; }
        /** @return Sprite texture height in pixels. */
        [[nodiscard]] int getHeight() const { return spriteHeight; }

      private:
        /** @brief Static quad vertex containing local position and UV coordinates. */
        struct Vertex {
            glm::vec2 pos;
            glm::vec2 uv;
        };

        /** @brief One queued 3D billboard draw command. */
        struct DrawCmd {
            glm::vec3 position;
            glm::vec2 size;
            glm::vec4 color;
            float rotationRadians = 0.0f;
        };

        /** @brief Per-frame camera uniform payload. */
        struct CameraUBO {
            glm::mat4 view{1.0f};
            glm::mat4 proj{1.0f};
        };

        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkPipelineCache pipelineCache = VK_NULL_HANDLE;
        VkFormat colorAttachmentFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        size_t imageCount = 0;

        VkImage spriteImage = VK_NULL_HANDLE;
        VkDeviceMemory spriteImageMemory = VK_NULL_HANDLE;
        VkImageView spriteImageView = VK_NULL_HANDLE;
        VkSampler spriteSampler = VK_NULL_HANDLE;
        int spriteWidth = 0;
        int spriteHeight = 0;
        bool spriteLoaded = false;

        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;

        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptorSets;

        std::vector<VkBuffer> cameraBuffers;
        std::vector<VkDeviceMemory> cameraBufferMemory;
        std::vector<void *> cameraBuffersMapped;

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        std::string vertexShaderPath;
        std::string fragmentShaderPath;
        bool depthTestEnabled = true;
        bool depthWriteEnabled = false;
        float alphaDiscardThreshold = 0.1f;

        std::vector<DrawCmd> drawQueue;

        /** @brief Build the quad vertex and index buffers. */
        void createQuadBuffers();
        /** @brief Upload and initialize the sprite texture image. */
        void createTexture(SDL_Surface *surface);
        /** @brief Create the sampler used to sample the sprite texture. */
        void createSampler();
        /** @brief Create the descriptor set layout used by the pipeline. */
        void createDescriptorSetLayout();
        /** @brief Allocate and map one camera UBO per swapchain image. */
        void createCameraBuffers();
        /** @brief Destroy the per-image camera UBO resources. */
        void destroyCameraBuffers();
        /** @brief Create the descriptor pool for texture and camera bindings. */
        void createDescriptorPool();
        /** @brief Allocate and update descriptor sets for the loaded sprite. */
        void createDescriptorSets();
        /** @brief Create or recreate the graphics pipeline. */
        void createPipeline();
        /** @brief Destroy the graphics pipeline and pipeline layout. */
        void destroyPipeline();
        /** @brief Destroy the texture image, view, and sampler. */
        void destroyTexture();
        /** @brief Destroy the quad vertex and index buffers. */
        void destroyBuffers();
        /** @brief Destroy descriptor resources owned by this sprite batch. */
        void destroyDescriptors();

        /**
         * @brief Create a Vulkan buffer and bind device memory to it.
         * @param size Buffer size in bytes.
         * @param usage Buffer usage flags.
         * @param properties Required memory properties.
         * @param buffer Output buffer handle.
         * @param bufferMemory Output device memory handle.
         */
        void createBuffer(VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          VkBuffer &buffer,
                          VkDeviceMemory &bufferMemory) const;
        /** @brief Find a suitable memory type index for the requested properties. */
        [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
        /** @brief Begin a one-time command buffer on the sprite command pool. */
        [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const;
        /** @brief End and submit a one-time command buffer. */
        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;
        /**
         * @brief Create a Vulkan image for the sprite texture.
         * @param width Image width in pixels.
         * @param height Image height in pixels.
         * @param format Image format.
         * @param tiling Image tiling mode.
         * @param usage Image usage flags.
         * @param properties Required memory properties.
         * @param image Output image handle.
         * @param imageMemory Output device memory handle.
         */
        void createImage(uint32_t width,
                         uint32_t height,
                         VkFormat format,
                         VkImageTiling tiling,
                         VkImageUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         VkImage &image,
                         VkDeviceMemory &imageMemory) const;
        /** @brief Create an image view for the sprite texture. */
        [[nodiscard]] VkImageView createImageView(VkImage image, VkFormat format) const;
        /** @brief Transition an image between layouts for upload and sampling. */
        void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) const;
        /** @brief Copy a staging buffer into the sprite texture image. */
        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;
        /** @brief Convert an SDL surface to RGBA32 format. */
        [[nodiscard]] SDL_Surface *convertToRGBA(SDL_Surface *surface) const;
        /** @brief Read a SPIR-V shader file from disk. */
        [[nodiscard]] std::vector<char> readShaderFile(const std::string &path) const;
    };

} // namespace mxvk
