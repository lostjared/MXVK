#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_png.hpp"
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <vector>

#ifndef MXVK_SPRITE_SHADER_DIR
#define MXVK_SPRITE_SHADER_DIR "."
#endif

#ifndef MXVK_TEXT_SHADER_DIR
#define MXVK_TEXT_SHADER_DIR "."
#endif

#ifndef MXVK_DEFAULT_FONT_DIR
#define MXVK_DEFAULT_FONT_DIR "."
#endif

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

    VkPresentModeKHR VK_Window::choosePresentMode(const std::vector<VkPresentModeKHR> &available_present_modes) const {
        if (present_mode_preference == PresentModePreference::Vsync) {
            return VK_PRESENT_MODE_FIFO_KHR;
        }

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

    std::string VK_Window::resolveRuntimeShaderPath(const std::string &shaderFileName, const char *fallbackDir) const {
        if (shaderFileName.empty()) {
            throw mxvk::Exception("Shader file name is empty");
        }

        std::vector<std::filesystem::path> candidates{};

        if (const char *basePath = SDL_GetBasePath(); basePath != nullptr) {
            const std::filesystem::path executableDir(basePath);
            candidates.push_back(executableDir / "data" / shaderFileName);
            candidates.push_back(executableDir / shaderFileName);
        }

        candidates.push_back(std::filesystem::path("data") / shaderFileName);

        if (!font_path.empty()) {
            const std::filesystem::path fontPath(font_path);
            if (fontPath.has_parent_path()) {
                candidates.push_back(fontPath.parent_path() / shaderFileName);
            }
        }

        if (fallbackDir != nullptr && fallbackDir[0] != '\0') {
            candidates.push_back(std::filesystem::path(fallbackDir) / shaderFileName);
        }

        std::error_code existsError{};
        for (const std::filesystem::path &candidate : candidates) {
            if (std::filesystem::exists(candidate, existsError)) {
                return candidate.string();
            }
            existsError.clear();
        }

        throw mxvk::Exception(std::format("Failed to locate shader file '{}'", shaderFileName));
    }

    VK_Window::VK_Window(const std::string &title, int width, int height, bool full, bool validiation, PresentModePreference presentModePreference) : present_mode_preference(presentModePreference) {
        setEnableScreenshot(defaultEnableScreenshot());
        screenshot_prefix = defaultExecutableName();
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
        showCursor(!full);

        std::cout << "mxvk: initializing Vulkan runtime and rendering resources\n";
        if (!initVulkan(validiation)) {
            release();
            throw mxvk::Exception("Error on init of Vulkan");
        }
        std::cout << "mxvk: VK_Window construction complete\n";
    }

    VK_Window::VK_Window(const std::string &title, int width, int height, bool full, bool validiation, bool enableVsync)
        : VK_Window(title, width, height, full, validiation, enableVsync ? PresentModePreference::Vsync : PresentModePreference::LowLatency) {}

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

        post_process_sprite = nullptr;
        owned_post_process_sprite = nullptr;
        post_process_sprites.clear();
        owned_post_process_sprites.clear();
        post_process_effect_params.clear();
        post_process_effect_time_enabled.clear();
        post_process_effect_start_times.clear();

        if (!sprites.empty()) {
            std::cout << std::format("vk: releasing {} sprite(s)\n", sprites.size());
            sprites.clear();
        }
        if (!sprites3d.empty()) {
            std::cout << std::format("vk: releasing {} 3D sprite batch(es)\n", sprites3d.size());
            sprites3d.clear();
        }
        sprite_state_dirty = false;
        destroySpritePipeline();
        if (sprite_descriptor_set_layout != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, sprite_descriptor_set_layout, nullptr);
            sprite_descriptor_set_layout = VK_NULL_HANDLE;
        }

        if (text_renderer) {
            text_renderer.reset();
        }
        text_state_dirty = false;
        destroyTextPipeline();
        if (text_descriptor_set_layout != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, text_descriptor_set_layout, nullptr);
            text_descriptor_set_layout = VK_NULL_HANDLE;
        }

        cleanupSyncObjects();

        if (!retired_swapchains.empty()) {
            for (VkSwapchainKHR retired : retired_swapchains) {
                vkDestroySwapchainKHR(device, retired, nullptr);
            }
            retired_swapchains.clear();
        }
        std::cout << "vk: tearing down swapchain-dependent resources\n";
        cleanupSwapchain(true);

        if (command_pool != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
            std::cout << "vk: destroying command pool\n";
            vkDestroyCommandPool(device, command_pool, nullptr);
            command_pool = VK_NULL_HANDLE;
        }

        if (device != VK_NULL_HANDLE) {
            savePipelineCache();
            destroyPipelineCache();
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
            std::cout << "SDL3: shutting down SDL video/gamepad subsystems\n";
            SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
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

#if defined(MXVK_USE_MOLTENVK)
        const auto append_instance_extension_if_missing = [&enabled_extensions](const char *extension_name) {
            const bool exists = std::ranges::any_of(
                enabled_extensions,
                [extension_name](const char *existing) { return std::strcmp(existing, extension_name) == 0; });
            if (!exists) {
                enabled_extensions.push_back(extension_name);
            }
        };
        append_instance_extension_if_missing(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        append_instance_extension_if_missing(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

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
        app_info.engineVersion = VK_MAKE_VERSION(MXVK_VERSION_CODE_MAJOR, MXVK_VERSION_CODE_MINOR, MXVK_VERSION_CODE_PATCH);
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
#if defined(MXVK_USE_MOLTENVK)
        create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

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
        std::cout << "mxvk: engine version: "
                  << MXVK_VERSION_CODE_MAJOR << "."
                  << MXVK_VERSION_CODE_MINOR << "."
                  << MXVK_VERSION_CODE_PATCH << "\n";

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
        createPipelineCache();

        std::cout << "mxvk: deferring swapchain/render/sync resource creation until first frame\n";

        std::cout << "mxvk: initVulkan complete\n";
        return true;
    }

    std::string VK_Window::pipelineCachePath() const {
        if (physical_device == VK_NULL_HANDLE) {
            return {};
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_device, &properties);

        char *pref_path = SDL_GetPrefPath("mxvk", "MXVK");
        std::filesystem::path base_path;
        if (pref_path != nullptr) {
            base_path = pref_path;
            SDL_free(pref_path);
        } else {
            base_path = std::filesystem::current_path();
        }

        std::error_code ec;
        std::filesystem::create_directories(base_path, ec);
        if (ec) {
            return {};
        }

        std::ostringstream filename;
        filename << "pipeline_cache_"
                 << std::hex << std::setfill('0')
                 << properties.vendorID << '_'
                 << properties.deviceID << '_'
                 << properties.driverVersion << '_';
        for (uint8_t byte : properties.pipelineCacheUUID) {
            filename << std::setw(2) << static_cast<unsigned>(byte);
        }
        filename << ".bin";

        return (base_path / filename.str()).string();
    }

    void VK_Window::createPipelineCache() {
        if (device == VK_NULL_HANDLE || pipeline_cache != VK_NULL_HANDLE) {
            return;
        }

        const std::string cache_path = pipelineCachePath();
        std::vector<char> initial_data{};
        if (!cache_path.empty() && std::filesystem::exists(cache_path)) {
            std::ifstream file(cache_path, std::ios::binary);
            if (file) {
                initial_data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            }
        }

        VkPipelineCacheCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        create_info.initialDataSize = initial_data.size();
        create_info.pInitialData = initial_data.empty() ? nullptr : initial_data.data();

        VkResult result = vkCreatePipelineCache(device, &create_info, nullptr, &pipeline_cache);
        if (result != VK_SUCCESS && !initial_data.empty()) {
            std::cerr << "vk: cached pipeline data rejected; creating an empty pipeline cache\n";
            create_info.initialDataSize = 0;
            create_info.pInitialData = nullptr;
            result = vkCreatePipelineCache(device, &create_info, nullptr, &pipeline_cache);
        }

        if (result != VK_SUCCESS) {
            pipeline_cache = VK_NULL_HANDLE;
            std::cerr << std::format("vk: failed to create pipeline cache ({})\n", static_cast<int>(result));
            return;
        }

        if (!initial_data.empty()) {
            std::cout << std::format("vk: loaded pipeline cache: {} bytes\n", initial_data.size());
        }
    }

    void VK_Window::savePipelineCache() const {
        if (device == VK_NULL_HANDLE || pipeline_cache == VK_NULL_HANDLE) {
            return;
        }

        size_t data_size = 0;
        VkResult result = vkGetPipelineCacheData(device, pipeline_cache, &data_size, nullptr);
        if (result != VK_SUCCESS || data_size == 0) {
            return;
        }

        std::vector<char> data(data_size);
        result = vkGetPipelineCacheData(device, pipeline_cache, &data_size, data.data());
        if (result != VK_SUCCESS || data_size == 0) {
            return;
        }
        data.resize(data_size);

        const std::string cache_path = pipelineCachePath();
        if (cache_path.empty()) {
            return;
        }

        std::ofstream file(cache_path, std::ios::binary | std::ios::trunc);
        if (!file) {
            std::cerr << std::format("vk: failed to open pipeline cache for writing: {}\n", cache_path);
            return;
        }

        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (file) {
            std::cout << std::format("vk: saved pipeline cache: {} bytes\n", data.size());
        }
    }

    void VK_Window::destroyPipelineCache() {
        if (device != VK_NULL_HANDLE && pipeline_cache != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(device, pipeline_cache, nullptr);
        }
        pipeline_cache = VK_NULL_HANDLE;
    }

    void VK_Window::event(SDL_Event &e) {
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
    void VK_Window::setClearColor(float r, float g, float b, float a) {
        clear_color.float32[0] = std::clamp(r, 0.0f, 1.0f);
        clear_color.float32[1] = std::clamp(g, 0.0f, 1.0f);
        clear_color.float32[2] = std::clamp(b, 0.0f, 1.0f);
        clear_color.float32[3] = std::clamp(a, 0.0f, 1.0f);
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
                    if (window != nullptr) {
                        framebuffer_resized = true;
                        last_resize_event_ms = SDL_GetTicks();
                        force_swapchain_recreate = false;
                    }
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if ((e.key.key == SDLK_F12 || e.key.scancode == SDL_SCANCODE_F12) && !e.key.repeat) {
                        toggleFpsCounter();
                    }
                    if (screenshot_enabled &&
                        (e.key.key == SDLK_F10 || e.key.scancode == SDL_SCANCODE_F10) &&
                        !e.key.repeat) {
                        try {
                            saveScreenshot();
                        } catch (const mxvk::Exception &ex) {
                            std::cerr << std::format("mxvk: screenshot failed: {}\n", ex.text());
                        } catch (const std::exception &ex) {
                            std::cerr << std::format("mxvk: screenshot failed: {}\n", ex.what());
                        }
                        continue;
                    }
                    break;
                default:
                    break;
                }
                event(e);
            }
            if (framebuffer_resized) {
                const uint64_t now_ms = SDL_GetTicks();
                if (!force_swapchain_recreate &&
                    last_resize_event_ms != 0 &&
                    (now_ms - last_resize_event_ms) < resize_settle_delay_ms) {
                    proc();
                    render();
                    SDL_Delay(1);
                    continue;
                }
                recreateSwapchain();
            }
            proc();
            render();
            maybeTrimMemory();
        }
    }

    void VK_Window::saveScreenshot() {
        const std::string path = makeScreenshotPath();
        saveSnapshot(path);
        std::cout << std::format("mxvk: screenshot saved: {}\n", path);
    }

    std::string VK_Window::makeScreenshotPath() {
        const char *home = std::getenv("HOME");
        std::filesystem::path pictures_dir = (home != nullptr && home[0] != '\0')
                                                 ? std::filesystem::path(home) / "Pictures"
                                                 : std::filesystem::path("Pictures");
        std::filesystem::create_directories(pictures_dir);

        const std::time_t now = std::time(nullptr);
        std::tm local_time{};
#if defined(_WIN32)
        localtime_s(&local_time, &now);
#else
        localtime_r(&now, &local_time);
#endif

        std::ostringstream date_stream;
        date_stream << std::put_time(&local_time, "%Y.%m.%d");
        std::ostringstream time_stream;
        time_stream << std::put_time(&local_time, "%H.%M.%S");

        const std::string prefix = std::format("{}.screenshot.{}.{}.{}x{}-",
                                               screenshot_prefix.empty() ? "mxvk" : screenshot_prefix,
                                               date_stream.str(),
                                               time_stream.str(),
                                               swapchain_extent.width,
                                               swapchain_extent.height);

        for (uint32_t attempt = 0; attempt < 10000U; ++attempt) {
            const uint32_t index = screenshot_index++;
            std::filesystem::path path = pictures_dir / std::format("{}{}.png", prefix, index);
            if (!std::filesystem::exists(path)) {
                return path.string();
            }
        }

        return (pictures_dir / std::format("{}{}.png", prefix, screenshot_index++)).string();
    }

    void VK_Window::render() {
        drawFrame();
    }

    void VK_Window::saveSnapshot(const std::string &path) {
        if (path.empty()) {
            throw mxvk::Exception("saveSnapshot requires a non-empty output path");
        }

        std::vector<std::uint8_t> rgba{};
        uint32_t width = 0;
        uint32_t height = 0;
        captureSnapshotPixels(rgba, width, height);

        if (!mxvk::SavePNG_RGBA(path.c_str(),
                                rgba.data(),
                                static_cast<int>(width),
                                static_cast<int>(height))) {
            throw mxvk::Exception("saveSnapshot failed to write PNG: " + path);
        }
    }

    void VK_Window::captureSnapshotPixels(std::vector<std::uint8_t> &rgba_pixels, uint32_t &width, uint32_t &height) {
        if (device == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE || graphics_queue == VK_NULL_HANDLE) {
            throw mxvk::Exception("captureSnapshotPixels called before Vulkan render resources are ready");
        }
        if (!swapchain_supports_transfer_src) {
            throw mxvk::Exception("captureSnapshotPixels requires swapchain transfer-source support");
        }
        if (last_presented_image_index == invalid_queue_index ||
            last_presented_image_index >= swapchain_images.size() ||
            last_presented_image_index >= image_fences.size()) {
            throw mxvk::Exception("captureSnapshotPixels called before a frame has been presented");
        }
        if (swapchain_extent.width == 0U || swapchain_extent.height == 0U) {
            throw mxvk::Exception("captureSnapshotPixels cannot capture an empty swapchain extent");
        }

        const bool format_is_bgra =
            swapchain_format == VK_FORMAT_B8G8R8A8_UNORM ||
            swapchain_format == VK_FORMAT_B8G8R8A8_SRGB;
        const bool format_is_rgba =
            swapchain_format == VK_FORMAT_R8G8B8A8_UNORM ||
            swapchain_format == VK_FORMAT_R8G8B8A8_SRGB;
        if (!format_is_bgra && !format_is_rgba) {
            throw mxvk::Exception(std::format("captureSnapshotPixels unsupported swapchain format: {}", static_cast<int>(swapchain_format)));
        }

        const VkDeviceSize row_bytes = static_cast<VkDeviceSize>(swapchain_extent.width) * 4U;
        const VkDeviceSize image_bytes = row_bytes * static_cast<VkDeviceSize>(swapchain_extent.height);

        VkBuffer readback_buffer = VK_NULL_HANDLE;
        VkDeviceMemory readback_memory = VK_NULL_HANDLE;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        VkFence copy_fence = VK_NULL_HANDLE;

        auto cleanup = [&]() {
            if (copy_fence != VK_NULL_HANDLE) {
                vkDestroyFence(device, copy_fence, nullptr);
            }
            if (command_buffer != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
            }
            if (readback_buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, readback_buffer, nullptr);
            }
            if (readback_memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, readback_memory, nullptr);
            }
        };

        try {
            VkBufferCreateInfo buffer_info{};
            buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer_info.size = image_bytes;
            buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(device, &buffer_info, nullptr, &readback_buffer) != VK_SUCCESS) {
                throw mxvk::Exception("captureSnapshotPixels failed to create readback buffer");
            }

            VkMemoryRequirements memory_requirements{};
            vkGetBufferMemoryRequirements(device, readback_buffer, &memory_requirements);

            VkPhysicalDeviceMemoryProperties memory_properties{};
            vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
            uint32_t memory_type_index = invalid_queue_index;
            constexpr VkMemoryPropertyFlags required_properties =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
                const bool type_matches = (memory_requirements.memoryTypeBits & (1U << i)) != 0U;
                const bool properties_match =
                    (memory_properties.memoryTypes[i].propertyFlags & required_properties) == required_properties;
                if (type_matches && properties_match) {
                    memory_type_index = i;
                    break;
                }
            }
            if (memory_type_index == invalid_queue_index) {
                throw mxvk::Exception("captureSnapshotPixels failed to find host-visible coherent memory");
            }

            VkMemoryAllocateInfo allocation_info{};
            allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation_info.allocationSize = memory_requirements.size;
            allocation_info.memoryTypeIndex = memory_type_index;
            if (vkAllocateMemory(device, &allocation_info, nullptr, &readback_memory) != VK_SUCCESS) {
                throw mxvk::Exception("captureSnapshotPixels failed to allocate readback memory");
            }
            if (vkBindBufferMemory(device, readback_buffer, readback_memory, 0) != VK_SUCCESS) {
                throw mxvk::Exception("captureSnapshotPixels failed to bind readback memory");
            }

            VkCommandBufferAllocateInfo command_buffer_info{};
            command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            command_buffer_info.commandPool = command_pool;
            command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            command_buffer_info.commandBufferCount = 1;
            if (vkAllocateCommandBuffers(device, &command_buffer_info, &command_buffer) != VK_SUCCESS) {
                throw mxvk::Exception("captureSnapshotPixels failed to allocate command buffer");
            }

            VkFenceCreateInfo fence_info{};
            fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            if (vkCreateFence(device, &fence_info, nullptr, &copy_fence) != VK_SUCCESS) {
                throw mxvk::Exception("captureSnapshotPixels failed to create copy fence");
            }

            VkFence source_fence = image_fences[last_presented_image_index];
            if (source_fence != VK_NULL_HANDLE) {
                const VkResult wait_result = vkWaitForFences(device, 1, &source_fence, VK_TRUE, UINT64_MAX);
                if (wait_result != VK_SUCCESS) {
                    throw mxvk::Exception(std::format("captureSnapshotPixels failed waiting for rendered frame: {}", static_cast<int>(wait_result)));
                }
            }

            const VkResult idle_result = vkDeviceWaitIdle(device);
            if (idle_result != VK_SUCCESS) {
                throw mxvk::Exception(std::format("captureSnapshotPixels failed waiting for device idle: {}", static_cast<int>(idle_result)));
            }

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
                throw mxvk::Exception("captureSnapshotPixels failed to begin copy command buffer");
            }

            VkImageMemoryBarrier2 to_transfer_barrier{};
            to_transfer_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            to_transfer_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            to_transfer_barrier.srcAccessMask = VK_ACCESS_2_NONE;
            to_transfer_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            to_transfer_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            to_transfer_barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            to_transfer_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            to_transfer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_transfer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_transfer_barrier.image = swapchain_images[last_presented_image_index];
            to_transfer_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            to_transfer_barrier.subresourceRange.baseMipLevel = 0;
            to_transfer_barrier.subresourceRange.levelCount = 1;
            to_transfer_barrier.subresourceRange.baseArrayLayer = 0;
            to_transfer_barrier.subresourceRange.layerCount = 1;

            VkDependencyInfo to_transfer_dependency{};
            to_transfer_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            to_transfer_dependency.imageMemoryBarrierCount = 1;
            to_transfer_dependency.pImageMemoryBarriers = &to_transfer_barrier;
            vkCmdPipelineBarrier2(command_buffer, &to_transfer_dependency);

            VkBufferImageCopy copy_region{};
            copy_region.bufferOffset = 0;
            copy_region.bufferRowLength = 0;
            copy_region.bufferImageHeight = 0;
            copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy_region.imageSubresource.mipLevel = 0;
            copy_region.imageSubresource.baseArrayLayer = 0;
            copy_region.imageSubresource.layerCount = 1;
            copy_region.imageOffset = {0, 0, 0};
            copy_region.imageExtent = {swapchain_extent.width, swapchain_extent.height, 1};
            vkCmdCopyImageToBuffer(command_buffer,
                                   swapchain_images[last_presented_image_index],
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   readback_buffer,
                                   1,
                                   &copy_region);

            VkImageMemoryBarrier2 to_present_barrier{};
            to_present_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            to_present_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            to_present_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            to_present_barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
            to_present_barrier.dstAccessMask = VK_ACCESS_2_NONE;
            to_present_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            to_present_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            to_present_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_present_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_present_barrier.image = swapchain_images[last_presented_image_index];
            to_present_barrier.subresourceRange = to_transfer_barrier.subresourceRange;

            VkDependencyInfo to_present_dependency{};
            to_present_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            to_present_dependency.imageMemoryBarrierCount = 1;
            to_present_dependency.pImageMemoryBarriers = &to_present_barrier;
            vkCmdPipelineBarrier2(command_buffer, &to_present_dependency);

            if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
                throw mxvk::Exception("captureSnapshotPixels failed to end copy command buffer");
            }

            VkCommandBufferSubmitInfo command_submit_info{};
            command_submit_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            command_submit_info.commandBuffer = command_buffer;

            VkSubmitInfo2 submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit_info.commandBufferInfoCount = 1;
            submit_info.pCommandBufferInfos = &command_submit_info;

            const VkResult submit_result = vkQueueSubmit2(graphics_queue, 1, &submit_info, copy_fence);
            if (submit_result != VK_SUCCESS) {
                throw mxvk::Exception(std::format("captureSnapshotPixels failed to submit copy command: {}", static_cast<int>(submit_result)));
            }
            const VkResult copy_wait_result = vkWaitForFences(device, 1, &copy_fence, VK_TRUE, UINT64_MAX);
            if (copy_wait_result != VK_SUCCESS) {
                throw mxvk::Exception(std::format("captureSnapshotPixels failed waiting for copy: {}", static_cast<int>(copy_wait_result)));
            }

            void *mapped = nullptr;
            if (vkMapMemory(device, readback_memory, 0, image_bytes, 0, &mapped) != VK_SUCCESS) {
                throw mxvk::Exception("captureSnapshotPixels failed to map readback memory");
            }

            const auto *src = static_cast<const std::uint8_t *>(mapped);
            rgba_pixels.resize(static_cast<std::size_t>(image_bytes));
            for (std::size_t i = 0; i < rgba_pixels.size(); i += 4U) {
                if (format_is_bgra) {
                    rgba_pixels[i + 0U] = src[i + 2U];
                    rgba_pixels[i + 1U] = src[i + 1U];
                    rgba_pixels[i + 2U] = src[i + 0U];
                    rgba_pixels[i + 3U] = src[i + 3U];
                } else {
                    rgba_pixels[i + 0U] = src[i + 0U];
                    rgba_pixels[i + 1U] = src[i + 1U];
                    rgba_pixels[i + 2U] = src[i + 2U];
                    rgba_pixels[i + 3U] = src[i + 3U];
                }
            }
            vkUnmapMemory(device, readback_memory);
            width = swapchain_extent.width;
            height = swapchain_extent.height;
        } catch (...) {
            cleanup();
            throw;
        }

        cleanup();
    }

    void VK_Window::proc() {
    }

    void VK_Window::trimMemory() {
        if (device == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE || vkTrimCommandPool == nullptr) {
            return;
        }

        vkTrimCommandPool(device, command_pool, 0);
    }

    void VK_Window::maybeTrimMemory() {
        const auto now = std::chrono::steady_clock::now();
        if ((now - last_memory_trim_time) < MEMORY_TRIM_INTERVAL) {
            return;
        }

        trimMemory();
        last_memory_trim_time = now;
    }

    bool VK_Window::initWindow(const std::string &title, int width, int height, SDL_WindowFlags flags) {
        std::cout << std::format("mxvk: entering initWindow (title='{}', width={}, height={})\n", title, width, height);
        if (width <= 0 || height <= 0) {
            std::cerr << "mxvk: Window dimensions must be positive\n";
            return false;
        }

        if (!sdl_initialized) {
            constexpr SDL_InitFlags sdlInitFlags = SDL_INIT_VIDEO | SDL_INIT_GAMEPAD;
            std::cout << "SDL3: initializing video/gamepad subsystems\n";
            if (!SDL_Init(sdlInitFlags)) {
                std::cerr << "mxvk: Error on SDL init: " << SDL_GetError() << "\n";
                return false;
            }
            sdl_initialized = true;
            SDL_SetGamepadEventsEnabled(true);
            SDL_SetJoystickEventsEnabled(true);
            std::cout << "SDL3: video/gamepad subsystems initialized\n";
        }

        std::cout << "SDL3: creating SDL window with Vulkan capability\n";
        SDL_Window *raw_window = SDL_CreateWindow(title.c_str(), width, height, flags);
        if (raw_window == nullptr) {
            std::cerr << std::format("Error creating window: {}\n", SDL_GetError());
            std::cout << "SDL3: rolling back SDL video/gamepad subsystems after window creation failure\n";
            SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
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

    void VK_Window::onPrepareFrameRendering([[maybe_unused]] VkCommandBuffer cmd, [[maybe_unused]] uint32_t image_index) {}

    void VK_Window::onRecordCustomRendering([[maybe_unused]] VkCommandBuffer cmd, [[maybe_unused]] uint32_t image_index) {}

    void VK_Window::renderStandaloneSprite(VK_Sprite &sprite, VkCommandBuffer cmd) {
        if (device == VK_NULL_HANDLE || swapchain_extent.width == 0U || swapchain_extent.height == 0U) {
            return;
        }

        if (sprite_pipeline == VK_NULL_HANDLE) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline);
        sprite.renderSprites(cmd, sprite_pipeline_layout, swapchain_extent.width, swapchain_extent.height);
    }

    VK_Sprite *VK_Window::attachPostProcessingShader(const std::string &fragmentShaderPath, float p1, float p2, float p3, float p4) {
        const std::vector<VK_Sprite *> sprites = attachPostProcessingShaders({PostProcessingEffect{fragmentShaderPath, {p1, p2, p3, p4}, false}});
        return sprites.empty() ? nullptr : sprites.front();
    }

    std::vector<VK_Sprite *> VK_Window::attachPostProcessingShaders(const std::vector<PostProcessingEffect> &effects) {
        detachPostProcessingShader();

        std::vector<VK_Sprite *> attachedSprites;
        attachedSprites.reserve(effects.size());
        for (const PostProcessingEffect &effect : effects) {
            if (effect.fragmentShaderPath.empty()) {
                throw mxvk::Exception("Cannot attach post-processing shader with an empty fragment shader path");
            }

            VK_Sprite *sprite = createSprite(
                1,
                1,
                resolveRuntimeShaderPath("sprite.vert.spv", MXVK_SPRITE_SHADER_DIR),
                effect.fragmentShaderPath);
            const uint32_t black_pixel = 0xFF000000u;
            sprite->updateTexture(&black_pixel, 1, 1);
            sprite->setShaderParams(effect.params[0], effect.params[1], effect.params[2], effect.params[3]);

            attachedSprites.push_back(sprite);
            owned_post_process_sprites.push_back(sprite);
            post_process_sprites.push_back(sprite);
            post_process_effect_params.push_back(effect.params);
            post_process_effect_time_enabled.push_back(effect.timeEnabled);
            post_process_effect_start_times.push_back(std::chrono::steady_clock::now());
        }

        post_process_sprite = attachedSprites.empty() ? nullptr : attachedSprites.front();
        owned_post_process_sprite = post_process_sprite;
        post_process_params = post_process_effect_params.empty() ? std::array<float, 4>{} : post_process_effect_params.front();
        post_process_time_enabled = !post_process_effect_time_enabled.empty() && post_process_effect_time_enabled.front();
        post_process_start_time = post_process_effect_start_times.empty() ? std::chrono::steady_clock::now() : post_process_effect_start_times.front();
        if (device != VK_NULL_HANDLE && !swapchain_images.empty()) {
            createPostProcessTargets();
        }
        sprite_state_dirty = true;
        return attachedSprites;
    }

    void VK_Window::detachPostProcessingShader() {
        if (device != VK_NULL_HANDLE && !post_process_sprites.empty()) {
            vkDeviceWaitIdle(device);
        }

        post_process_sprite = nullptr;
        destroyPostProcessTargets();
        post_process_time_enabled = false;

        if (!owned_post_process_sprites.empty()) {
            const std::vector<VK_Sprite *> sprites_to_remove = owned_post_process_sprites;
            sprites.erase(
                std::remove_if(
                    sprites.begin(),
                    sprites.end(),
                    [&sprites_to_remove](const std::unique_ptr<VK_Sprite> &sprite) {
                        return std::ranges::find(sprites_to_remove, sprite.get()) != sprites_to_remove.end();
                    }),
                sprites.end());
            sprite_state_dirty = true;
        }
        owned_post_process_sprite = nullptr;
        owned_post_process_sprites.clear();
        post_process_sprites.clear();
        post_process_effect_params.clear();
        post_process_effect_time_enabled.clear();
        post_process_effect_start_times.clear();
    }

    void VK_Window::setPostProcessingShaderParams(float p1, float p2, float p3, float p4) {
        post_process_params = {p1, p2, p3, p4};
        if (post_process_sprites.empty()) {
            return;
        }
        setPostProcessingShaderParams(0, p1, p2, p3, p4);
    }

    void VK_Window::setPostProcessingShaderParams(size_t effectIndex, float p1, float p2, float p3, float p4) {
        if (effectIndex >= post_process_sprites.size()) {
            return;
        }
        post_process_params = {p1, p2, p3, p4};
        if (effectIndex < post_process_effect_params.size()) {
            post_process_effect_params[effectIndex] = post_process_params;
        }
        if (post_process_sprites[effectIndex] != nullptr) {
            post_process_sprites[effectIndex]->setShaderParams(p1, p2, p3, p4);
        }
    }

    void VK_Window::setPostProcessingShaderTimeEnabled(bool enabled) {
        post_process_time_enabled = enabled;
        post_process_start_time = std::chrono::steady_clock::now();
        if (post_process_sprites.empty()) {
            return;
        }
        setPostProcessingShaderTimeEnabled(0, enabled);
    }

    void VK_Window::setPostProcessingShaderTimeEnabled(size_t effectIndex, bool enabled) {
        if (effectIndex >= post_process_sprites.size()) {
            return;
        }
        post_process_time_enabled = enabled;
        post_process_start_time = std::chrono::steady_clock::now();
        if (effectIndex < post_process_effect_time_enabled.size()) {
            post_process_effect_time_enabled[effectIndex] = enabled;
        }
        if (effectIndex < post_process_effect_start_times.size()) {
            post_process_effect_start_times[effectIndex] = post_process_start_time;
        }
    }

    void VK_Window::enablePostProcessing(VK_Sprite *sprite) {
        if (owned_post_process_sprite != nullptr && sprite != owned_post_process_sprite) {
            std::vector<VK_Sprite *> sprites_to_remove = owned_post_process_sprites;
            if (sprites_to_remove.empty()) {
                sprites_to_remove.push_back(owned_post_process_sprite);
            }
            sprites.erase(
                std::remove_if(
                    sprites.begin(),
                    sprites.end(),
                    [&sprites_to_remove](const std::unique_ptr<VK_Sprite> &existing_sprite) {
                        return std::ranges::find(sprites_to_remove, existing_sprite.get()) != sprites_to_remove.end();
                    }),
                sprites.end());
            owned_post_process_sprite = nullptr;
            owned_post_process_sprites.clear();
            sprite_state_dirty = true;
        }
        post_process_sprite = sprite;
        post_process_sprites = sprite == nullptr ? std::vector<VK_Sprite *>{} : std::vector<VK_Sprite *>{sprite};
        post_process_effect_params.assign(post_process_sprites.size(), post_process_params);
        post_process_effect_time_enabled.assign(post_process_sprites.size(), post_process_time_enabled);
        post_process_effect_start_times.assign(post_process_sprites.size(), post_process_start_time);
        if (device != VK_NULL_HANDLE && !swapchain_images.empty()) {
            createPostProcessTargets();
        }
    }

    bool VK_Window::isPostProcessSprite(const VK_Sprite *sprite) const {
        return sprite != nullptr && std::ranges::find(post_process_sprites, sprite) != post_process_sprites.end();
    }

    void VK_Window::destroyPostProcessTargets() {
        for (const std::vector<VkImageView> &views : post_process_views) {
            for (VkImageView view : views) {
                if (view != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, view, nullptr);
                }
            }
        }
        for (const std::vector<VkImage> &images : post_process_images) {
            for (VkImage image : images) {
                if (image != VK_NULL_HANDLE) {
                    vkDestroyImage(device, image, nullptr);
                }
            }
        }
        for (const std::vector<VkDeviceMemory> &memories : post_process_memories) {
            for (VkDeviceMemory memory : memories) {
                if (memory != VK_NULL_HANDLE) {
                    vkFreeMemory(device, memory, nullptr);
                }
            }
        }
        post_process_views.clear();
        post_process_images.clear();
        post_process_memories.clear();
        post_process_initialized.clear();
    }

    void VK_Window::createPostProcessTargets() {
        destroyPostProcessTargets();
        if (post_process_sprites.empty() || swapchain_images.empty()) {
            return;
        }
        const size_t target_count = post_process_sprites.size() > 1 ? 2U : 1U;
        post_process_images.assign(target_count, std::vector<VkImage>(swapchain_images.size(), VK_NULL_HANDLE));
        post_process_memories.assign(target_count, std::vector<VkDeviceMemory>(swapchain_images.size(), VK_NULL_HANDLE));
        post_process_views.assign(target_count, std::vector<VkImageView>(swapchain_images.size(), VK_NULL_HANDLE));
        post_process_initialized.assign(target_count, std::vector<bool>(swapchain_images.size(), false));
        VkPhysicalDeviceMemoryProperties memory_properties{};
        vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
        for (size_t target = 0; target < target_count; ++target) {
            for (size_t i = 0; i < swapchain_images.size(); ++i) {
                VkImageCreateInfo image_info{};
                image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                image_info.imageType = VK_IMAGE_TYPE_2D;
                image_info.extent = {swapchain_extent.width, swapchain_extent.height, 1};
                image_info.mipLevels = 1;
                image_info.arrayLayers = 1;
                image_info.format = swapchain_format;
                image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
                image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                image_info.samples = VK_SAMPLE_COUNT_1_BIT;
                image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                if (vkCreateImage(device, &image_info, nullptr, &post_process_images[target][i]) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create post-process image");
                }
                VkMemoryRequirements requirements{};
                vkGetImageMemoryRequirements(device, post_process_images[target][i], &requirements);
                uint32_t memory_type = UINT32_MAX;
                for (uint32_t type = 0; type < memory_properties.memoryTypeCount; ++type) {
                    if ((requirements.memoryTypeBits & (1U << type)) != 0U &&
                        (memory_properties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0U) {
                        memory_type = type;
                        break;
                    }
                }
                if (memory_type == UINT32_MAX) {
                    throw mxvk::Exception("Failed to find post-process image memory type");
                }
                VkMemoryAllocateInfo allocation{};
                allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocation.allocationSize = requirements.size;
                allocation.memoryTypeIndex = memory_type;
                if (vkAllocateMemory(device, &allocation, nullptr, &post_process_memories[target][i]) != VK_SUCCESS ||
                    vkBindImageMemory(device, post_process_images[target][i], post_process_memories[target][i], 0) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to allocate post-process image memory");
                }
                VkImageViewCreateInfo view_info{};
                view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                view_info.image = post_process_images[target][i];
                view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
                view_info.format = swapchain_format;
                view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                view_info.subresourceRange.levelCount = 1;
                view_info.subresourceRange.layerCount = 1;
                if (vkCreateImageView(device, &view_info, nullptr, &post_process_views[target][i]) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create post-process image view");
                }
            }
        }
    }

    bool VK_Window::ensureRenderResources() {
        const auto sync_ready = [this]() {
            return std::ranges::all_of(image_available.begin(), image_available.end(), [](VkSemaphore semaphore) { return semaphore != VK_NULL_HANDLE; }) &&
                   render_finished.size() == swapchain_images.size() &&
                   std::ranges::all_of(render_finished.begin(), render_finished.end(), [](VkSemaphore semaphore) { return semaphore != VK_NULL_HANDLE; }) &&
                   std::ranges::all_of(in_flight_fences.begin(), in_flight_fences.end(), [](VkFence fence) { return fence != VK_NULL_HANDLE; });
        };

        if (device == VK_NULL_HANDLE) {
            return false;
        }

        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE || command_buffers.empty() || !sync_ready() ||
            image_fences.size() != swapchain_images.size()) {
            createDevice();
        }

        return (swapchain != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE && !command_buffers.empty() && sync_ready() &&
                image_fences.size() == swapchain_images.size());
    }

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
        uint32_t device_extension_count = 0;
        vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &device_extension_count, nullptr);
        std::vector<VkExtensionProperties> device_extensions(device_extension_count);
        if (device_extension_count > 0U) {
            vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &device_extension_count, device_extensions.data());
        }

        const auto has_device_extension = [&device_extensions](const char *extension_name) {
            return std::ranges::any_of(
                device_extensions,
                [extension_name](const VkExtensionProperties &ext) {
                    return std::strcmp(ext.extensionName, extension_name) == 0;
                });
        };

        std::vector<const char *> required_device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
#if defined(MXVK_CUDA) && defined(__linux__)
        if (has_device_extension(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME)) {
            if (has_device_extension(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME)) {
                required_device_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
            }
            required_device_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
            std::cout << "vk: enabling external memory FD support for CUDA interop\n";
        } else {
            std::cout << "vk: external memory FD support unavailable; CUDA texture interop will fall back\n";
        }
#endif

#if defined(MXVK_USE_MOLTENVK)
        const bool has_portability_subset = std::ranges::any_of(
            device_extensions,
            [](const VkExtensionProperties &ext) {
                return std::strcmp(ext.extensionName, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME) == 0;
            });

        if (has_portability_subset) {
            required_device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
        }
#endif
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        VkPhysicalDeviceVulkan13Features supported_vulkan13_features{};
        supported_vulkan13_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features2.pNext = &supported_vulkan13_features;
        vkGetPhysicalDeviceFeatures2(physical_device, &features2);

        std::cout << std::format(
            "vk: feature support - synchronization2={}, dynamicRendering={}, shaderFloat64={}, fillModeNonSolid={}, samplerAnisotropy={}\n",
            supported_vulkan13_features.synchronization2 == VK_TRUE ? "true" : "false",
            supported_vulkan13_features.dynamicRendering == VK_TRUE ? "true" : "false",
            features2.features.shaderFloat64 == VK_TRUE ? "true" : "false",
            features2.features.fillModeNonSolid == VK_TRUE ? "true" : "false",
            features2.features.samplerAnisotropy == VK_TRUE ? "true" : "false");

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

        VkPhysicalDeviceFeatures2 enabled_features2{};
        enabled_features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        enabled_features2.features.shaderFloat64 = features2.features.shaderFloat64;
        enabled_features2.features.fillModeNonSolid = features2.features.fillModeNonSolid;
        enabled_features2.features.samplerAnisotropy = features2.features.samplerAnisotropy;

        enabled_features2.pNext = &vulkan13_features;

        VkDeviceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.pNext = &enabled_features2;
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

        const bool sync_ready =
            std::ranges::all_of(image_available.begin(), image_available.end(), [](VkSemaphore semaphore) { return semaphore != VK_NULL_HANDLE; }) &&
            render_finished.size() == swapchain_images.size() &&
            std::ranges::all_of(render_finished.begin(), render_finished.end(), [](VkSemaphore semaphore) { return semaphore != VK_NULL_HANDLE; }) &&
            std::ranges::all_of(in_flight_fences.begin(), in_flight_fences.end(), [](VkFence fence) { return fence != VK_NULL_HANDLE; });

        if (swapchain != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE && !command_buffers.empty() &&
            sync_ready && image_fences.size() == swapchain_images.size()) {
            std::cout << "vk: createDevice skipped because render resources are already initialized\n";
            return;
        }

        std::cout << "vk: creating swapchain\n";
        if (!createSwapchain(VK_NULL_HANDLE)) {
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

    bool VK_Window::createSwapchain(VkSwapchainKHR old_swapchain) {
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
        swapchain_supports_transfer_src = (support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0U;
        if (swapchain_supports_transfer_src) {
            create_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        } else {
            std::cerr << "mxvk: swapchain does not support transfer-source usage; saveSnapshot is unavailable\n";
        }

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
        create_info.oldSwapchain = old_swapchain;

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
        last_presented_image_index = invalid_queue_index;

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
        if (command_pool == VK_NULL_HANDLE) {
            VkCommandPoolCreateInfo pool_info{};
            pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool_info.queueFamilyIndex = graphics_queue_family;

            std::cout << "vk: creating command pool\n";
            if (vkCreateCommandPool(device, &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
                return false;
            }
        }

        if (!command_buffers.empty()) {
            std::cout << "vk: freeing stale command buffers before reallocation\n";
            vkFreeCommandBuffers(device, command_pool, static_cast<uint32_t>(command_buffers.size()), command_buffers.data());
            command_buffers.clear();
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

        auto findDepthFormat = [&](VkPhysicalDevice gpu) -> VkFormat {
            const std::array<VkFormat, 2> candidates = {
                VK_FORMAT_D32_SFLOAT,
                VK_FORMAT_D16_UNORM,
            };

            for (const VkFormat format : candidates) {
                VkFormatProperties props{};
                vkGetPhysicalDeviceFormatProperties(gpu, format, &props);
                if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U) {
                    return format;
                }
            }

            return VK_FORMAT_UNDEFINED;
        };

        depth_format = findDepthFormat(physical_device);
        if (depth_format == VK_FORMAT_UNDEFINED) {
            std::cerr << "mxvk: failed to find a supported depth format\n";
            return false;
        }

        depth_images.resize(max_frames_in_flight, VK_NULL_HANDLE);
        depth_image_memories.resize(max_frames_in_flight, VK_NULL_HANDLE);
        depth_image_views.resize(max_frames_in_flight, VK_NULL_HANDLE);
        depth_image_initialized.assign(max_frames_in_flight, false);

        for (size_t i = 0; i < depth_images.size(); ++i) {
            std::cout << std::format("vk: creating depth image for frame {}\n", i);
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = swapchain_extent.width;
            imageInfo.extent.height = swapchain_extent.height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = depth_format;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateImage(device, &imageInfo, nullptr, &depth_images[i]) != VK_SUCCESS) {
                std::cerr << std::format("mxvk: failed to create depth image {}\n", i);
                return false;
            }

            VkMemoryRequirements memReq{};
            vkGetImageMemoryRequirements(device, depth_images[i], &memReq);

            VkPhysicalDeviceMemoryProperties memProps{};
            vkGetPhysicalDeviceMemoryProperties(physical_device, &memProps);
            uint32_t memoryTypeIndex = UINT32_MAX;
            for (uint32_t t = 0; t < memProps.memoryTypeCount; ++t) {
                if (((memReq.memoryTypeBits & (1u << t)) != 0U) &&
                    ((memProps.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0U)) {
                    memoryTypeIndex = t;
                    break;
                }
            }
            if (memoryTypeIndex == UINT32_MAX) {
                std::cerr << "mxvk: failed to find depth image memory type\n";
                return false;
            }

            std::cout << std::format("vk: allocating depth image memory for frame {}\n", i);
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReq.size;
            allocInfo.memoryTypeIndex = memoryTypeIndex;

            if (vkAllocateMemory(device, &allocInfo, nullptr, &depth_image_memories[i]) != VK_SUCCESS) {
                std::cerr << std::format("mxvk: failed to allocate depth image memory {}\n", i);
                return false;
            }

            if (vkBindImageMemory(device, depth_images[i], depth_image_memories[i], 0) != VK_SUCCESS) {
                std::cerr << std::format("mxvk: failed to bind depth image memory {}\n", i);
                return false;
            }

            std::cout << std::format("vk: creating depth image view for frame {}\n", i);
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = depth_images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = depth_format;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device, &viewInfo, nullptr, &depth_image_views[i]) != VK_SUCCESS) {
                std::cerr << std::format("mxvk: failed to create depth image view {}\n", i);
                return false;
            }
        }

        if (!post_process_sprites.empty()) {
            createPostProcessTargets();
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

        cleanupSyncObjects();

        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        render_finished.assign(swapchain_images.size(), VK_NULL_HANDLE);

        for (uint32_t frame = 0; frame < max_frames_in_flight; ++frame) {
            std::cout << std::format("vk: creating image-available semaphore for frame {}\n", frame);
            if (vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available[frame]) != VK_SUCCESS) {
                cleanupSyncObjects();
                return false;
            }

            std::cout << std::format("vk: creating in-flight fence for frame {}\n", frame);
            if (vkCreateFence(device, &fence_info, nullptr, &in_flight_fences[frame]) != VK_SUCCESS) {
                cleanupSyncObjects();
                return false;
            }
        }

        for (size_t image_index = 0; image_index < render_finished.size(); ++image_index) {
            std::cout << std::format("vk: creating render-finished semaphore for swapchain image {}\n", image_index);
            if (vkCreateSemaphore(device, &semaphore_info, nullptr, &render_finished[image_index]) != VK_SUCCESS) {
                cleanupSyncObjects();
                return false;
            }
        }

        image_fences.assign(swapchain_images.size(), VK_NULL_HANDLE);
        current_frame = 0;

        std::cout << "vk: createSyncObjects complete\n";
        return true;
    }

    void VK_Window::cleanupSyncObjects() {
        if (device == VK_NULL_HANDLE) {
            return;
        }

        const bool has_in_flight_fences = std::ranges::any_of(
            in_flight_fences.begin(),
            in_flight_fences.end(),
            [](VkFence fence) { return fence != VK_NULL_HANDLE; });
        const bool has_render_finished = std::ranges::any_of(
            render_finished.begin(),
            render_finished.end(),
            [](VkSemaphore semaphore) { return semaphore != VK_NULL_HANDLE; });
        const bool has_image_available = std::ranges::any_of(
            image_available.begin(),
            image_available.end(),
            [](VkSemaphore semaphore) { return semaphore != VK_NULL_HANDLE; });

        if (!has_in_flight_fences && !has_render_finished && !has_image_available) {
            image_fences.clear();
            current_frame = 0;
            return;
        }

        if (has_in_flight_fences) {
            std::cout << "vk: destroying in-flight fences\n";
            for (VkFence &fence : in_flight_fences) {
                if (fence != VK_NULL_HANDLE) {
                    vkDestroyFence(device, fence, nullptr);
                    fence = VK_NULL_HANDLE;
                }
            }
        }

        if (has_render_finished) {
            std::cout << "vk: destroying render-finished semaphores\n";
            for (VkSemaphore &semaphore : render_finished) {
                if (semaphore != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device, semaphore, nullptr);
                    semaphore = VK_NULL_HANDLE;
                }
            }
        }

        if (has_image_available) {
            std::cout << "vk: destroying image-available semaphores\n";
            for (VkSemaphore &semaphore : image_available) {
                if (semaphore != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device, semaphore, nullptr);
                    semaphore = VK_NULL_HANDLE;
                }
            }
        }

        image_fences.clear();
        render_finished.clear();
        current_frame = 0;
    }

    void VK_Window::recreateSwapchain() {
        if (device == VK_NULL_HANDLE || window == nullptr) {
            return;
        }
        int pixel_w = 0;
        int pixel_h = 0;
        SDL_GetWindowSizeInPixels(window.get(), &pixel_w, &pixel_h);
        VkSurfaceCapabilitiesKHR surface_capabilities{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_capabilities);

        VkExtent2D new_extent{};

        if (surface_capabilities.currentExtent.width != 0xFFFFFFFFU) {
            new_extent = surface_capabilities.currentExtent;
        } else {
            new_extent.width = std::clamp(static_cast<uint32_t>(pixel_w),
                                          surface_capabilities.minImageExtent.width,
                                          surface_capabilities.maxImageExtent.width);

            new_extent.height = std::clamp(static_cast<uint32_t>(pixel_h),
                                           surface_capabilities.minImageExtent.height,
                                           surface_capabilities.maxImageExtent.height);
        }

        if (new_extent.width == 0 || new_extent.height == 0) {
            return;
        }

        const uint32_t target_w = std::clamp(static_cast<uint32_t>(pixel_w),
                                             surface_capabilities.minImageExtent.width,
                                             surface_capabilities.maxImageExtent.width);
        const uint32_t target_h = std::clamp(static_cast<uint32_t>(pixel_h),
                                             surface_capabilities.minImageExtent.height,
                                             surface_capabilities.maxImageExtent.height);

        if (new_extent.width != target_w || new_extent.height != target_h) {
            return;
        }

        if (!force_swapchain_recreate && swapchain_extent.width == new_extent.width &&
            swapchain_extent.height == new_extent.height) {
            return;
        }

        if (!force_swapchain_recreate && swapchain_extent.width == new_extent.width &&
            swapchain_extent.height == new_extent.height) {
            return;
        }

        std::cout << std::format("mxvk: recreating swapchain for {}x{} window\n", new_extent.width, new_extent.height);
        vkDeviceWaitIdle(device);
        onSwapchainAboutToRecreate();

        for (const std::unique_ptr<VK_Sprite> &sprite : sprites) {
            if (sprite) {
                sprite->releaseUploadResources();
            }
        }

        sprite_state_dirty = true;
        text_state_dirty = true;
        destroySpritePipeline();
        destroyTextPipeline();
        cleanupSyncObjects();
        VkSwapchainKHR old_swapchain = swapchain;
        cleanupSwapchain(false);
        if (!createSwapchain(old_swapchain) || !createRenderResources() || !createSyncObjects()) {
            std::cerr << "mxvk: failed to recreate swapchain after resize\n";
            if (old_swapchain != VK_NULL_HANDLE) {
                retired_swapchains.push_back(old_swapchain);
                // vkDestroySwapchainKHR(device, old_swapchain, nullptr);
            }
            return;
        }
        if (old_swapchain != VK_NULL_HANDLE) {
            // vkDestroySwapchainKHR(device, old_swapchain, nullptr);
            retired_swapchains.push_back(old_swapchain);
        }

        for (const std::unique_ptr<VK_Sprite> &sprite : sprites) {
            if (sprite) {
                sprite->setCommandPool(command_pool);
            }
        }
        for (const std::unique_ptr<VK_Sprite3D> &sprite : sprites3d) {
            if (sprite) {
                sprite->resize(this);
            }
        }

        onSwapchainRecreated();
        force_swapchain_recreate = false;
        framebuffer_resized = false;
        std::cout << "mxvk: swapchain recreation complete\n";
    }

    void VK_Window::cleanupSwapchain(bool destroy_swapchain_handle) {
        std::cout << "vk: entering cleanupSwapchain\n";
        if (device == VK_NULL_HANDLE) {
            std::cout << "vk: skipping cleanupSwapchain because logical device is null\n";
            return;
        }

        destroyPostProcessTargets();

        if (!swapchain_image_views.empty()) {
            std::cout << "vk: destroying swapchain image views\n";
            for (VkImageView image_view : swapchain_image_views) {
                if (image_view != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, image_view, nullptr);
                }
            }
        }
        swapchain_image_views.clear();

        if (!depth_image_views.empty()) {
            std::cout << "vk: destroying depth image views\n";
            for (VkImageView view : depth_image_views) {
                if (view != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, view, nullptr);
                }
            }
        }
        depth_image_views.clear();

        if (!depth_images.empty()) {
            std::cout << "vk: destroying depth images\n";
            for (VkImage image : depth_images) {
                if (image != VK_NULL_HANDLE) {
                    vkDestroyImage(device, image, nullptr);
                }
            }
        }
        depth_images.clear();

        if (!depth_image_memories.empty()) {
            std::cout << "vk: freeing depth image memory\n";
            for (VkDeviceMemory memory : depth_image_memories) {
                if (memory != VK_NULL_HANDLE) {
                    vkFreeMemory(device, memory, nullptr);
                }
            }
        }
        depth_image_memories.clear();
        depth_image_initialized.clear();

        swapchain_images.clear();
        swapchain_image_initialized.clear();
        last_presented_image_index = invalid_queue_index;
        swapchain_supports_transfer_src = false;
        depth_format = VK_FORMAT_UNDEFINED;

        // This guard prevents destroying the active swapchain during a live resize
        if (destroy_swapchain_handle && swapchain != VK_NULL_HANDLE) {
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

        int pixel_w = 0;
        int pixel_h = 0;
        if (window != nullptr) {
            SDL_GetWindowSizeInPixels(window.get(), &pixel_w, &pixel_h);
        }
        if (pixel_w <= 0 || pixel_h <= 0) {
            framebuffer_resized = true;
            return;
        }

        if (swapchain_extent.width != 0 && swapchain_extent.height != 0) {
            if (swapchain_extent.width != static_cast<uint32_t>(pixel_w) ||
                swapchain_extent.height != static_cast<uint32_t>(pixel_h)) {
                framebuffer_resized = true;
                force_swapchain_recreate = true;
                return;
            }
        }

        if (!ensureRenderResources()) {
            std::cout << "mxvk: creating deferred swapchain/render/sync resources\n";
            if (!ensureRenderResources()) {
                std::cerr << "mxvk: deferred resource creation failed; skipping frame\n";
                return;
            }
        }

        const bool sync_ready =
            std::ranges::all_of(image_available.begin(), image_available.end(), [](VkSemaphore semaphore) { return semaphore != VK_NULL_HANDLE; }) &&
            std::ranges::all_of(render_finished.begin(), render_finished.end(), [](VkSemaphore semaphore) { return semaphore != VK_NULL_HANDLE; }) &&
            std::ranges::all_of(in_flight_fences.begin(), in_flight_fences.end(), [](VkFence fence) { return fence != VK_NULL_HANDLE; });
        if (!sync_ready) {
            return;
        }

        if (sprite_state_dirty && !sprites.empty() && swapchain_format != VK_FORMAT_UNDEFINED) {
            for (const std::unique_ptr<VK_Sprite> &sprite : sprites) {
                if (!sprite) {
                    continue;
                }
                sprite->setColorAttachmentFormat(swapchain_format);
                sprite->setDepthAttachmentFormat(depth_format);
                sprite->setDescriptorSetLayout(sprite_descriptor_set_layout);
                sprite->rebuildPipeline();
                sprite->rebuildInstancedPipeline();
            }
            try {
                createSpritePipeline();
            } catch (const std::exception &ex) {
                std::cerr << std::format("mxvk: sprite pipeline build skipped: {}\n", ex.what());
            }
            sprite_state_dirty = false;
        }

        if (fps_counter_enabled) {
            updateFpsCounter();
        }

        if (text_state_dirty && text_renderer && swapchain_format != VK_FORMAT_UNDEFINED) {
            text_renderer->setDescriptorSetLayout(text_descriptor_set_layout);
            try {
                createTextPipeline();
            } catch (const std::exception &ex) {
                std::cerr << std::format("mxvk: text pipeline build skipped: {}\n", ex.what());
            }
            text_state_dirty = false;
        }

        VkFence &frame_fence = in_flight_fences[current_frame];
        VkSemaphore &acquire_semaphore = image_available[current_frame];
        const size_t depth_slot = static_cast<size_t>(current_frame);

        const VkResult wait_result = vkWaitForFences(device, 1, &frame_fence, VK_TRUE, UINT64_MAX);
        if (wait_result == VK_ERROR_DEVICE_LOST) {
            std::cerr << "mxvk: device lost while waiting for frame fence; stopping render loop\n";
            active = false;
            return;
        }
        if (wait_result != VK_SUCCESS) {
            std::cerr << std::format("mxvk: Failed waiting for frame fence (VkResult={})\n", static_cast<int>(wait_result));
            framebuffer_resized = true;
            return;
        }

        uint32_t image_index = 0;
        const uint64_t acquire_timeout_ns = 100000000ULL; // 100 ms avoids UINT64_MAX forward-progress VUIDs.
        const VkResult acquire_result =
            vkAcquireNextImageKHR(device, swapchain, acquire_timeout_ns, acquire_semaphore, VK_NULL_HANDLE, &image_index);

        static VkResult last_acquire_error = VK_SUCCESS;
        static uint32_t repeated_acquire_errors = 0;

        if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
            last_acquire_error = VK_SUCCESS;
            repeated_acquire_errors = 0;
            force_swapchain_recreate = true;
            framebuffer_resized = true;
            return;
        }

        if (acquire_result == VK_ERROR_SURFACE_LOST_KHR) {
            if (last_acquire_error != acquire_result) {
                std::cerr << "mxvk: swapchain surface lost during acquire; requesting swapchain recreation\n";
                last_acquire_error = acquire_result;
                repeated_acquire_errors = 0;
            }
            force_swapchain_recreate = true;
            framebuffer_resized = true;
            return;
        }

        if (acquire_result == VK_TIMEOUT || acquire_result == VK_NOT_READY) {
            // Non-fatal: no image available this frame.
            return;
        }

        if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
            if (last_acquire_error == acquire_result) {
                ++repeated_acquire_errors;
                if ((repeated_acquire_errors % 120U) == 0U) {
                    std::cerr << std::format(
                        "mxvk: repeated swapchain acquire failures continue (VkResult={})\n",
                        static_cast<int>(acquire_result));
                }
            } else {
                last_acquire_error = acquire_result;
                repeated_acquire_errors = 0;
                std::cerr << std::format(
                    "mxvk: Failed to acquire swapchain image (VkResult={})\n",
                    static_cast<int>(acquire_result));
            }

            if (acquire_result == VK_ERROR_DEVICE_LOST) {
                std::cerr << "mxvk: device lost; stopping render loop\n";
                active = false;
            }
            return;
        }

        last_acquire_error = VK_SUCCESS;
        repeated_acquire_errors = 0;

        if (image_index >= command_buffers.size() || image_index >= swapchain_images.size() || image_index >= swapchain_image_views.size()) {
            return;
        }

        if (image_index >= image_fences.size()) {
            std::cerr << "mxvk: acquired image index exceeds tracked in-flight image count\n";
            return;
        }

        if (image_index >= render_finished.size() || render_finished[image_index] == VK_NULL_HANDLE) {
            std::cerr << "mxvk: acquired image index exceeds render-finished semaphore count\n";
            return;
        }

        VkSemaphore &present_semaphore = render_finished[image_index];

        if (image_fences[image_index] != VK_NULL_HANDLE && image_fences[image_index] != frame_fence) {
            const VkResult image_wait_result = vkWaitForFences(device, 1, &image_fences[image_index], VK_TRUE, UINT64_MAX);
            if (image_wait_result == VK_ERROR_DEVICE_LOST) {
                std::cerr << "mxvk: device lost while waiting on acquired image fence; stopping render loop\n";
                active = false;
                return;
            }
            if (image_wait_result != VK_SUCCESS) {
                std::cerr << std::format("mxvk: Failed waiting for acquired image fence (VkResult={})\n", static_cast<int>(image_wait_result));
                framebuffer_resized = true;
                return;
            }
        }

        const VkCommandBuffer cmd = command_buffers[image_index];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
            std::cerr << "mxvk: Failed to begin command buffer\n";
            framebuffer_resized = true;
            return;
        }

        VkClearValue clear_value{};
        clear_value.color = clear_color;
        const bool use_post_process = post_process_enabled &&
                                      !post_process_sprites.empty() &&
                                      !post_process_images.empty() &&
                                      !post_process_views.empty() &&
                                      !post_process_initialized.empty() &&
                                      image_index < post_process_images.front().size() &&
                                      image_index < post_process_views.front().size() &&
                                      image_index < post_process_initialized.front().size();

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

        if (use_post_process) {
            VkImageMemoryBarrier2 post_target_barrier{};
            post_target_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            post_target_barrier.srcStageMask = post_process_initialized[0][image_index] ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_NONE;
            post_target_barrier.srcAccessMask = post_process_initialized[0][image_index] ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : VK_ACCESS_2_NONE;
            post_target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            post_target_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            post_target_barrier.oldLayout = post_process_initialized[0][image_index] ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
            post_target_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            post_target_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            post_target_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            post_target_barrier.image = post_process_images[0][image_index];
            post_target_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            post_target_barrier.subresourceRange.levelCount = 1;
            post_target_barrier.subresourceRange.layerCount = 1;
            VkDependencyInfo post_target_dependency{};
            post_target_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            post_target_dependency.imageMemoryBarrierCount = 1;
            post_target_dependency.pImageMemoryBarriers = &post_target_barrier;
            vkCmdPipelineBarrier2(cmd, &post_target_dependency);
        }

        VkImageMemoryBarrier2 to_depth_barrier{};
        to_depth_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        to_depth_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        to_depth_barrier.srcAccessMask = VK_ACCESS_2_NONE;
        to_depth_barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        to_depth_barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        to_depth_barrier.oldLayout =
            (depth_slot < depth_image_initialized.size() && depth_image_initialized[depth_slot])
                ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_UNDEFINED;
        to_depth_barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        to_depth_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_depth_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        if (depth_slot < depth_images.size()) {
            to_depth_barrier.image = depth_images[depth_slot];
        }
        to_depth_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        to_depth_barrier.subresourceRange.baseMipLevel = 0;
        to_depth_barrier.subresourceRange.levelCount = 1;
        to_depth_barrier.subresourceRange.baseArrayLayer = 0;
        to_depth_barrier.subresourceRange.layerCount = 1;

        if (to_depth_barrier.image != VK_NULL_HANDLE) {
            VkDependencyInfo pre_depth_dependency{};
            pre_depth_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            pre_depth_dependency.imageMemoryBarrierCount = 1;
            pre_depth_dependency.pImageMemoryBarriers = &to_depth_barrier;
            vkCmdPipelineBarrier2(cmd, &pre_depth_dependency);
        }

        onPrepareFrameRendering(cmd, image_index);
        for (const std::unique_ptr<VK_Sprite> &sprite : sprites) {
            if (sprite) {
                sprite->prepareForRendering(cmd);
            }
        }

        VkRenderingAttachmentInfo color_attachment{};
        color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attachment.imageView = use_post_process ? post_process_views[0][image_index] : swapchain_image_views[image_index];
        color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
        color_attachment.resolveImageView = VK_NULL_HANDLE;
        color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.clearValue = clear_value;

        VkClearValue depth_clear_value{};
        depth_clear_value.depthStencil = {1.0f, 0};

        VkRenderingAttachmentInfo depth_attachment{};
        depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        if (depth_slot < depth_image_views.size()) {
            depth_attachment.imageView = depth_image_views[depth_slot];
        }
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.clearValue = depth_clear_value;

        VkRenderingInfo rendering_info{};
        rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering_info.renderArea.offset = {0, 0};
        rendering_info.renderArea.extent = swapchain_extent;
        rendering_info.layerCount = 1;
        rendering_info.viewMask = 0;
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachments = &color_attachment;
        rendering_info.pDepthAttachment = (depth_attachment.imageView != VK_NULL_HANDLE) ? &depth_attachment : nullptr;
        rendering_info.pStencilAttachment = nullptr;

        vkCmdBeginRendering(cmd, &rendering_info);

        VkViewport viewport{};
        viewport.x = 0.0F;
        viewport.y = 0.0F;
        viewport.width = static_cast<float>(swapchain_extent.width);
        viewport.height = static_cast<float>(swapchain_extent.height);
        viewport.minDepth = 0.0F;
        viewport.maxDepth = 1.0F;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapchain_extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        onRecordCustomRendering(cmd, image_index);

        // Draw 2D overlays after custom scene rendering so HUD/text stays on top.
        if (!sprites.empty()) {
            for (const std::unique_ptr<VK_Sprite> &sprite : sprites) {
                if (!sprite || isPostProcessSprite(sprite.get())) {
                    continue;
                }
                if (sprite_pipeline != VK_NULL_HANDLE) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline);
                }
                sprite->renderSprites(cmd, sprite_pipeline_layout, swapchain_extent.width, swapchain_extent.height);
            }
        }

        if (text_renderer && text_pipeline != VK_NULL_HANDLE) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, text_pipeline);
            text_renderer->renderText(cmd, text_pipeline_layout, swapchain_extent.width, swapchain_extent.height);
        }

        vkCmdEndRendering(cmd);
        if (depth_slot < depth_image_initialized.size()) {
            depth_image_initialized[depth_slot] = true;
        }

        if (use_post_process) {
            VkImageMemoryBarrier2 post_target_barrier{};
            post_target_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            post_target_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            post_target_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            post_target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            post_target_barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            post_target_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            post_target_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            post_target_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            post_target_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            post_target_barrier.image = post_process_images[0][image_index];
            post_target_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            post_target_barrier.subresourceRange.levelCount = 1;
            post_target_barrier.subresourceRange.layerCount = 1;
            VkDependencyInfo post_target_dependency{};
            post_target_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            post_target_dependency.imageMemoryBarrierCount = 1;
            post_target_dependency.pImageMemoryBarriers = &post_target_barrier;
            vkCmdPipelineBarrier2(cmd, &post_target_dependency);
            post_process_initialized[0][image_index] = true;

            size_t source_target = 0;
            for (size_t effect_index = 0; effect_index < post_process_sprites.size(); ++effect_index) {
                VK_Sprite *effect_sprite = post_process_sprites[effect_index];
                if (effect_sprite == nullptr) {
                    continue;
                }

                const bool final_effect = (effect_index + 1U) == post_process_sprites.size();
                const size_t destination_target = source_target == 0U ? 1U : 0U;
                VkImageView destination_view = final_effect ? swapchain_image_views[image_index] : post_process_views[destination_target][image_index];

                if (!final_effect) {
                    VkImageMemoryBarrier2 next_target_barrier{};
                    next_target_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    next_target_barrier.srcStageMask = post_process_initialized[destination_target][image_index] ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_NONE;
                    next_target_barrier.srcAccessMask = post_process_initialized[destination_target][image_index] ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : VK_ACCESS_2_NONE;
                    next_target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    next_target_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    next_target_barrier.oldLayout = post_process_initialized[destination_target][image_index] ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
                    next_target_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    next_target_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    next_target_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    next_target_barrier.image = post_process_images[destination_target][image_index];
                    next_target_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    next_target_barrier.subresourceRange.levelCount = 1;
                    next_target_barrier.subresourceRange.layerCount = 1;
                    VkDependencyInfo next_target_dependency{};
                    next_target_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    next_target_dependency.imageMemoryBarrierCount = 1;
                    next_target_dependency.pImageMemoryBarriers = &next_target_barrier;
                    vkCmdPipelineBarrier2(cmd, &next_target_dependency);
                }

                VkRenderingAttachmentInfo post_attachment{};
                post_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                post_attachment.imageView = destination_view;
                post_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                post_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                post_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                VkRenderingInfo post_info{};
                post_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                post_info.renderArea.extent = swapchain_extent;
                post_info.layerCount = 1;
                post_info.colorAttachmentCount = 1;
                post_info.pColorAttachments = &post_attachment;
                vkCmdBeginRendering(cmd, &post_info);

                if (effect_index < post_process_effect_time_enabled.size() && post_process_effect_time_enabled[effect_index]) {
                    post_process_effect_params[effect_index][0] = std::chrono::duration<float>(std::chrono::steady_clock::now() - post_process_effect_start_times[effect_index]).count();
                }
                if (effect_index < post_process_effect_params.size()) {
                    const std::array<float, 4> &params = post_process_effect_params[effect_index];
                    effect_sprite->setShaderParams(params[0], params[1], params[2], params[3]);
                }

                effect_sprite->setExternalTexture(post_process_views[source_target][image_index], static_cast<int>(swapchain_extent.width), static_cast<int>(swapchain_extent.height));
                effect_sprite->drawSpriteRect(0, 0, static_cast<int>(swapchain_extent.width), static_cast<int>(swapchain_extent.height));
                renderStandaloneSprite(*effect_sprite, cmd);
                vkCmdEndRendering(cmd);

                if (!final_effect) {
                    VkImageMemoryBarrier2 sampled_target_barrier{};
                    sampled_target_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    sampled_target_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    sampled_target_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    sampled_target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    sampled_target_barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
                    sampled_target_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    sampled_target_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    sampled_target_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    sampled_target_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    sampled_target_barrier.image = post_process_images[destination_target][image_index];
                    sampled_target_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    sampled_target_barrier.subresourceRange.levelCount = 1;
                    sampled_target_barrier.subresourceRange.layerCount = 1;
                    VkDependencyInfo sampled_target_dependency{};
                    sampled_target_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    sampled_target_dependency.imageMemoryBarrierCount = 1;
                    sampled_target_dependency.pImageMemoryBarriers = &sampled_target_barrier;
                    vkCmdPipelineBarrier2(cmd, &sampled_target_dependency);
                    post_process_initialized[destination_target][image_index] = true;
                    source_target = destination_target;
                }
            }
        }

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
            framebuffer_resized = true;
            return;
        }

        VkSemaphoreSubmitInfo wait_semaphore_info{};
        wait_semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        wait_semaphore_info.semaphore = acquire_semaphore;
        wait_semaphore_info.value = 0;
        wait_semaphore_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        wait_semaphore_info.deviceIndex = 0;

        VkCommandBufferSubmitInfo command_buffer_info{};
        command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        command_buffer_info.commandBuffer = cmd;
        command_buffer_info.deviceMask = 0;

        VkSemaphoreSubmitInfo signal_semaphore_info{};
        signal_semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signal_semaphore_info.semaphore = present_semaphore;
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

        const VkResult fence_reset_result = vkResetFences(device, 1, &frame_fence);
        if (fence_reset_result == VK_ERROR_DEVICE_LOST) {
            std::cerr << "mxvk: device lost while resetting frame fence; stopping render loop\n";
            active = false;
            return;
        }
        if (fence_reset_result != VK_SUCCESS) {
            std::cerr << std::format("mxvk: Failed to reset frame fence (VkResult={})\n", static_cast<int>(fence_reset_result));
            framebuffer_resized = true;
            return;
        }

        const VkResult submit_result = vkQueueSubmit2(graphics_queue, 1, &submit_info, frame_fence);
        if (submit_result == VK_ERROR_DEVICE_LOST) {
            std::cerr << "mxvk: device lost during queue submit; stopping render loop\n";
            active = false;
            return;
        }
        if (submit_result != VK_SUCCESS) {
            std::cerr << std::format("mxvk: Failed to submit draw command (VkResult={})\n", static_cast<int>(submit_result));
            // We already acquired an image this frame. Force swapchain recreation so we do not reuse
            // a signaled acquire semaphore that never got consumed by a successful submit.
            framebuffer_resized = true;
            return;
        }
        image_fences[image_index] = frame_fence;
        swapchain_image_initialized[image_index] = true;

        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &present_semaphore;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain;
        present_info.pImageIndices = &image_index;

        const VkResult present_result = vkQueuePresentKHR(present_queue, &present_info);
        if (present_result == VK_SUCCESS || present_result == VK_SUBOPTIMAL_KHR) {
            last_presented_image_index = image_index;
        }

        if (present_result == VK_ERROR_OUT_OF_DATE_KHR) {
            force_swapchain_recreate = true;
            last_resize_event_ms = SDL_GetTicks();
            framebuffer_resized = true;
        } else if (present_result == VK_SUBOPTIMAL_KHR) {
            last_resize_event_ms = SDL_GetTicks();
            framebuffer_resized = true;
            force_swapchain_recreate = true;
        } else if (present_result != VK_SUCCESS) {
            std::cerr << "mxvk: Failed to present swapchain image\n";
        }

        for (const std::unique_ptr<VK_Sprite> &sprite : sprites) {
            if (sprite) {
                sprite->clearQueue();
            }
        }
        if (text_renderer) {
            text_renderer->clearQueue();
        }

        current_frame = (current_frame + 1U) % max_frames_in_flight;
        if (!retired_swapchains.empty() && (present_result == VK_SUCCESS || present_result == VK_SUBOPTIMAL_KHR)) {
            for (VkSwapchainKHR retired : retired_swapchains) {
                vkDestroySwapchainKHR(device, retired, nullptr);
            }
            retired_swapchains.clear();
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

    void VK_Window::setFont(const std::string &fontPath, int fontSize) {
        if (fontPath.empty() || fontSize <= 0) {
            throw mxvk::Exception("setFont requires a non-empty path and positive font size");
        }

        font_path = fontPath;
        font_size = fontSize;
        font_configured = true;

        if (device == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot set font before Vulkan device initialization");
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            createDevice();
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot initialize text renderer before swapchain and command resources are available");
        }

        if (!text_renderer) {
            ensureTextRenderer();
        } else {
            text_renderer->setFont(font_path, font_size);
        }
        text_state_dirty = true;
    }

    void VK_Window::printText(const std::string &text, int x, int y, const SDL_Color &col) {
        if (text.empty()) {
            return;
        }

        if (!font_configured) {
            throw mxvk::Exception("printText requires setFont() to be called first");
        }

        ensureTextRenderer();
        text_renderer->printTextG_Solid(text, x, y, col);
    }

    void VK_Window::printText(const std::string &text, int x, int y, const SDL_Color &col, TTF_Font *font) {
        if (text.empty()) {
            return;
        }

        if (font == nullptr) {
            throw mxvk::Exception("printText requires a non-null TTF_Font");
        }

        ensureTextRenderer(resolveDefaultFontPath(), font_size);
        if (!text_renderer) {
            throw mxvk::Exception("printText with an explicit font could not initialize the text renderer");
        }
        text_renderer->printTextG_Solid(text, x, y, col, font);
    }

    void VK_Window::printText(const std::string &text, int x, int y, const SDL_Color &col, const Font &font) {
        printText(text, x, y, col, font.get());
    }

    void VK_Window::clearTextQueue() {
        if (text_renderer) {
            text_renderer->clearQueue();
        }
    }

    bool VK_Window::getTextDimensions(const std::string &text, int &width, int &height) {
        if (!font_configured) {
            throw mxvk::Exception("getTextDimensions requires setFont() to be called first");
        }

        if (!text_renderer) {
            ensureTextRenderer();
        }
        if (!text_renderer) {
            width = 0;
            height = 0;
            return false;
        }
        return text_renderer->getTextDimensions(text, width, height);
    }

    bool VK_Window::getTextDimensions(const std::string &text, int &width, int &height, TTF_Font *font) {
        if (font == nullptr) {
            width = 0;
            height = 0;
            return false;
        }

        ensureTextRenderer(resolveDefaultFontPath(), font_size);
        if (!text_renderer) {
            width = 0;
            height = 0;
            return false;
        }
        return text_renderer->getTextDimensions(text, width, height, font);
    }

    bool VK_Window::getTextDimensions(const std::string &text, int &width, int &height, const Font &font) {
        return getTextDimensions(text, width, height, font.get());
    }

    void VK_Window::ensureTextRenderer() {
        if (text_renderer) {
            return;
        }

        if (!font_configured || font_path.empty() || font_size <= 0) {
            return;
        }

        if (device == VK_NULL_HANDLE) {
            return;
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            createDevice();
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            return;
        }

        if (text_descriptor_set_layout == VK_NULL_HANDLE) {
            createTextDescriptorSetLayout();
        }

        text_renderer = std::make_unique<VK_Text>(device, physical_device, graphics_queue, command_pool, font_path, font_size);
        text_renderer->setDescriptorSetLayout(text_descriptor_set_layout);
        text_state_dirty = true;
    }

    void VK_Window::ensureTextRenderer(const std::string &fallbackFontPath, int fallbackFontSize) {
        if (text_renderer) {
            return;
        }

        if (device == VK_NULL_HANDLE) {
            return;
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            createDevice();
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            return;
        }

        const std::string renderer_font_path = font_configured ? font_path : fallbackFontPath;
        const int renderer_font_size = font_configured ? font_size : fallbackFontSize;
        if (renderer_font_path.empty() || renderer_font_size <= 0) {
            return;
        }

        if (text_descriptor_set_layout == VK_NULL_HANDLE) {
            createTextDescriptorSetLayout();
        }

        text_renderer = std::make_unique<VK_Text>(device, physical_device, graphics_queue, command_pool, renderer_font_path, renderer_font_size);
        text_renderer->setDescriptorSetLayout(text_descriptor_set_layout);
        text_state_dirty = true;
    }

    std::string VK_Window::resolveDefaultFontPath() const {
        std::vector<std::filesystem::path> candidates{};

        if (const char *basePath = SDL_GetBasePath(); basePath != nullptr) {
            const std::filesystem::path executableDir(basePath);
            candidates.push_back(executableDir / "data" / "default.ttf");
            candidates.push_back(executableDir / "default.ttf");
        }

        candidates.push_back(std::filesystem::path("data") / "default.ttf");

        if (!font_path.empty()) {
            const std::filesystem::path configured_font_path(font_path);
            if (configured_font_path.has_parent_path()) {
                candidates.push_back(configured_font_path.parent_path() / "default.ttf");
            }
        }

        candidates.push_back(std::filesystem::path(MXVK_DEFAULT_FONT_DIR) / "default.ttf");

        std::error_code exists_error{};
        for (const std::filesystem::path &candidate : candidates) {
            if (std::filesystem::exists(candidate, exists_error)) {
                return candidate.string();
            }
            exists_error.clear();
        }

        return {};
    }

    void VK_Window::toggleFpsCounter() {
        fps_counter_enabled = !fps_counter_enabled;
        fps_counter_frame_count = 0;
        fps_counter_sample_time = std::chrono::steady_clock::now();
        fps_counter_text = "FPS: --";
        std::cout << std::format("mxvk: FPS counter {}\n", fps_counter_enabled ? "enabled" : "disabled");

        if (!fps_counter_enabled) {
            return;
        }

        if (!fps_counter_font_ready) {
            const std::string default_font_path = resolveDefaultFontPath();
            if (default_font_path.empty()) {
                std::cerr << "mxvk: F12 FPS counter could not locate data/default.ttf\n";
                fps_counter_enabled = false;
                return;
            }
            fps_counter_font.reset(default_font_path, 18);
            fps_counter_font_ready = true;
        }
    }

    void VK_Window::updateFpsCounter() {
        if (!fps_counter_enabled) {
            return;
        }

        if (!fps_counter_font_ready) {
            const std::string default_font_path = resolveDefaultFontPath();
            if (default_font_path.empty()) {
                std::cerr << "mxvk: F12 FPS counter could not locate data/default.ttf\n";
                fps_counter_enabled = false;
                return;
            }
            fps_counter_font.reset(default_font_path, 18);
            fps_counter_font_ready = true;
        }

        ++fps_counter_frame_count;
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - fps_counter_sample_time).count();
        if (elapsed >= 0.5) {
            const double fps = static_cast<double>(fps_counter_frame_count) / elapsed;
            fps_counter_frame_count = 0;
            fps_counter_sample_time = now;
            fps_counter_text = std::format("FPS: {:.1f}", fps);
        }

        printText(fps_counter_text, 12, 10, SDL_Color{255, 255, 255, 255}, fps_counter_font);
    }

    void VK_Window::createTextDescriptorSetLayout() {
        if (device == VK_NULL_HANDLE || text_descriptor_set_layout != VK_NULL_HANDLE) {
            return;
        }

        VkDescriptorSetLayoutBinding sampler_binding{};
        sampler_binding.binding = 0;
        sampler_binding.descriptorCount = 1;
        sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sampler_binding.pImmutableSamplers = nullptr;
        sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &sampler_binding;

        if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &text_descriptor_set_layout) != VK_SUCCESS) {
            throw mxvk::Exception("Failed to create text descriptor set layout");
        }
    }

    void VK_Window::destroyTextPipeline() {
        if (device == VK_NULL_HANDLE) {
            text_pipeline = VK_NULL_HANDLE;
            text_pipeline_layout = VK_NULL_HANDLE;
            return;
        }

        if (text_pipeline != VK_NULL_HANDLE) {
            std::cout << "vk: destroying text pipeline\n";
            vkDestroyPipeline(device, text_pipeline, nullptr);
            text_pipeline = VK_NULL_HANDLE;
        }
        if (text_pipeline_layout != VK_NULL_HANDLE) {
            std::cout << "vk: destroying text pipeline layout\n";
            vkDestroyPipelineLayout(device, text_pipeline_layout, nullptr);
            text_pipeline_layout = VK_NULL_HANDLE;
        }
    }

    void VK_Window::createTextPipeline() {
        if (device == VK_NULL_HANDLE || swapchain_format == VK_FORMAT_UNDEFINED || text_descriptor_set_layout == VK_NULL_HANDLE) {
            return;
        }

        destroyTextPipeline();

        const std::vector<char> vert_shader = loadSpv(resolveRuntimeShaderPath("text.vert.spv", MXVK_TEXT_SHADER_DIR));
        const std::vector<char> frag_shader = loadSpv(resolveRuntimeShaderPath("text.frag.spv", MXVK_TEXT_SHADER_DIR));

        const VkShaderModule vert_module = createShaderModule(device, vert_shader);
        VkShaderModule frag_module = VK_NULL_HANDLE;

        try {
            frag_module = createShaderModule(device, frag_shader);

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

            const VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage, frag_stage};

            VkVertexInputBindingDescription binding_description{};
            binding_description.binding = 0;
            binding_description.stride = sizeof(float) * 4;
            binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            std::array<VkVertexInputAttributeDescription, 2> attributes{};
            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
            attributes[0].offset = 0;
            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
            attributes[1].offset = sizeof(float) * 2;

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

            const std::array<VkDynamicState, 2> dynamic_states = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            };
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
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0F;
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

            VkPipelineColorBlendAttachmentState color_attachment{};
            color_attachment.colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;
            color_attachment.blendEnable = VK_TRUE;
            color_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            color_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            color_attachment.colorBlendOp = VK_BLEND_OP_ADD;
            color_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            color_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            color_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

            VkPipelineColorBlendStateCreateInfo color_blending{};
            color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            color_blending.logicOpEnable = VK_FALSE;
            color_blending.attachmentCount = 1;
            color_blending.pAttachments = &color_attachment;

            VkPushConstantRange push_constant_range{};
            push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            push_constant_range.offset = 0;
            push_constant_range.size = sizeof(float) * 2;

            VkPipelineLayoutCreateInfo pipeline_layout_info{};
            pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipeline_layout_info.setLayoutCount = 1;
            pipeline_layout_info.pSetLayouts = &text_descriptor_set_layout;
            pipeline_layout_info.pushConstantRangeCount = 1;
            pipeline_layout_info.pPushConstantRanges = &push_constant_range;

            if (vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &text_pipeline_layout) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create text pipeline layout");
            }

            VkPipelineRenderingCreateInfo rendering_info{};
            rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            rendering_info.viewMask = 0;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachmentFormats = &swapchain_format;
            if (depth_format != VK_FORMAT_UNDEFINED) {
                rendering_info.depthAttachmentFormat = depth_format;
            }

            VkGraphicsPipelineCreateInfo pipeline_info{};
            pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipeline_info.pNext = &rendering_info;
            pipeline_info.stageCount = 2;
            pipeline_info.pStages = shader_stages;
            pipeline_info.pVertexInputState = &vertex_input;
            pipeline_info.pInputAssemblyState = &input_assembly;
            pipeline_info.pViewportState = &viewport_state;
            pipeline_info.pRasterizationState = &rasterizer;
            pipeline_info.pMultisampleState = &multisampling;
            pipeline_info.pDepthStencilState = &depth_stencil;
            pipeline_info.pColorBlendState = &color_blending;
            pipeline_info.pDynamicState = &dynamic_state;
            pipeline_info.layout = text_pipeline_layout;
            pipeline_info.renderPass = VK_NULL_HANDLE;
            pipeline_info.subpass = 0;
            pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
            pipeline_info.basePipelineIndex = -1;

            if (vkCreateGraphicsPipelines(device, pipeline_cache, 1, &pipeline_info, nullptr, &text_pipeline) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create text graphics pipeline");
            }
        } catch (...) {
            if (text_pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, text_pipeline, nullptr);
                text_pipeline = VK_NULL_HANDLE;
            }
            if (text_pipeline_layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, text_pipeline_layout, nullptr);
                text_pipeline_layout = VK_NULL_HANDLE;
            }
            if (frag_module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, frag_module, nullptr);
            }
            vkDestroyShaderModule(device, vert_module, nullptr);
            throw;
        }

        vkDestroyShaderModule(device, frag_module, nullptr);
        vkDestroyShaderModule(device, vert_module, nullptr);
    }

    VK_Sprite *VK_Window::createSprite(const std::string &pngPath, const std::string &vertexShaderPath, const std::string &fragmentShaderPath) {
        if (device == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot create sprite before Vulkan device initialization");
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            createDevice();
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot create sprite before swapchain and command resources are available");
        }

        if (sprite_descriptor_set_layout == VK_NULL_HANDLE) {
            createSpriteDescriptorSetLayout();
        }

        auto sprite = std::make_unique<VK_Sprite>(device, physical_device, graphics_queue, command_pool);
        sprite->setPipelineCache(pipeline_cache);
        sprite->setDescriptorSetLayout(sprite_descriptor_set_layout);
        sprite->setColorAttachmentFormat(swapchain_format);
        sprite->setDepthAttachmentFormat(depth_format);

        if (!vertexShaderPath.empty()) {
            sprite->setVertexShaderPath(vertexShaderPath);
        }

        sprite->loadSprite(pngPath, fragmentShaderPath);

        VK_Sprite *const sprite_ptr = sprite.get();
        sprites.push_back(std::move(sprite));
        sprite_state_dirty = true;
        return sprite_ptr;
    }

    VK_Sprite *VK_Window::createSprite(SDL_Surface *surface, const std::string &vertexShaderPath, const std::string &fragmentShaderPath) {
        if (surface == nullptr) {
            throw mxvk::Exception("Cannot create sprite from a null SDL surface");
        }
        if (device == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot create sprite before Vulkan device initialization");
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            createDevice();
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot create sprite before swapchain and command resources are available");
        }

        if (sprite_descriptor_set_layout == VK_NULL_HANDLE) {
            createSpriteDescriptorSetLayout();
        }

        auto sprite = std::make_unique<VK_Sprite>(device, physical_device, graphics_queue, command_pool);
        sprite->setPipelineCache(pipeline_cache);
        sprite->setDescriptorSetLayout(sprite_descriptor_set_layout);
        sprite->setColorAttachmentFormat(swapchain_format);
        sprite->setDepthAttachmentFormat(depth_format);

        if (!vertexShaderPath.empty()) {
            sprite->setVertexShaderPath(vertexShaderPath);
        }

        sprite->loadSprite(surface, fragmentShaderPath);

        VK_Sprite *const sprite_ptr = sprite.get();
        sprites.push_back(std::move(sprite));
        sprite_state_dirty = true;
        return sprite_ptr;
    }

    VK_Sprite *VK_Window::createSprite(int width, int height, const std::string &vertexShaderPath, const std::string &fragmentShaderPath) {
        if (width <= 0 || height <= 0) {
            throw mxvk::Exception("Sprite dimensions must be positive");
        }
        if (device == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot create sprite before Vulkan device initialization");
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            createDevice();
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot create sprite before swapchain and command resources are available");
        }

        if (sprite_descriptor_set_layout == VK_NULL_HANDLE) {
            createSpriteDescriptorSetLayout();
        }

        auto sprite = std::make_unique<VK_Sprite>(device, physical_device, graphics_queue, command_pool);
        sprite->setPipelineCache(pipeline_cache);
        sprite->setDescriptorSetLayout(sprite_descriptor_set_layout);
        sprite->setColorAttachmentFormat(swapchain_format);
        sprite->setDepthAttachmentFormat(depth_format);

        sprite->createEmptySprite(width, height, vertexShaderPath, fragmentShaderPath);

        VK_Sprite *const sprite_ptr = sprite.get();
        sprites.push_back(std::move(sprite));
        sprite_state_dirty = true;
        return sprite_ptr;
    }

    VK_Sprite3D *VK_Window::createSprite3D(const std::string &pngPath, const std::string &vertexShaderPath, const std::string &fragmentShaderPath) {
        if (device == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot create 3D sprite before Vulkan device initialization");
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            createDevice();
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot create 3D sprite before swapchain and command resources are available");
        }

        const std::string vertPath = vertexShaderPath.empty()
                                         ? resolveRuntimeShaderPath("sprite3d.vert.spv", MXVK_SPRITE3D_SHADER_DIR)
                                         : vertexShaderPath;
        const std::string fragPath = fragmentShaderPath.empty()
                                         ? resolveRuntimeShaderPath("sprite3d.frag.spv", MXVK_SPRITE3D_SHADER_DIR)
                                         : fragmentShaderPath;

        auto sprite = std::make_unique<VK_Sprite3D>();
        sprite->load(this, pngPath, vertPath, fragPath);

        VK_Sprite3D *const sprite_ptr = sprite.get();
        sprites3d.push_back(std::move(sprite));
        return sprite_ptr;
    }

    VK_Sprite3D *VK_Window::createSprite3D(SDL_Surface *surface, const std::string &vertexShaderPath, const std::string &fragmentShaderPath) {
        if (surface == nullptr) {
            throw mxvk::Exception("Cannot create 3D sprite from a null SDL surface");
        }
        if (device == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot create 3D sprite before Vulkan device initialization");
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            createDevice();
        }
        if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot create 3D sprite before swapchain and command resources are available");
        }

        const std::string vertPath = vertexShaderPath.empty()
                                         ? resolveRuntimeShaderPath("sprite3d.vert.spv", MXVK_SPRITE3D_SHADER_DIR)
                                         : vertexShaderPath;
        const std::string fragPath = fragmentShaderPath.empty()
                                         ? resolveRuntimeShaderPath("sprite3d.frag.spv", MXVK_SPRITE3D_SHADER_DIR)
                                         : fragmentShaderPath;

        auto sprite = std::make_unique<VK_Sprite3D>();
        sprite->load(this, surface, vertPath, fragPath);

        VK_Sprite3D *const sprite_ptr = sprite.get();
        sprites3d.push_back(std::move(sprite));
        return sprite_ptr;
    }

    void VK_Window::showCursor(bool on) {
        if (on)
            SDL_ShowCursor();
        else
            SDL_HideCursor();
    }

    void VK_Window::createSpriteDescriptorSetLayout() {
        if (device == VK_NULL_HANDLE || sprite_descriptor_set_layout != VK_NULL_HANDLE) {
            return;
        }

        VkDescriptorSetLayoutBinding sampler_binding{};
        sampler_binding.binding = 0;
        sampler_binding.descriptorCount = 1;
        sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sampler_binding.pImmutableSamplers = nullptr;
        sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &sampler_binding;

        if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &sprite_descriptor_set_layout) != VK_SUCCESS) {
            throw mxvk::Exception("Failed to create sprite descriptor set layout");
        }
    }

    void VK_Window::destroySpritePipeline() {
        if (device == VK_NULL_HANDLE) {
            sprite_pipeline = VK_NULL_HANDLE;
            sprite_pipeline_layout = VK_NULL_HANDLE;
            return;
        }

        if (sprite_pipeline != VK_NULL_HANDLE) {
            std::cout << "vk: destroying sprite pipeline\n";
            vkDestroyPipeline(device, sprite_pipeline, nullptr);
            sprite_pipeline = VK_NULL_HANDLE;
        }
        if (sprite_pipeline_layout != VK_NULL_HANDLE) {
            std::cout << "vk: destroying sprite pipeline layout\n";
            vkDestroyPipelineLayout(device, sprite_pipeline_layout, nullptr);
            sprite_pipeline_layout = VK_NULL_HANDLE;
        }
    }

    void VK_Window::createSpritePipeline() {
        if (device == VK_NULL_HANDLE || swapchain_format == VK_FORMAT_UNDEFINED) {
            return;
        }
        if (sprite_descriptor_set_layout == VK_NULL_HANDLE) {
            createSpriteDescriptorSetLayout();
        }

        destroySpritePipeline();

        const std::vector<char> vert_shader = loadSpv(resolveRuntimeShaderPath("sprite.vert.spv", MXVK_SPRITE_SHADER_DIR));
        const std::vector<char> frag_shader = loadSpv(resolveRuntimeShaderPath("sprite.frag.spv", MXVK_SPRITE_SHADER_DIR));

        const VkShaderModule vert_module = createShaderModule(device, vert_shader);
        VkShaderModule frag_module = VK_NULL_HANDLE;

        try {
            frag_module = createShaderModule(device, frag_shader);

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

            const VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage, frag_stage};

            VkVertexInputBindingDescription binding_description{};
            binding_description.binding = 0;
            binding_description.stride = sizeof(float) * 4;
            binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            std::array<VkVertexInputAttributeDescription, 2> attributes{};
            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
            attributes[0].offset = 0;
            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
            attributes[1].offset = sizeof(float) * 2;

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

            const std::array<VkDynamicState, 2> dynamic_states = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            };
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
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0F;
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

            VkPipelineColorBlendAttachmentState color_attachment{};
            color_attachment.colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;
            color_attachment.blendEnable = VK_TRUE;
            color_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            color_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            color_attachment.colorBlendOp = VK_BLEND_OP_ADD;
            color_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            color_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            color_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

            VkPipelineColorBlendStateCreateInfo color_blending{};
            color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            color_blending.logicOpEnable = VK_FALSE;
            color_blending.attachmentCount = 1;
            color_blending.pAttachments = &color_attachment;

            VkPushConstantRange push_constant_range{};
            push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            push_constant_range.offset = 0;
            push_constant_range.size = sizeof(float) * 12;

            VkPipelineLayoutCreateInfo pipeline_layout_info{};
            pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipeline_layout_info.setLayoutCount = 1;
            pipeline_layout_info.pSetLayouts = &sprite_descriptor_set_layout;
            pipeline_layout_info.pushConstantRangeCount = 1;
            pipeline_layout_info.pPushConstantRanges = &push_constant_range;

            if (vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &sprite_pipeline_layout) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create sprite pipeline layout");
            }

            VkPipelineRenderingCreateInfo rendering_info{};
            rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            rendering_info.viewMask = 0;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachmentFormats = &swapchain_format;
            if (depth_format != VK_FORMAT_UNDEFINED) {
                rendering_info.depthAttachmentFormat = depth_format;
            }

            VkGraphicsPipelineCreateInfo pipeline_info{};
            pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipeline_info.pNext = &rendering_info;
            pipeline_info.stageCount = 2;
            pipeline_info.pStages = shader_stages;
            pipeline_info.pVertexInputState = &vertex_input;
            pipeline_info.pInputAssemblyState = &input_assembly;
            pipeline_info.pViewportState = &viewport_state;
            pipeline_info.pRasterizationState = &rasterizer;
            pipeline_info.pMultisampleState = &multisampling;
            pipeline_info.pDepthStencilState = &depth_stencil;
            pipeline_info.pColorBlendState = &color_blending;
            pipeline_info.pDynamicState = &dynamic_state;
            pipeline_info.layout = sprite_pipeline_layout;
            pipeline_info.renderPass = VK_NULL_HANDLE;
            pipeline_info.subpass = 0;
            pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
            pipeline_info.basePipelineIndex = -1;

            if (vkCreateGraphicsPipelines(device, pipeline_cache, 1, &pipeline_info, nullptr, &sprite_pipeline) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create sprite graphics pipeline");
            }
        } catch (...) {
            if (sprite_pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, sprite_pipeline, nullptr);
                sprite_pipeline = VK_NULL_HANDLE;
            }
            if (sprite_pipeline_layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, sprite_pipeline_layout, nullptr);
                sprite_pipeline_layout = VK_NULL_HANDLE;
            }
            if (frag_module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, frag_module, nullptr);
            }
            vkDestroyShaderModule(device, vert_module, nullptr);
            throw;
        }

        vkDestroyShaderModule(device, frag_module, nullptr);
        vkDestroyShaderModule(device, vert_module, nullptr);
    }
} // namespace mxvk
