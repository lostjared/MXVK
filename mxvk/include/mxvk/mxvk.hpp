#ifndef _MXVK_MXVK_H_
#define _MXVK_MXVK_H_

#include "mxvk_sprite.hpp"
#include "mxvk_sprite3d.hpp"
#include "mxvk_text.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <mxvk/mxvk_version.hpp>
#include <optional>
#include <string>
#include <vector>
#include <volk/volk.h>

#ifdef ENABLE_VALIDATION
#define MXVK_VALIDATION true
#else
#define MXVK_VALIDATION false
#endif

namespace mxvk {
    /**
     * @brief Main Vulkan window wrapper for MXVK.
     *
     * The class owns SDL window resources and a minimal Vulkan bootstrap
     * (instance + presentation surface).
     */
    class VK_Window {
      public:
        /**
         * @brief Construct an empty window object.
         */
        VK_Window() = default;

        /**
         * @brief Destroy owned Vulkan and SDL resources.
         */
        virtual ~VK_Window();

        /**
         * @brief Release Vulkan and SDL resources.
         *
         * Safe to call multiple times.
         */
        void release();

        /**
         * @brief Construct and initialize a window and Vulkan context.
         * @param title Window title string.
         * @param width Window width in pixels.
         * @param height Window height in pixels.
         * @param full Enables fullscreen mode when true.
         * @param validiation Enables validation-related behavior when true.
         */
        VK_Window(const std::string &title, int width, int height, bool full = false, bool validiation = true);

        // no copy
        VK_Window(const VK_Window &) = delete;
        VK_Window(VK_Window &&) = delete;
        VK_Window &operator=(const VK_Window &) = delete;
        VK_Window &operator=(VK_Window &&) = delete;

        /**
         * @brief Initialize Vulkan state.
         * @param validiation Enables validation-related behavior when true.
         * @return true on success, false otherwise.
         */

        virtual bool initVulkan(bool validiation);

        /**
         * @brief Handle one SDL event.
         * @param e SDL event to process.
         */
        virtual void event(SDL_Event &e);

        /**
         * @brief Run the main event/render loop.
         */
        void loop();

        /**
         * @brief Render one frame.
         */
        virtual void render();

        /**
         * @brief Execute one processing/update step.
         */
        virtual void proc();

        /**
         * @brief Set the active text-render font.
         * @param fontPath Path to a TTF font file.
         * @param fontSize Font point size.
         */
        void setFont(const std::string &fontPath, int fontSize = 24);

        /**
         * @brief Queue a text string for rendering during the current frame.
         * @param text UTF-8 text.
         * @param x Pixel X coordinate.
         * @param y Pixel Y coordinate.
         * @param col Text color.
         */
        void printText(const std::string &text, int x, int y, const SDL_Color &col);
        void printText(const std::string &text, int x, int y, const SDL_Color &col, TTF_Font *font);
        void printText(const std::string &text, int x, int y, const SDL_Color &col, const Font &font);

        /** @brief Clear all queued text draw calls for the current frame. */
        void clearTextQueue();

        /**
         * @brief Set the per-frame color attachment clear color.
         * @param r Red channel [0, 1].
         * @param g Green channel [0, 1].
         * @param b Blue channel [0, 1].
         * @param a Alpha channel [0, 1].
         */
        void setClearColor(float r, float g, float b, float a = 1.0f);

        /** @brief Get the underlying SDL window handle. */
        [[nodiscard]] SDL_Window *getSDLWindow() const noexcept { return window.get(); }

        /** @brief Get the Vulkan logical device handle. */
        [[nodiscard]] VkDevice getDevice() const noexcept { return device; }

        /** @brief Get the Vulkan physical device handle. */
        [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const noexcept { return physical_device; }

        /** @brief Get the graphics queue handle. */
        [[nodiscard]] VkQueue getGraphicsQueue() const noexcept { return graphics_queue; }

        /** @brief Get the command pool used for graphics/upload work. */
        [[nodiscard]] VkCommandPool getCommandPool() const noexcept { return command_pool; }

        /** @brief Get the swapchain color format. */
        [[nodiscard]] VkFormat getSwapchainFormat() const noexcept { return swapchain_format; }

        /** @brief Get the current swapchain extent. */
        [[nodiscard]] VkExtent2D getSwapchainExtent() const noexcept { return swapchain_extent; }

        /** @brief Get the depth format used for dynamic rendering attachments. */
        [[nodiscard]] VkFormat getDepthFormat() const noexcept { return depth_format; }

        /** @brief Get the number of swapchain images currently allocated. */
        [[nodiscard]] size_t getSwapchainImageCount() const noexcept { return swapchain_images.size(); }

        /**
         * @brief Ensure deferred render resources are initialized.
         * @return true when swapchain, command pool, and sync objects are ready.
         */
        bool ensureRenderResources();

        /**
         * @brief Measure text dimensions in pixels.
         * @param text Text string to measure.
         * @param width Output width in pixels.
         * @param height Output height in pixels.
         * @return true when measurement succeeded.
         */
        [[nodiscard]] bool getTextDimensions(const std::string &text, int &width, int &height);
        [[nodiscard]] bool getTextDimensions(const std::string &text, int &width, int &height, TTF_Font *font);
        [[nodiscard]] bool getTextDimensions(const std::string &text, int &width, int &height, const Font &font);

        /**
         * @brief Create a sprite from a PNG file and register it with this window.
         * @param pngPath Path to the PNG file.
         * @param vertexShaderPath Optional custom vertex shader SPIR-V path.
         * @param fragmentShaderPath Optional custom fragment shader SPIR-V path.
         * @return Non-owning pointer to the created sprite.
         */
        VK_Sprite *createSprite(const std::string &pngPath, const std::string &vertexShaderPath = "", const std::string &fragmentShaderPath = "");

        /**
         * @brief Create a sprite from an SDL surface and register it with this window.
         * @param surface Source surface pointer.
         * @param vertexShaderPath Optional custom vertex shader SPIR-V path.
         * @param fragmentShaderPath Optional custom fragment shader SPIR-V path.
         * @return Non-owning pointer to the created sprite.
         */
        VK_Sprite *createSprite(SDL_Surface *surface, const std::string &vertexShaderPath = "", const std::string &fragmentShaderPath = "");

        /**
         * @brief Create a blank sprite texture and register it with this window.
         * @param width Texture width in pixels.
         * @param height Texture height in pixels.
         * @param vertexShaderPath Optional custom vertex shader SPIR-V path.
         * @param fragmentShaderPath Optional custom fragment shader SPIR-V path.
         * @return Non-owning pointer to the created sprite.
         */
        VK_Sprite *createSprite(int width, int height, const std::string &vertexShaderPath = "", const std::string &fragmentShaderPath = "");

        void enablePostProcessing(VK_Sprite *sprite);

        void setPostProcessingEnabled(bool enabled) { post_process_enabled = enabled; }

        /**
         * @brief Create a world-space billboard sprite from a PNG file.
         * @param pngPath Path to the PNG file.
         * @param vertexShaderPath Optional custom vertex shader SPIR-V path.
         * @param fragmentShaderPath Optional custom fragment shader SPIR-V path.
         * @return Non-owning pointer to the created 3D sprite batch.
         */
        VK_Sprite3D *createSprite3D(const std::string &pngPath, const std::string &vertexShaderPath = "", const std::string &fragmentShaderPath = "");

        /**
         * @brief Create a world-space billboard sprite from an SDL surface.
         * @param surface Source surface pointer.
         * @param vertexShaderPath Optional custom vertex shader SPIR-V path.
         * @param fragmentShaderPath Optional custom fragment shader SPIR-V path.
         * @return Non-owning pointer to the created 3D sprite batch.
         */
        VK_Sprite3D *createSprite3D(SDL_Surface *surface, const std::string &vertexShaderPath = "", const std::string &fragmentShaderPath = "");

        /**
         * @brief Check whether Vulkan validation layers are currently enabled.
         * @return true if validation layers are enabled, false otherwise.
         */
        [[nodiscard]] bool validationEnabled() const;

        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT *callback_data, [[maybe_unused]] void *user_data);

	void showCursor(bool on);

      protected:
        /**
         * @brief Load a SPIR-V file from disk.
         * @param path Absolute or relative path to a .spv file.
         * @return Raw bytes loaded from file.
         * @throws mxvk::Exception when the file cannot be opened, is empty, or is not 4-byte aligned.
         */
        static std::vector<char> loadSpv(const std::string &path);

        /**
         * @brief Create a shader module from SPIR-V bytecode.
         * @param device Logical Vulkan device used to create the module.
         * @param spv_bytes SPIR-V bytecode payload.
         * @return Created shader module handle.
         * @throws mxvk::Exception when inputs are invalid or module creation fails.
         */
        static VkShaderModule createShaderModule(VkDevice device, const std::vector<char> &spv_bytes);

        /**
         * @brief Initialize SDL window resources.
         * @param title Window title string.
         * @param width Window width in pixels.
         * @param height Window height in pixels.
         * @param flags Additional SDL window creation flags.
         * @return true on success, false otherwise.
         */
        bool initWindow(const std::string &title, int width, int height, SDL_WindowFlags flags);

        /**
         * @brief Pick a suitable Vulkan physical device.
         */
        void pickDevice();

        /**
         * @brief Create a logical Vulkan device from the selected physical device.
         */
        void createLogicalDevice();

        /**
         * @brief Create final device resources.
         */
        void createDevice();

        /**
         * @brief Request loop termination.
         */
        void exit();

        /**
         * @brief Called right before swapchain-dependent resources are recreated.
         *
         * Derived classes can release swapchain-dependent resources here.
         */
        virtual void onSwapchainAboutToRecreate();

        /**
         * @brief Called after swapchain and render resources are recreated.
         *
         * Derived classes can rebuild swapchain-dependent resources here.
         */
        virtual void onSwapchainRecreated();

        /**
         * @brief Optional hook for derived classes to record extra draw commands.
         *
         * This callback is invoked inside the dynamic-rendering scope started by
         * the window, after viewport/scissor are configured and before rendering ends.
         *
         * @param cmd Active command buffer in recording state.
         * @param image_index Current swapchain image index.
         */
        virtual void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t image_index);

      protected:
        /**
         * @brief Render one standalone sprite using the window's shared sprite pipeline.
         *
         * This is intended for derived classes that want to draw a sprite before or after
         * their own scene content without registering it in the window-managed sprite list.
         */
        void renderStandaloneSprite(VK_Sprite &sprite, VkCommandBuffer cmd);

      private:
        static constexpr uint32_t invalid_queue_index = std::numeric_limits<uint32_t>::max();
        static constexpr uint32_t max_frames_in_flight = 2;

        static constexpr const char *validation_layer_name = "VK_LAYER_KHRONOS_validation";

        struct SwapchainSupport {
            VkSurfaceCapabilitiesKHR capabilities{};
            std::vector<VkSurfaceFormatKHR> formats{};
            std::vector<VkPresentModeKHR> present_modes{};
        };

        static SwapchainSupport querySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);
        static VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &available_formats);
        static VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR> &available_present_modes);
        static VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR &capabilities, SDL_Window *window);
        static bool hasValidationLayerSupport();
        static std::optional<VkDebugUtilsMessengerCreateInfoEXT> makeDebugMessengerCreateInfo();
        void setupDebugMessenger();
        void cleanupDebugMessenger();
        bool createSwapchain(VkSwapchainKHR old_swapchain);
        bool createRenderResources();
        bool createSyncObjects();
        void cleanupSyncObjects();
        void cleanupSwapchain(bool preserveCommandPool = true);
        void recreateSwapchain();
        void drawFrame();
        [[nodiscard]] std::string resolveRuntimeShaderPath(const std::string &shaderFileName, const char *fallbackDir) const;
        void createSpriteDescriptorSetLayout();
        void createSpritePipeline();
        void destroySpritePipeline();
        void createPostProcessTargets();
        void destroyPostProcessTargets();
        void ensureTextRenderer();
        void createTextDescriptorSetLayout();
        void createTextPipeline();
        void destroyTextPipeline();

      protected:
        // Protected state allows subclasses to implement custom rendering paths.

        struct SDLWindowDeleter {
            void operator()(SDL_Window *ptr) const {
                if (ptr != nullptr) {
                    SDL_DestroyWindow(ptr);
                }
            }
        };

        std::unique_ptr<SDL_Window, SDLWindowDeleter> window{};
        VkInstance instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        uint32_t graphics_queue_family = invalid_queue_index;
        uint32_t present_queue_family = invalid_queue_index;
        VkQueue graphics_queue = VK_NULL_HANDLE;
        VkQueue present_queue = VK_NULL_HANDLE;

        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
        VkFormat depth_format = VK_FORMAT_UNDEFINED;
        VkExtent2D swapchain_extent{};
        std::vector<VkImage> swapchain_images{};
        std::vector<VkImageView> swapchain_image_views{};
        std::vector<bool> swapchain_image_initialized{};
        std::vector<VkImage> depth_images{};
        std::vector<VkDeviceMemory> depth_image_memories{};
        std::vector<VkImageView> depth_image_views{};
        std::vector<bool> depth_image_initialized{};

        VkCommandPool command_pool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> command_buffers{};

        std::array<VkSemaphore, max_frames_in_flight> image_available{};
        std::vector<VkSemaphore> render_finished{};
        std::array<VkFence, max_frames_in_flight> in_flight_fences{};
        std::vector<VkFence> image_fences{};
        uint32_t current_frame = 0;

        bool sdl_initialized = false;
        bool active = false;
        bool validation_enabled = false;
        bool framebuffer_resized = false;
        bool force_swapchain_recreate = false;
        uint64_t last_resize_event_ms = 0;
        static constexpr uint64_t resize_settle_delay_ms = 150;

        std::vector<std::unique_ptr<VK_Sprite>> sprites{};
        std::vector<std::unique_ptr<VK_Sprite3D>> sprites3d{};
        VkDescriptorSetLayout sprite_descriptor_set_layout = VK_NULL_HANDLE;
        VkPipelineLayout sprite_pipeline_layout = VK_NULL_HANDLE;
        VkPipeline sprite_pipeline = VK_NULL_HANDLE;
        bool sprite_state_dirty = false;
        VK_Sprite *post_process_sprite = nullptr;
        bool post_process_enabled = true;
        std::vector<VkImage> post_process_images{};
        std::vector<VkDeviceMemory> post_process_memories{};
        std::vector<VkImageView> post_process_views{};
        std::vector<bool> post_process_initialized{};

        std::unique_ptr<VK_Text> text_renderer{};
        VkDescriptorSetLayout text_descriptor_set_layout = VK_NULL_HANDLE;
        VkPipelineLayout text_pipeline_layout = VK_NULL_HANDLE;
        VkPipeline text_pipeline = VK_NULL_HANDLE;
        bool text_state_dirty = false;
        bool font_configured = false;
        std::string font_path{};
        int font_size = 24;
        VkClearColorValue clear_color{{0.0f, 0.0f, 0.0f, 1.0f}};
        std::vector<VkSwapchainKHR> retired_swapchains;
    };

} // namespace mxvk

#endif
