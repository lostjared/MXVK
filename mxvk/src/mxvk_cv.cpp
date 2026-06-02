/**
 * @file vk_cv.cpp
 * @brief Implementation of mxvk::VK_Capture (Vulkan + OpenCV variant).
 */
#include "mxvk/mxvk_cv.hpp"
#include <filesystem>
#include <iostream>

namespace mxvk {

    bool VK_Capture::open(const std::string &filename) {
        const bool ok = cap.open(filename);
        if (ok)
            std::cout << std::format("mxvk_cv: Opened file: {}\n", filename);
        else
            std::cout << std::format("mxvk_cv: Failed to open file: {}\n", filename);
        return ok;
    }

    bool VK_Capture::open(int id, int mode) {
#ifdef __linux__
        if (mode == 0)
            mode = cv::CAP_V4L2;
#endif
        if (cap.open(id, mode)) {
            cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
            std::cout << std::format("mxvk_cv: Opened device: {}\n", id);
            return true;
        }
        std::cout << std::format("mxvk_cv: Failed to open device: {}\n", id);
        return false;
    }

    void VK_Capture::close() {
        cap.release();
        std::cout << "mxvk_cv: Capture closed\n";
    }

    void VK_Capture::resetSprite() {
        sprite.reset();
    }

    bool VK_Capture::createImage(VkDevice device, VkPhysicalDevice physDev, VkQueue gQueue,
                                 VkCommandPool cmdPool, size_t width, size_t height,
                                 const std::string &vert, const std::string &frag) {
        sprite = std::make_unique<VK_Sprite>(device, physDev, gQueue, cmdPool);
        sprite->createEmptySprite(static_cast<int>(width), static_cast<int>(height), vert, frag);
        sprite->enableExtendedUBO();
        sprite->rebuildPipeline();
        std::cout << std::format("mxvk_cv: Sprite created: {}x{}\n", width, height);
        return true;
    }

    bool VK_Capture::reload(size_t width, size_t height, const std::string &vert, const std::string &frag) {
        if (sprite) {
            sprite->createEmptySprite(static_cast<int>(width), static_cast<int>(height), vert, frag);
            sprite->enableExtendedUBO();
            std::cout << std::format("mxvk_cv: Shader reloaded: vert={} frag={}\n",
                                     std::filesystem::path(vert).filename().string(),
                                     std::filesystem::path(frag).filename().string());
            return true;
        }
        std::cout << "mxvk_cv: Reload failed: no sprite\n";
        return false;
    }

    bool VK_Capture::read(cv::Mat &frame) {
        return cap.read(frame);
    }

    bool VK_Capture::read() {
        if (!cap.read(frame)) {
            return false;
        }
        cv::Mat rgba;
        cv::cvtColor(frame, rgba, cv::COLOR_BGR2RGBA);
        sprite->updateTexture(rgba.ptr(), rgba.cols, rgba.rows, rgba.step);
        return true;
    }

    void VK_Capture::draw(int x, int y, int width, int height) {
        if (sprite)
            sprite->drawSpriteRect(x, y, width, height);
    }

    void VK_Capture::draw(int x, int y) {
        if (sprite)
            sprite->drawSprite(x, y);
    }

    void VK_Capture::set(unsigned int option, double value) {
        cap.set(option, value);
    }

    double VK_Capture::get(unsigned int option) {
        return cap.get(option);
    }

} // namespace mxvk