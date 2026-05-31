#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <vector>

namespace mxvk {
    VKAPI_ATTR VkBool32 VKAPI_CALL VK_Window::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT *callback_data, [[maybe_unused]] void *user_data) {
        const char *message = (callback_data != nullptr && callback_data->pMessage != nullptr)
                                  ? callback_data->pMessage
                                  : "Unknown Vulkan validation message";

        if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0U) {
            std::cerr << std::format("vk validation error: {}\n", message);
        } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0U) {
            std::cerr << std::format("vk validation warning: {}\n", message);
        } else {
            std::cout << std::format("vk validation: {}\n", message);
        }
        return VK_FALSE;
    }

    VK_Window::SwapchainSupport VK_Window::querySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
        std::cout << "vk: querying swapchain support details\n";
        SwapchainSupport support{};

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &support.capabilities);

        uint32_t format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);
        if (format_count > 0U) {
            support.formats.resize(format_count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, support.formats.data());
        }

        uint32_t present_mode_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, nullptr);
        if (present_mode_count > 0U) {
            support.present_modes.resize(present_mode_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(
                device,
                surface,
                &present_mode_count,
                support.present_modes.data());
        }

        return support;
    }

    VkSurfaceFormatKHR VK_Window::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &available_formats) {
        for (const VkSurfaceFormatKHR &format : available_formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }

        return available_formats.front();
    }

    VkPresentModeKHR VK_Window::choosePresentMode(const std::vector<VkPresentModeKHR> &available_present_modes) {
        for (const VkPresentModeKHR present_mode : available_present_modes) {
            if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return present_mode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D VK_Window::chooseExtent(const VkSurfaceCapabilitiesKHR &capabilities, SDL_Window *window) {
        if (capabilities.currentExtent.width != UINT32_MAX) {
            return capabilities.currentExtent;
        }

        int width = 1;
        int height = 1;
        SDL_GetWindowSizeInPixels(window, &width, &height);

        VkExtent2D actual_extent{};
        actual_extent.width = std::clamp(
            static_cast<uint32_t>(std::max(width, 1)),
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width);
        actual_extent.height = std::clamp(
            static_cast<uint32_t>(std::max(height, 1)),
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height);

        return actual_extent;
    }

    std::vector<char> VK_Window::loadSpv(const std::string &path) {
        if (path.empty()) {
            throw mxvk::Exception("SPIR-V path is empty");
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw mxvk::Exception("Failed to open SPIR-V file");
        }

        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (bytes.empty()) {
            throw mxvk::Exception("SPIR-V file is empty");
        }
        if ((bytes.size() % 4U) != 0U) {
            throw mxvk::Exception("SPIR-V file size is not 4-byte aligned");
        }

        return bytes;
    }

    VkShaderModule VK_Window::createShaderModule(VkDevice device, const std::vector<char> &spv_bytes) {
        if (device == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot create shader module with a null device");
        }
        if (spv_bytes.empty() || (spv_bytes.size() % 4U) != 0U) {
            throw mxvk::Exception("Invalid SPIR-V shader data");
        }

        VkShaderModuleCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        create_info.codeSize = spv_bytes.size();
        create_info.pCode = reinterpret_cast<const uint32_t *>(spv_bytes.data());

        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &create_info, nullptr, &module) != VK_SUCCESS) {
            throw mxvk::Exception("Failed to create shader module");
        }

        return module;
    }

    VK_Window::VK_Window(const std::string &title, int width, int height, bool full, bool validiation) {
        std::cout << std::format("mxvk: starting VK_Window construction (title='{}', width={}, height={}, fullscreen={}, validation={})\n", title, width, height, full, validiation);
        SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (full) {
            std::cout << "SDL3: enabling fullscreen window flag\n";
            flags = static_cast<SDL_WindowFlags>(flags | SDL_WINDOW_FULLSCREEN);
        }

        std::cout << "mxvk: initializing window subsystem and creating SDL window\n";
        if (!initWindow(title, width, height, flags)) {
            throw mxvk::Exception("Error on init of Window");
        }

        std::cout << "mxvk: initializing Vulkan runtime and rendering resources\n";
        if (!initVulkan(validiation)) {
            release();
            throw mxvk::Exception("Error on init of Vulkan");
        }
        std::cout << "mxvk: VK_Window construction complete\n";
    }

    VK_Window::~VK_Window() {
        std::cout << "mxvk: destructor invoked, releasing resources\n";
        release();
    }

    void VK_Window::release() {
        std::cout << "mxvk: starting resource teardown\n";
        if (device != VK_NULL_HANDLE) {
            std::cout << "vk: waiting for device idle before teardown\n";
            vkDeviceWaitIdle(device);
        }

        if (in_flight != VK_NULL_HANDLE) {
            std::cout << "vk: destroying in-flight fence\n";
            vkDestroyFence(device, in_flight, nullptr);
            in_flight = VK_NULL_HANDLE;
        }

        if (!render_finished.empty()) {
            std::cout << "vk: destroying render-finished semaphores\n";
            for (VkSemaphore semaphore : render_finished) {
                if (semaphore != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device, semaphore, nullptr);
                }
            }
            render_finished.clear();
        }

        if (image_available != VK_NULL_HANDLE) {
            std::cout << "vk: destroying image-available semaphore\n";
            vkDestroySemaphore(device, image_available, nullptr);
            image_available = VK_NULL_HANDLE;
        }

        std::cout << "vk: tearing down swapchain-dependent resources\n";
        cleanupSwapchain();

        if (device != VK_NULL_HANDLE) {
            std::cout << "vk: destroying logical device\n";
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
            graphics_queue = VK_NULL_HANDLE;
            present_queue = VK_NULL_HANDLE;
        }

        graphics_queue_family = invalid_queue_index;
        present_queue_family = invalid_queue_index;
        physical_device = VK_NULL_HANDLE;

        if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
            std::cout << "vk: destroying presentation surface\n";
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }

        cleanupDebugMessenger();

        if (instance != VK_NULL_HANDLE) {
            std::cout << "vk: destroying Vulkan instance\n";
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }

        std::cout << "SDL3: destroying SDL window handle\n";
        window.reset();

        if (sdl_initialized) {
            std::cout << "SDL3: shutting down SDL video subsystem\n";
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            sdl_initialized = false;
        }

        active = false;
        std::cout << "mxvk: resource teardown complete\n";
    }

    bool VK_Window::initVulkan(bool validiation) {
        std::cout << std::format("mxvk: entering initVulkan (validation={})\n", validiation);
        validation_enabled = validiation;

        if (window == nullptr) {
            std::cerr << "mxvk: Cannot initialize Vulkan without an SDL window\n";
            return false;
        }

        if (instance != VK_NULL_HANDLE) {
            std::cout << "vk: instance already initialized; skipping initVulkan\n";
            return true;
        }

        std::cout << "vk: initializing volk loader\n";
        if (volkInitialize() != VK_SUCCESS) {
            std::cerr << "mxvk: Failed to initialize volk\n";
            return false;
        }

        unsigned int extension_count = 0;
        std::cout << "SDL3: querying Vulkan instance extensions required by SDL\n";
        const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
        if (extensions == nullptr || extension_count == 0U) {
            std::cerr << std::format("mxvk: Failed to get Vulkan instance extensions: {}\n", SDL_GetError());
            return false;
        }
        std::cout << std::format("vk: SDL provided {} required instance extension(s)\n", extension_count);

        std::vector<const char *> enabled_extensions(extensions, extensions + extension_count);

        std::vector<const char *> enabled_layers{};
        VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
        if (validation_enabled) {
            if (!hasValidationLayerSupport()) {
                std::cerr << std::format(
                    "mxvk: validation layer '{}' is not available; continuing without validation\n",
                    validation_layer_name);
                validation_enabled = false;
            } else {
                enabled_layers.push_back(validation_layer_name);
                enabled_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                const std::optional<VkDebugUtilsMessengerCreateInfoEXT> maybe_debug_create_info =
                    makeDebugMessengerCreateInfo();
                if (!maybe_debug_create_info.has_value()) {
                    std::cerr << "mxvk: failed to construct debug messenger create info\n";
                    validation_enabled = false;
                    enabled_layers.clear();
                } else {
                    debug_create_info = maybe_debug_create_info.value();
                }
            }
        }

        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "mxvk";
        app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app_info.pEngineName = "mxvk";
        app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        app_info.apiVersion = VK_API_VERSION_1_4;

        VkInstanceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &app_info;
        create_info.enabledExtensionCount = extension_count;
        create_info.ppEnabledExtensionNames = enabled_extensions.data();
        create_info.enabledLayerCount = static_cast<uint32_t>(enabled_layers.size());
        create_info.ppEnabledLayerNames = enabled_layers.empty() ? nullptr : enabled_layers.data();
        create_info.pNext = validation_enabled ? &debug_create_info : nullptr;

        create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());

        std::cout << "vk: creating Vulkan instance\n";
        if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS) {
            std::cerr << "mxvk: Failed to create Vulkan instance\n";
            instance = VK_NULL_HANDLE;
            return false;
        }

        std::cout << "vk: loading Vulkan instance function pointers via volk\n";
        volkLoadInstance(instance);

        setupDebugMessenger();

        uint32_t instanceVersion = 0;
        if (vkEnumerateInstanceVersion != nullptr) {
            vkEnumerateInstanceVersion(&instanceVersion);
        }
        std::cout << "vk: Vulkan instance version: "
                  << VK_VERSION_MAJOR(instanceVersion) << "."
                  << VK_VERSION_MINOR(instanceVersion) << "."
                  << VK_VERSION_PATCH(instanceVersion) << "\n";

        std::cout << "SDL3: creating Vulkan presentation surface from SDL window\n";
        if (!SDL_Vulkan_CreateSurface(window.get(), instance, nullptr, &surface)) {
            std::cerr << std::format("mxvk: Failed to create Vulkan surface: {}\n", SDL_GetError());
            cleanupDebugMessenger();
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
            surface = VK_NULL_HANDLE;
            return false;
        }

        std::cout << "mxvk: selecting suitable physical device\n";
        pickDevice();
        if (physical_device == VK_NULL_HANDLE) {
            std::cerr << "mxvk: Failed to find a Vulkan physical device with present support\n";
            return false;
        }

        std::cout << "mxvk: creating logical device and queues\n";
        createLogicalDevice();
        if (device == VK_NULL_HANDLE) {
            std::cerr << "mxvk: Failed to create Vulkan logical device\n";
            return false;
        }

        std::cout << "mxvk: deferring swapchain/render/sync resource creation until first frame\n";

        std::cout << "mxvk: initVulkan complete\n";
        return true;
    }

    void VK_Window::event(VK_Window *window, SDL_Event &e) {
        switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            switch (e.key.key) {
            case SDLK_ESCAPE:
                active = false;
                break;
            }
            break;
        }
    }

    void VK_Window::loop() {
        SDL_Event e;
        active = true;
        while (active) {
            while (SDL_PollEvent(&e)) {
                switch (e.type) {
                case SDL_EVENT_QUIT:
                    active = false;
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    last_resize_event_ms_ = SDL_GetTicks();
                    framebuffer_resized_ = true;
                    break;
                default:
                    break;
                }
                event(this, e);
            }
            if (framebuffer_resized_) {
                const uint64_t now_ms = SDL_GetTicks();
                if (last_resize_event_ms_ != 0 && (now_ms - last_resize_event_ms_) < resize_settle_delay_ms_) {
                    proc(this);
                    render(this);
                    SDL_Delay(1);
                    continue;
                }
                recreateSwapchain();
            }
            proc(this);
            render(this);
            SDL_Delay(1);
        }
    }

    void VK_Window::render([[maybe_unused]] VK_Window *window) {
        drawFrame();
    }

    void VK_Window::proc([[maybe_unused]] VK_Window *window) {
    }

    bool VK_Window::initWindow(const std::string &title, int width, int height, SDL_WindowFlags flags) {
        std::cout << std::format("mxvk: entering initWindow (title='{}', width={}, height={})\n", title, width, height);
        if (width <= 0 || height <= 0) {
            std::cerr << "mxvk: Window dimensions must be positive\n";
            return false;
        }

        if (!sdl_initialized) {
            std::cout << "SDL3: initializing video subsystem\n";
            if (!SDL_Init(SDL_INIT_VIDEO)) {
                std::cerr << "mxvk: Error on SDL init: " << SDL_GetError() << "\n";
                return false;
            }
            sdl_initialized = true;
            std::cout << "SDL3: video subsystem initialized\n";
        }

        std::cout << "SDL3: creating SDL window with Vulkan capability\n";
        SDL_Window *raw_window = SDL_CreateWindow(title.c_str(), width, height, flags);
        if (raw_window == nullptr) {
            std::cerr << std::format("Error creating window: {}\n", SDL_GetError());
            std::cout << "SDL3: rolling back SDL video subsystem after window creation failure\n";
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            sdl_initialized = false;
            return false;
        }

        window.reset(raw_window);
        std::cout << "SDL3: showing created window\n";
        SDL_ShowWindow(window.get());
        std::cout << "mxvk: initWindow complete\n";
        return true;
    }

    void VK_Window::exit() {
        active = false;
    }

    void VK_Window::onSwapchainAboutToRecreate() {}

    void VK_Window::onSwapchainRecreated() {}

    void VK_Window::pickDevice() {
        std::cout << "mxvk: entering pickDevice\n";
        if (instance == VK_NULL_HANDLE || surface == VK_NULL_HANDLE) {
            std::cout << "vk: cannot pick device because instance or surface is missing\n";
            return;
        }

        uint32_t device_count = 0;
        std::cout << "vk: enumerating physical devices\n";
        vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
        if (device_count == 0U) {
            std::cout << "vk: no physical devices found\n";
            return;
        }
        std::cout << std::format("vk: found {} physical device(s)\n", device_count);

        std::vector<VkPhysicalDevice> devices(device_count);
        vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

        for (const VkPhysicalDevice candidate : devices) {
            std::cout << "vk: evaluating candidate physical device\n";
            uint32_t queue_family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_family_count, nullptr);
            std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_family_count, queue_families.data());

            uint32_t candidate_graphics = invalid_queue_index;
            uint32_t candidate_present = invalid_queue_index;

            for (uint32_t i = 0; i < queue_family_count; ++i) {
                if ((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
                    candidate_graphics = i;
                }

                VkBool32 present_support = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present_support);
                if (present_support == VK_TRUE) {
                    candidate_present = i;
                }
            }

            if (candidate_graphics == invalid_queue_index || candidate_present == invalid_queue_index) {
                std::cout << "vk: candidate rejected due to missing graphics/present queue support\n";
                continue;
            }

            const SwapchainSupport swapchain_support = querySwapchainSupport(candidate, surface);
            if (swapchain_support.formats.empty() || swapchain_support.present_modes.empty()) {
                std::cout << "vk: candidate rejected due to incomplete swapchain support\n";
                continue;
            }

            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);

            const char *device_type = "unknown";
            switch (properties.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                device_type = "integrated";
                break;
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                device_type = "discrete";
                break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                device_type = "virtual";
                break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:
                device_type = "cpu";
                break;
            default:
                break;
            }

            physical_device = candidate;
            graphics_queue_family = candidate_graphics;
            present_queue_family = candidate_present;
            std::cout << std::format(
                "vk: selected GPU='{}' type={} vendor=0x{:04x} device=0x{:04x}\n",
                properties.deviceName,
                device_type,
                properties.vendorID,
                properties.deviceID);
            return;
        }

        std::cout << "vk: no suitable physical device selected\n";
    }
    void VK_Window::createLogicalDevice() {
        std::cout << "mxvk: entering createLogicalDevice\n";
        if (physical_device == VK_NULL_HANDLE) {
            std::cout << "vk: cannot create logical device without a selected physical device\n";
            return;
        }

        std::vector<uint32_t> queue_families{};
        queue_families.push_back(graphics_queue_family);
        if (present_queue_family != graphics_queue_family) {
            queue_families.push_back(present_queue_family);
        }

        constexpr float queue_priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queue_create_infos{};
        queue_create_infos.reserve(queue_families.size());
        for (const uint32_t family : queue_families) {
            VkDeviceQueueCreateInfo queue_create_info{};
            queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_info.queueFamilyIndex = family;
            queue_create_info.queueCount = 1;
            queue_create_info.pQueuePriorities = &queue_priority;
            queue_create_infos.push_back(queue_create_info);
        }
        const std::array<const char *, 1> required_device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        VkPhysicalDeviceVulkan13Features supported_vulkan13_features{};
        supported_vulkan13_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features2.pNext = &supported_vulkan13_features;
        vkGetPhysicalDeviceFeatures2(physical_device, &features2);

        std::cout << std::format(
            "vk: feature support - synchronization2={}, dynamicRendering={}\n",
            supported_vulkan13_features.synchronization2 == VK_TRUE ? "true" : "false",
            supported_vulkan13_features.dynamicRendering == VK_TRUE ? "true" : "false");

        if (supported_vulkan13_features.synchronization2 != VK_TRUE) {
            std::cout << "vk: synchronization2 is unsupported on selected physical device\n";
            return;
        }
        if (supported_vulkan13_features.dynamicRendering != VK_TRUE) {
            std::cout << "vk: dynamic rendering is unsupported on selected physical device\n";
            return;
        }
        VkPhysicalDeviceVulkan13Features vulkan13_features{};
        vulkan13_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vulkan13_features.synchronization2 = VK_TRUE;
        vulkan13_features.dynamicRendering = VK_TRUE;
        features2.pNext = &vulkan13_features;

        VkDeviceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.pNext = &features2;
        create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
        create_info.pQueueCreateInfos = queue_create_infos.data();
        create_info.enabledExtensionCount = static_cast<uint32_t>(required_device_extensions.size());
        create_info.ppEnabledExtensionNames = required_device_extensions.data();
        create_info.enabledLayerCount = 0;
        create_info.ppEnabledLayerNames = nullptr;
        create_info.pEnabledFeatures = nullptr;

        std::cout << "vk: creating logical device\n";
        if (vkCreateDevice(physical_device, &create_info, nullptr, &device) != VK_SUCCESS) {
            std::cout << "vk: logical device creation failed\n";
            device = VK_NULL_HANDLE;
            return;
        }

        std::cout << "vk: loading device-level function pointers via volk\n";
        volkLoadDevice(device);
        if (vkQueueSubmit2 == nullptr || vkCmdBeginRendering == nullptr || vkCmdEndRendering == nullptr || vkCmdPipelineBarrier2 == nullptr) {
            std::cout << "vk: required dynamic rendering/synchronization function pointers are unavailable\n";
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
            return;
        }
        std::cout << "vk: retrieving graphics and present queues\n";
        vkGetDeviceQueue(device, graphics_queue_family, 0, &graphics_queue);
        vkGetDeviceQueue(device, present_queue_family, 0, &present_queue);
        std::cout << "mxvk: createLogicalDevice complete\n";
    }

    void VK_Window::createDevice() {
        std::cout << "mxvk: entering createDevice\n";
        if (device == VK_NULL_HANDLE) {
            std::cout << "vk: skipping createDevice because logical device is null\n";
            return;
        }

        std::cout << "vk: creating swapchain\n";
        if (!createSwapchain()) {
            std::cout << "vk: createSwapchain failed\n";
            return;
        }

        std::cout << "vk: creating render resources\n";
        if (!createRenderResources()) {
            std::cout << "vk: createRenderResources failed\n";
            return;
        }

        std::cout << "vk: creating synchronization objects\n";
        if (!createSyncObjects()) {
            std::cout << "vk: createSyncObjects failed\n";
            return;
        }

        std::cout << "mxvk: createDevice complete\n";
    }

    bool VK_Window::createSwapchain() {
        std::cout << "vk: entering createSwapchain\n";
        const SwapchainSupport support = querySwapchainSupport(physical_device, surface);
        if (support.formats.empty() || support.present_modes.empty()) {
            std::cout << "vk: cannot create swapchain because support is incomplete\n";
            return false;
        }

        const VkSurfaceFormatKHR surface_format = chooseSurfaceFormat(support.formats);
        const VkPresentModeKHR present_mode = choosePresentMode(support.present_modes);
        const VkExtent2D extent = chooseExtent(support.capabilities, window.get());

        uint32_t image_count = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0U && image_count > support.capabilities.maxImageCount) {
            image_count = support.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create_info.surface = surface;
        create_info.minImageCount = image_count;
        create_info.imageFormat = surface_format.format;
        create_info.imageColorSpace = surface_format.colorSpace;
        create_info.imageExtent = extent;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        const uint32_t queue_family_indices[] = {graphics_queue_family, present_queue_family};
        if (graphics_queue_family != present_queue_family) {
            create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            create_info.queueFamilyIndexCount = 2;
            create_info.pQueueFamilyIndices = queue_family_indices;
        } else {
            create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            create_info.queueFamilyIndexCount = 0;
            create_info.pQueueFamilyIndices = nullptr;
        }

        create_info.preTransform = support.capabilities.currentTransform;
        create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        create_info.presentMode = present_mode;
        create_info.clipped = VK_TRUE;
        create_info.oldSwapchain = VK_NULL_HANDLE;

        std::cout << "vk: creating swapchain object\n";
        if (vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain) != VK_SUCCESS) {
            swapchain = VK_NULL_HANDLE;
            return false;
        }

        std::cout << "vk: querying swapchain images\n";
        vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr);
        swapchain_images.resize(image_count);
        vkGetSwapchainImagesKHR(device, swapchain, &image_count, swapchain_images.data());
        swapchain_image_initialized.assign(swapchain_images.size(), false);

        swapchain_format = surface_format.format;
        swapchain_extent = extent;

        swapchain_image_views.resize(swapchain_images.size());
        for (size_t i = 0; i < swapchain_images.size(); ++i) {
            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = swapchain_images[i];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = swapchain_format;
            view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            std::cout << std::format("vk: creating image view for swapchain image {}\n", i);
            if (vkCreateImageView(device, &view_info, nullptr, &swapchain_image_views[i]) != VK_SUCCESS) {
                return false;
            }
        }

        std::cout << "vk: createSwapchain complete\n";
        return true;
    }

    bool VK_Window::createRenderResources() {
        std::cout << "vk: entering createRenderResources\n";
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = graphics_queue_family;

        std::cout << "vk: creating command pool\n";
        if (vkCreateCommandPool(device, &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
            return false;
        }

        command_buffers.resize(swapchain_images.size());
        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = static_cast<uint32_t>(command_buffers.size());

        std::cout << "vk: allocating command buffers\n";
        if (vkAllocateCommandBuffers(device, &alloc_info, command_buffers.data()) != VK_SUCCESS) {
            return false;
        }

        std::cout << "vk: createRenderResources complete\n";
        return true;
    }

    bool VK_Window::createSyncObjects() {
        std::cout << "vk: entering createSyncObjects\n";
        if (swapchain_images.empty()) {
            std::cerr << "mxvk: cannot create sync objects without swapchain images\n";
            return false;
        }

        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        std::cout << "vk: creating image-available semaphore\n";
        if (vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available) != VK_SUCCESS) {
            return false;
        }

        render_finished.resize(swapchain_images.size(), VK_NULL_HANDLE);
        for (size_t i = 0; i < render_finished.size(); ++i) {
            std::cout << std::format("vk: creating render-finished semaphore for swapchain image {}\n", i);
            if (vkCreateSemaphore(device, &semaphore_info, nullptr, &render_finished[i]) != VK_SUCCESS) {
                for (VkSemaphore semaphore : render_finished) {
                    if (semaphore != VK_NULL_HANDLE) {
                        vkDestroySemaphore(device, semaphore, nullptr);
                    }
                }
                render_finished.clear();
                return false;
            }
        }

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        std::cout << "vk: creating in-flight fence\n";
        if (vkCreateFence(device, &fence_info, nullptr, &in_flight) != VK_SUCCESS) {
            return false;
        }

        std::cout << "vk: createSyncObjects complete\n";
        return true;
    }

    void VK_Window::cleanupSyncObjects() {
        if (device == VK_NULL_HANDLE) {
            return;
        }

        if (in_flight != VK_NULL_HANDLE) {
            std::cout << "vk: destroying in-flight fence\n";
            vkDestroyFence(device, in_flight, nullptr);
            in_flight = VK_NULL_HANDLE;
        }

        if (!render_finished.empty()) {
            std::cout << "vk: destroying render-finished semaphores\n";
            for (VkSemaphore semaphore : render_finished) {
                if (semaphore != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device, semaphore, nullptr);
                }
            }
            render_finished.clear();
        }

        if (image_available != VK_NULL_HANDLE) {
            std::cout << "vk: destroying image-available semaphore\n";
            vkDestroySemaphore(device, image_available, nullptr);
            image_available = VK_NULL_HANDLE;
        }
    }

    void VK_Window::recreateSwapchain() {
        if (device == VK_NULL_HANDLE || window == nullptr) {
            return;
        }

        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(window.get(), &w, &h);
        if (w == 0 || h == 0) {
            // Window is minimized; skip recreation until it has a real size.
            std::cout << "mxvk: resize detected while minimized; deferring swapchain recreation\n";
            return;
        }

        std::cout << std::format("mxvk: recreating swapchain for {}x{} window\n", w, h);
        vkDeviceWaitIdle(device);
        onSwapchainAboutToRecreate();
        cleanupSyncObjects();
        cleanupSwapchain();
        if (!createSwapchain() || !createRenderResources() || !createSyncObjects()) {
            std::cerr << "mxvk: failed to recreate swapchain after resize\n";
            return;
        }
        onSwapchainRecreated();
        framebuffer_resized_ = false;
        std::cout << "mxvk: swapchain recreation complete\n";
    }

    void VK_Window::cleanupSwapchain() {
        std::cout << "vk: entering cleanupSwapchain\n";
        if (device == VK_NULL_HANDLE) {
            std::cout << "vk: skipping cleanupSwapchain because logical device is null\n";
            return;
        }

        if (command_pool != VK_NULL_HANDLE && !command_buffers.empty()) {
            std::cout << "vk: freeing command buffers\n";
            vkFreeCommandBuffers(
                device,
                command_pool,
                static_cast<uint32_t>(command_buffers.size()),
                command_buffers.data());
            command_buffers.clear();
        }

        if (command_pool != VK_NULL_HANDLE) {
            std::cout << "vk: destroying command pool\n";
            vkDestroyCommandPool(device, command_pool, nullptr);
            command_pool = VK_NULL_HANDLE;
        }

        if (!swapchain_image_views.empty()) {
            std::cout << "vk: destroying swapchain image views\n";
            for (VkImageView image_view : swapchain_image_views) {
                if (image_view != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, image_view, nullptr);
                }
            }
        }
        swapchain_image_views.clear();
        swapchain_images.clear();
        swapchain_image_initialized.clear();

        if (swapchain != VK_NULL_HANDLE) {
            std::cout << "vk: destroying swapchain\n";
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
        std::cout << "vk: cleanupSwapchain complete\n";
    }

    void VK_Window::drawFrame() {
        if (device == VK_NULL_HANDLE) {
            return;
        }

        if (swapchain == VK_NULL_HANDLE || command_buffers.empty() || in_flight == VK_NULL_HANDLE ||
            image_available == VK_NULL_HANDLE || render_finished.size() != swapchain_images.size()) {
            std::cout << "mxvk: creating deferred swapchain/render/sync resources\n";
            createDevice();
            if (swapchain == VK_NULL_HANDLE || command_buffers.empty() || in_flight == VK_NULL_HANDLE ||
                image_available == VK_NULL_HANDLE || render_finished.size() != swapchain_images.size()) {
                std::cerr << "mxvk: deferred resource creation failed; skipping frame\n";
                return;
            }
        }

        if (render_finished.empty()) {
            return;
        }

        vkWaitForFences(device, 1, &in_flight, VK_TRUE, UINT64_MAX);

        uint32_t image_index = 0;
        const VkResult acquire_result =
            vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_available, VK_NULL_HANDLE, &image_index);
        if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
            framebuffer_resized_ = true;
            return;
        }
        if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
            std::cerr << "mxvk: Failed to acquire swapchain image\n";
            return;
        }

        if (image_index >= command_buffers.size() || image_index >= swapchain_images.size() || image_index >= swapchain_image_views.size()) {
            return;
        }
        if (image_index >= render_finished.size()) {
            std::cerr << "mxvk: acquired image index exceeds render-finished semaphore count\n";
            return;
        }

        const VkCommandBuffer cmd = command_buffers[image_index];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
            std::cerr << "mxvk: Failed to begin command buffer\n";
            return;
        }

        VkClearValue clear_value{};
        clear_value.color = {{0.0F, 0.0F, 0.0F, 1.0F}};

        VkImageMemoryBarrier2 to_color_barrier{};
        to_color_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        to_color_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        to_color_barrier.srcAccessMask = VK_ACCESS_2_NONE;
        to_color_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        to_color_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        to_color_barrier.oldLayout =
            swapchain_image_initialized[image_index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
        to_color_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        to_color_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_color_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_color_barrier.image = swapchain_images[image_index];
        to_color_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_color_barrier.subresourceRange.baseMipLevel = 0;
        to_color_barrier.subresourceRange.levelCount = 1;
        to_color_barrier.subresourceRange.baseArrayLayer = 0;
        to_color_barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo pre_render_dependency{};
        pre_render_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        pre_render_dependency.imageMemoryBarrierCount = 1;
        pre_render_dependency.pImageMemoryBarriers = &to_color_barrier;
        vkCmdPipelineBarrier2(cmd, &pre_render_dependency);

        VkRenderingAttachmentInfo color_attachment{};
        color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attachment.imageView = swapchain_image_views[image_index];
        color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
        color_attachment.resolveImageView = VK_NULL_HANDLE;
        color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.clearValue = clear_value;

        VkRenderingInfo rendering_info{};
        rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering_info.renderArea.offset = {0, 0};
        rendering_info.renderArea.extent = swapchain_extent;
        rendering_info.layerCount = 1;
        rendering_info.viewMask = 0;
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachments = &color_attachment;
        rendering_info.pDepthAttachment = nullptr;
        rendering_info.pStencilAttachment = nullptr;

        vkCmdBeginRendering(cmd, &rendering_info);
        vkCmdEndRendering(cmd);

        VkImageMemoryBarrier2 to_present_barrier{};
        to_present_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        to_present_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        to_present_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        to_present_barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        to_present_barrier.dstAccessMask = VK_ACCESS_2_NONE;
        to_present_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        to_present_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        to_present_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present_barrier.image = swapchain_images[image_index];
        to_present_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_present_barrier.subresourceRange.baseMipLevel = 0;
        to_present_barrier.subresourceRange.levelCount = 1;
        to_present_barrier.subresourceRange.baseArrayLayer = 0;
        to_present_barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo post_render_dependency{};
        post_render_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        post_render_dependency.imageMemoryBarrierCount = 1;
        post_render_dependency.pImageMemoryBarriers = &to_present_barrier;
        vkCmdPipelineBarrier2(cmd, &post_render_dependency);

        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
            std::cerr << "mxvk: Failed to end command buffer\n";
            return;
        }

        const VkSemaphore signal_semaphore = render_finished[image_index];

        VkSemaphoreSubmitInfo wait_semaphore_info{};
        wait_semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        wait_semaphore_info.semaphore = image_available;
        wait_semaphore_info.value = 0;
        wait_semaphore_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        wait_semaphore_info.deviceIndex = 0;

        VkCommandBufferSubmitInfo command_buffer_info{};
        command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        command_buffer_info.commandBuffer = cmd;
        command_buffer_info.deviceMask = 0;

        VkSemaphoreSubmitInfo signal_semaphore_info{};
        signal_semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signal_semaphore_info.semaphore = signal_semaphore;
        signal_semaphore_info.value = 0;
        signal_semaphore_info.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
        signal_semaphore_info.deviceIndex = 0;

        VkSubmitInfo2 submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit_info.waitSemaphoreInfoCount = 1;
        submit_info.pWaitSemaphoreInfos = &wait_semaphore_info;
        submit_info.commandBufferInfoCount = 1;
        submit_info.pCommandBufferInfos = &command_buffer_info;
        submit_info.signalSemaphoreInfoCount = 1;
        submit_info.pSignalSemaphoreInfos = &signal_semaphore_info;

        vkResetFences(device, 1, &in_flight);
        if (vkQueueSubmit2(graphics_queue, 1, &submit_info, in_flight) != VK_SUCCESS) {
            std::cerr << "mxvk: Failed to submit draw command\n";
            return;
        }
        swapchain_image_initialized[image_index] = true;

        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &signal_semaphore;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain;
        present_info.pImageIndices = &image_index;

        const VkResult present_result = vkQueuePresentKHR(present_queue, &present_info);
        if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
            framebuffer_resized_ = true;
        } else if (present_result != VK_SUCCESS) {
            std::cerr << "mxvk: Failed to present swapchain image\n";
        }
    }

    bool VK_Window::validationEnabled() const {
        return validation_enabled;
    }

    bool VK_Window::hasValidationLayerSupport() {
        uint32_t layer_count = 0;
        const VkResult count_result = vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
        if (count_result != VK_SUCCESS) {
            std::cerr << std::format(
                "mxvk: Failed to query validation layer count (VkResult={})\n",
                static_cast<int>(count_result));
            return false;
        }

        if (layer_count == 0U) {
            return false;
        }

        std::vector<VkLayerProperties> available_layers(layer_count);
        const VkResult layers_result = vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
        if (layers_result != VK_SUCCESS) {
            std::cerr << std::format(
                "mxvk: Failed to enumerate validation layers (VkResult={})\n",
                static_cast<int>(layers_result));
            return false;
        }

        return std::ranges::any_of(
            available_layers,
            [](const VkLayerProperties &layer) {
                return std::strcmp(layer.layerName, validation_layer_name) == 0;
            });
    }

    std::optional<VkDebugUtilsMessengerCreateInfoEXT> VK_Window::makeDebugMessengerCreateInfo() {
        VkDebugUtilsMessengerCreateInfoEXT create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        create_info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        create_info.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        create_info.pfnUserCallback = debugCallback;
        create_info.pUserData = nullptr;
        return create_info;
    }

    void VK_Window::setupDebugMessenger() {
        if (!validation_enabled || instance == VK_NULL_HANDLE) {
            return;
        }

        const std::optional<VkDebugUtilsMessengerCreateInfoEXT> maybe_create_info = makeDebugMessengerCreateInfo();
        if (!maybe_create_info.has_value()) {
            std::cerr << "mxvk: unable to build debug messenger create info\n";
            return;
        }

        const VkResult result = vkCreateDebugUtilsMessengerEXT(
            instance,
            &maybe_create_info.value(),
            nullptr,
            &debug_messenger);
        if (result != VK_SUCCESS) {
            std::cerr << "mxvk: failed to create Vulkan debug messenger\n";
            debug_messenger = VK_NULL_HANDLE;
        }
    }

    void VK_Window::cleanupDebugMessenger() {
        if (debug_messenger != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
            std::cout << "vk: destroying debug messenger\n";
            vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
            debug_messenger = VK_NULL_HANDLE;
        }
    }
} // namespace mxvk