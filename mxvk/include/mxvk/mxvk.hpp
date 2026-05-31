#ifndef _MXVK_MXVK_H_
#define _MXVK_MXVK_H_

#include <SDL3/SDL.h>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <volk/volk.h>

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

        /**
         * @brief Initialize Vulkan state.
         * @param validiation Enables validation-related behavior when true.
         * @return true on success, false otherwise.
         */
        virtual bool initVulkan(bool validiation);

        /**
         * @brief Handle one SDL event.
         * @param window Current window instance pointer.
         * @param e SDL event to process.
         */
        virtual void event(VK_Window *window, SDL_Event &e);

        /**
         * @brief Run the main event/render loop.
         */
        void loop();

        /**
         * @brief Render one frame.
         * @param window Current window instance pointer.
         */
        virtual void render(VK_Window *window);

        /**
         * @brief Execute one processing/update step.
         * @param window Current window instance pointer.
         */
        virtual void proc(VK_Window *window);

        /**
         * @brief Check whether Vulkan validation layers are currently enabled.
         * @return true if validation layers are enabled, false otherwise.
         */
        [[nodiscard]] bool validationEnabled() const;

        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data);

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

      private:
        static constexpr uint32_t invalid_queue_index = std::numeric_limits<uint32_t>::max();

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
        bool createSwapchain();
        bool createRenderResources();
        bool createSyncObjects();
        void cleanupSyncObjects();
        void cleanupSwapchain();
        void recreateSwapchain();
        void drawFrame();

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
        VkExtent2D swapchain_extent{};
        std::vector<VkImage> swapchain_images{};
        std::vector<VkImageView> swapchain_image_views{};
        std::vector<bool> swapchain_image_initialized{};

        VkCommandPool command_pool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> command_buffers{};

        VkSemaphore image_available = VK_NULL_HANDLE;
        std::vector<VkSemaphore> render_finished{};
        VkFence in_flight = VK_NULL_HANDLE;

        bool sdl_initialized = false;
        bool active = false;
        bool validation_enabled = false;
        bool framebuffer_resized_ = false;
        uint64_t last_resize_event_ms_ = 0;
        static constexpr uint64_t resize_settle_delay_ms_ = 150;
    };

} // namespace mxvk

#endif