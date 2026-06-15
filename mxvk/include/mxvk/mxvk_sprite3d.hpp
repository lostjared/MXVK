/**
 * @file mxvk_sprite3d.hpp
 * @brief World-space textured billboard renderer for MXVK dynamic rendering.
 */
#ifndef __MXVK_SPRITE3D__
#define __MXVK_SPRITE3D__

#include <volk/volk.h>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace mxvk {

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
        VK_Sprite3D() = default;
        ~VK_Sprite3D();

        VK_Sprite3D(const VK_Sprite3D &) = delete;
        VK_Sprite3D &operator=(const VK_Sprite3D &) = delete;
        VK_Sprite3D(VK_Sprite3D &&) = delete;
        VK_Sprite3D &operator=(VK_Sprite3D &&) = delete;

        void load(VK_Window *window,
                  const std::string &pngPath,
                  const std::string &vertexShaderPath = "",
                  const std::string &fragmentShaderPath = "");
        void load(VK_Window *window,
                  SDL_Surface *surface,
                  const std::string &vertexShaderPath = "",
                  const std::string &fragmentShaderPath = "");

        void updateCamera(uint32_t imageIndex, const glm::mat4 &view, const glm::mat4 &proj);

        void drawSprite(const glm::vec3 &position,
                        const glm::vec2 &size,
                        const glm::vec4 &color = glm::vec4(1.0f),
                        float rotationRadians = 0.0f);
        void render(VkCommandBuffer cmd, uint32_t imageIndex);
        void clearQueue();

        void setDepthTestEnabled(bool enabled);
        void setDepthWriteEnabled(bool enabled);
        void setAlphaDiscardThreshold(float threshold) { alphaDiscardThreshold = threshold; }

        void resize(VK_Window *window);
        void cleanup();

        [[nodiscard]] bool loaded() const { return spriteLoaded; }
        [[nodiscard]] int getWidth() const { return spriteWidth; }
        [[nodiscard]] int getHeight() const { return spriteHeight; }

      private:
        struct Vertex {
            glm::vec2 pos;
            glm::vec2 uv;
        };

        struct DrawCmd {
            glm::vec3 position;
            glm::vec2 size;
            glm::vec4 color;
            float rotationRadians = 0.0f;
        };

        struct CameraUBO {
            glm::mat4 view{1.0f};
            glm::mat4 proj{1.0f};
        };

        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
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

        void createQuadBuffers();
        void createTexture(SDL_Surface *surface);
        void createSampler();
        void createDescriptorSetLayout();
        void createCameraBuffers();
        void destroyCameraBuffers();
        void createDescriptorPool();
        void createDescriptorSets();
        void createPipeline();
        void destroyPipeline();
        void destroyTexture();
        void destroyBuffers();
        void destroyDescriptors();

        void createBuffer(VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          VkBuffer &buffer,
                          VkDeviceMemory &bufferMemory) const;
        [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
        [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const;
        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;
        void createImage(uint32_t width,
                         uint32_t height,
                         VkFormat format,
                         VkImageTiling tiling,
                         VkImageUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         VkImage &image,
                         VkDeviceMemory &imageMemory) const;
        [[nodiscard]] VkImageView createImageView(VkImage image, VkFormat format) const;
        void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) const;
        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;
        [[nodiscard]] SDL_Surface *convertToRGBA(SDL_Surface *surface) const;
        [[nodiscard]] std::vector<char> readShaderFile(const std::string &path) const;
        [[nodiscard]] VkShaderModule createShaderModule(const std::vector<char> &code) const;
    };

} // namespace mxvk

#endif
