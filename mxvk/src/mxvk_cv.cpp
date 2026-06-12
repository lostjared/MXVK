/**
 * @file vk_cv.cpp
 * @brief Implementation of mxvk::VK_Capture (Vulkan + OpenCV variant).
 */
#include "mxvk/mxvk_cv.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include <cstdlib>
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

    bool VK_Capture::readRgba(cv::Mat &rgba, bool flipY) {
#ifdef MXVK_CUDA
        cv::Mat cpuFallbackFrame;

        try {
            if (initializeCuda()) {
                if (cudaMappedInput_) {
                    if (!cap.read(mappedFrame_) || mappedFrame_.empty()) {
                        return false;
                    }
                    cpuFallbackFrame = mappedFrame_.createMatHeader();
                    const cv::cuda::GpuMat mappedInput = mappedFrame_.createGpuMatHeader();
                    cv::cuda::cvtColor(mappedInput, gpuRgba_, cv::COLOR_BGR2RGBA, 0, cudaStream_);
                } else {
                    if (!cap.read(frame) || frame.empty()) {
                        return false;
                    }
                    cpuFallbackFrame = frame;
                    gpuFrame_.upload(frame, cudaStream_);
                    cv::cuda::cvtColor(gpuFrame_, gpuRgba_, cv::COLOR_BGR2RGBA, 0, cudaStream_);
                }

                if (flipY) {
                    cv::cuda::flip(gpuRgba_, gpuVulkanRgba_, 0, cudaStream_);
                } else {
                    gpuVulkanRgba_ = gpuRgba_;
                }

                if (!cudaPipelineLogged_) {
                    std::cout << std::format(
                        "mxvk_cv: CUDA RGBA path active: capture -> GpuMat -> cv::cuda::cvtColor(BGR to RGBA) -> {} -> download\n",
                        flipY ? "cv::cuda::flip(Y)" : "no Y flip");
                    cudaPipelineLogged_ = true;
                }

                pinnedRgba_.create(gpuVulkanRgba_.size(), gpuVulkanRgba_.type());
                gpuVulkanRgba_.download(pinnedRgba_, cudaStream_);
                cudaStream_.waitForCompletion();
                rgba = pinnedRgba_.createMatHeader();
                return true;
            }
        } catch (const cv::Exception &e) {
            std::cout << std::format("mxvk_cv: CUDA RGBA path failed; falling back to CPU path: {}\n", e.what());
            cudaAvailable_ = false;
            if (cpuFallbackFrame.empty()) {
                return false;
            }
            cv::cvtColor(cpuFallbackFrame, rgba, cv::COLOR_BGR2RGBA);
            if (flipY) {
                cv::flip(rgba, rgba, 0);
            }
            return true;
        }
#endif

        if (!cap.read(frame) || frame.empty()) {
            return false;
        }
        cv::cvtColor(frame, rgba, cv::COLOR_BGR2RGBA);
        if (flipY) {
            cv::flip(rgba, rgba, 0);
        }
        return true;
    }

#ifdef MXVK_CUDA
    bool VK_Capture::initializeCuda() {
        if (cudaChecked_) {
            return cudaAvailable_;
        }

        cudaChecked_ = true;
        std::cout << "mxvk_cv: CUDA init: MXVK_CUDA compile definition is enabled\n";
        try {
            const int deviceCount = cv::cuda::getCudaEnabledDeviceCount();
            std::cout << std::format("mxvk_cv: CUDA init: OpenCV reports {} CUDA-enabled device(s)\n", deviceCount);
            if (deviceCount <= 0) {
                std::cout << "mxvk_cv: CUDA path unavailable: no CUDA-enabled OpenCV device\n";
                return false;
            }

            cv::cuda::setDevice(0);
            const cv::cuda::DeviceInfo deviceInfo(0);
            cudaMappedInput_ = deviceInfo.canMapHostMemory();
            if (const char *flipEnv = std::getenv("MXVK_CUDA_FLIP_Y")) {
                cudaFlipYForVulkan_ = std::string(flipEnv) != "0";
            }
            cudaAvailable_ = true;
            std::cout << std::format("mxvk_cv: CUDA init: selected device 0: {}\n", deviceInfo.name());
            std::cout << std::format("mxvk_cv: CUDA init: compute capability {}.{}\n",
                                     deviceInfo.majorVersion(), deviceInfo.minorVersion());
            std::cout << std::format("mxvk_cv: CUDA init: unified addressing={}, host memory mapping={}\n",
                                     deviceInfo.unifiedAddressing() ? "true" : "false",
                                     cudaMappedInput_ ? "true" : "false");
            std::cout << std::format("mxvk_cv: CUDA init: capture input mode={}\n",
                                     cudaMappedInput_ ? "mapped HostMem::SHARED GpuMat header" : "cv::Mat upload to GpuMat");
            std::cout << std::format("mxvk_cv: CUDA init: Vulkan upload Y flip={} (default on for CUDA external-memory textures; override with MXVK_CUDA_FLIP_Y=0 or 1)\n",
                                     cudaFlipYForVulkan_ ? "enabled" : "disabled");
            std::cout << std::format("mxvk_cv: CUDA path enabled on {} ({})\n",
                                     deviceInfo.name(),
                                     cudaMappedInput_ ? "mapped zero-copy input" : "pinned async input upload");
            std::cout << "mxvk_cv: CUDA pipeline: capture -> GpuMat -> cv::cuda::cvtColor(BGR to RGBA) -> direct Vulkan texture when available\n";
        } catch (const cv::Exception &e) {
            std::cout << std::format("mxvk_cv: CUDA path unavailable: {}\n", e.what());
            cudaAvailable_ = false;
        }

        return cudaAvailable_;
    }

    bool VK_Capture::readCuda() {
        cv::Mat cpuFallbackFrame;

        try {
            if (cudaMappedInput_) {
                if (!cap.read(mappedFrame_)) {
                    return false;
                }
                if (mappedFrame_.empty()) {
                    return false;
                }

                cpuFallbackFrame = mappedFrame_.createMatHeader();
                const cv::cuda::GpuMat mappedInput = mappedFrame_.createGpuMatHeader();
                cv::cuda::cvtColor(mappedInput, gpuRgba_, cv::COLOR_BGR2RGBA, 0, cudaStream_);
            } else {
                if (!cap.read(frame)) {
                    return false;
                }
                if (frame.empty()) {
                    return false;
                }

                cpuFallbackFrame = frame;
                gpuFrame_.upload(frame, cudaStream_);
                cv::cuda::cvtColor(gpuFrame_, gpuRgba_, cv::COLOR_BGR2RGBA, 0, cudaStream_);
            }

            if (cudaFlipYForVulkan_) {
                cv::cuda::flip(gpuRgba_, gpuVulkanRgba_, 0, cudaStream_);
            } else {
                gpuVulkanRgba_ = gpuRgba_;
            }

            if (sprite && sprite->updateTextureCuda(gpuVulkanRgba_, cudaStream_)) {
                if (!cudaPipelineLogged_) {
                    std::cout << std::format(
                        "mxvk_cv: CUDA interop active: RGBA GpuMat -> {} -> Vulkan external-memory image\n",
                        cudaFlipYForVulkan_ ? "cv::cuda::flip(Y)" : "no Y flip");
                    cudaPipelineLogged_ = true;
                }
                return true;
            }

            if (!cudaPipelineLogged_) {
                std::cout << "mxvk_cv: CUDA interop unavailable for this sprite; using pinned CUDA download -> Vulkan staging upload\n";
                cudaPipelineLogged_ = true;
            }
            pinnedRgba_.create(gpuVulkanRgba_.size(), gpuVulkanRgba_.type());
            gpuVulkanRgba_.download(pinnedRgba_, cudaStream_);
            cudaStream_.waitForCompletion();

            pinnedRgbaMat_ = pinnedRgba_.createMatHeader();
            sprite->updateTexture(pinnedRgbaMat_.ptr(), pinnedRgbaMat_.cols, pinnedRgbaMat_.rows,
                                  static_cast<int>(pinnedRgbaMat_.step));
            return true;
        } catch (const cv::Exception &e) {
            std::cout << std::format("mxvk_cv: CUDA frame path failed; falling back to CPU path: {}\n", e.what());
            cudaAvailable_ = false;
        }

        if (cpuFallbackFrame.empty()) {
            return false;
        }

        cv::Mat rgba;
        cv::cvtColor(cpuFallbackFrame, rgba, cv::COLOR_BGR2RGBA);
        sprite->updateTexture(rgba.ptr(), rgba.cols, rgba.rows, static_cast<int>(rgba.step));
        return true;
    }

    bool VK_Capture::readGpuRgba(cv::cuda::GpuMat &rgba, bool flipY) {
        if (!initializeCuda()) {
            return false;
        }

        try {
            if (cudaMappedInput_) {
                if (!cap.read(mappedFrame_) || mappedFrame_.empty()) {
                    return false;
                }
                const cv::cuda::GpuMat mappedInput = mappedFrame_.createGpuMatHeader();
                cv::cuda::cvtColor(mappedInput, gpuRgba_, cv::COLOR_BGR2RGBA, 0, cudaStream_);
            } else {
                if (!cap.read(frame) || frame.empty()) {
                    return false;
                }
                gpuFrame_.upload(frame, cudaStream_);
                cv::cuda::cvtColor(gpuFrame_, gpuRgba_, cv::COLOR_BGR2RGBA, 0, cudaStream_);
            }

            if (flipY) {
                cv::cuda::flip(gpuRgba_, gpuVulkanRgba_, 0, cudaStream_);
            } else {
                gpuVulkanRgba_ = gpuRgba_;
            }

            if (!cudaPipelineLogged_) {
                std::cout << std::format(
                    "mxvk_cv: CUDA GpuMat path active: capture -> GpuMat -> cv::cuda::cvtColor(BGR to RGBA) -> {} -> resident GpuMat for caller\n",
                    flipY ? "cv::cuda::flip(Y)" : "no Y flip");
                cudaPipelineLogged_ = true;
            }

            rgba = gpuVulkanRgba_;
            return true;
        } catch (const cv::Exception &e) {
            std::cout << std::format("mxvk_cv: CUDA GpuMat path failed: {}\n", e.what());
            cudaAvailable_ = false;
            return false;
        }
    }
#endif

    bool VK_Capture::readToModelTexture(VKAbstractModel &model, bool flipY) {
#ifdef MXVK_CUDA
        if (initializeCuda()) {
            cv::Mat cpuFallbackFrame;

            try {
                if (cudaMappedInput_) {
                    if (!cap.read(mappedFrame_) || mappedFrame_.empty()) {
                        return false;
                    }
                    cpuFallbackFrame = mappedFrame_.createMatHeader();
                    const cv::cuda::GpuMat mappedInput = mappedFrame_.createGpuMatHeader();
                    cv::cuda::cvtColor(mappedInput, gpuRgba_, cv::COLOR_BGR2RGBA, 0, cudaStream_);
                } else {
                    if (!cap.read(frame) || frame.empty()) {
                        return false;
                    }
                    cpuFallbackFrame = frame;
                    gpuFrame_.upload(frame, cudaStream_);
                    cv::cuda::cvtColor(gpuFrame_, gpuRgba_, cv::COLOR_BGR2RGBA, 0, cudaStream_);
                }

                if (flipY) {
                    cv::cuda::flip(gpuRgba_, gpuVulkanRgba_, 0, cudaStream_);
                } else {
                    gpuVulkanRgba_ = gpuRgba_;
                }

                if (model.updatePrimaryTextureCuda(gpuVulkanRgba_, cudaStream_)) {
                    if (!cudaPipelineLogged_) {
                        std::cout << std::format(
                            "mxvk_cv: CUDA interop active for model texture: RGBA GpuMat -> {} -> Vulkan external-memory image array (no download)\n",
                            flipY ? "cv::cuda::flip(Y)" : "no Y flip");
                        cudaPipelineLogged_ = true;
                    }
                    return true;
                }

                if (!cudaPipelineLogged_) {
                    std::cout << "mxvk_cv: CUDA interop unavailable for model texture; using pinned CUDA download -> Vulkan staging upload\n";
                    cudaPipelineLogged_ = true;
                }
                pinnedRgba_.create(gpuVulkanRgba_.size(), gpuVulkanRgba_.type());
                gpuVulkanRgba_.download(pinnedRgba_, cudaStream_);
                cudaStream_.waitForCompletion();

                pinnedRgbaMat_ = pinnedRgba_.createMatHeader();
                return model.updatePrimaryTexture(pinnedRgbaMat_.ptr(), pinnedRgbaMat_.cols, pinnedRgbaMat_.rows,
                                                  static_cast<int>(pinnedRgbaMat_.step));
            } catch (const cv::Exception &e) {
                std::cout << std::format("mxvk_cv: CUDA model-texture path failed; falling back to CPU path: {}\n", e.what());
                cudaAvailable_ = false;
                if (cpuFallbackFrame.empty()) {
                    return false;
                }
                cv::Mat rgba;
                cv::cvtColor(cpuFallbackFrame, rgba, cv::COLOR_BGR2RGBA);
                if (flipY) {
                    cv::flip(rgba, rgba, 0);
                }
                return model.updatePrimaryTexture(rgba.ptr(), rgba.cols, rgba.rows, static_cast<int>(rgba.step));
            }
        }
#endif
        cv::Mat rgba;
        if (!readRgba(rgba, flipY)) {
            return false;
        }
        return model.updatePrimaryTexture(rgba.ptr(), rgba.cols, rgba.rows, static_cast<int>(rgba.step));
    }

    bool VK_Capture::readToSprite(VK_Sprite &targetSprite) {
#ifdef MXVK_CUDA
        if (initializeCuda()) {
            cv::Mat cpuFallbackFrame;

            try {
                if (cudaMappedInput_) {
                    if (!cap.read(mappedFrame_) || mappedFrame_.empty()) {
                        return false;
                    }
                    cpuFallbackFrame = mappedFrame_.createMatHeader();
                    const cv::cuda::GpuMat mappedInput = mappedFrame_.createGpuMatHeader();
                    cv::cuda::cvtColor(mappedInput, gpuRgba_, cv::COLOR_BGR2RGBA, 0, cudaStream_);
                } else {
                    if (!cap.read(frame) || frame.empty()) {
                        return false;
                    }
                    cpuFallbackFrame = frame;
                    gpuFrame_.upload(frame, cudaStream_);
                    cv::cuda::cvtColor(gpuFrame_, gpuRgba_, cv::COLOR_BGR2RGBA, 0, cudaStream_);
                }

                if (cudaFlipYForVulkan_) {
                    cv::cuda::flip(gpuRgba_, gpuVulkanRgba_, 0, cudaStream_);
                } else {
                    gpuVulkanRgba_ = gpuRgba_;
                }

                if (targetSprite.updateTextureCuda(gpuVulkanRgba_, cudaStream_)) {
                    if (!cudaPipelineLogged_) {
                        std::cout << std::format(
                            "mxvk_cv: CUDA interop active for external sprite: RGBA GpuMat -> {} -> Vulkan external-memory image\n",
                            cudaFlipYForVulkan_ ? "cv::cuda::flip(Y)" : "no Y flip");
                        cudaPipelineLogged_ = true;
                    }
                    return true;
                }

                if (!cudaPipelineLogged_) {
                    std::cout << "mxvk_cv: CUDA interop unavailable for external sprite; using pinned CUDA download -> Vulkan staging upload\n";
                    cudaPipelineLogged_ = true;
                }
                pinnedRgba_.create(gpuVulkanRgba_.size(), gpuVulkanRgba_.type());
                gpuVulkanRgba_.download(pinnedRgba_, cudaStream_);
                cudaStream_.waitForCompletion();

                pinnedRgbaMat_ = pinnedRgba_.createMatHeader();
                targetSprite.updateTexture(pinnedRgbaMat_.ptr(), pinnedRgbaMat_.cols, pinnedRgbaMat_.rows,
                                           static_cast<int>(pinnedRgbaMat_.step));
                return true;
            } catch (const cv::Exception &e) {
                std::cout << std::format("mxvk_cv: CUDA external-sprite path failed; falling back to CPU path: {}\n", e.what());
                cudaAvailable_ = false;
                if (cpuFallbackFrame.empty()) {
                    return false;
                }
                cv::Mat rgba;
                cv::cvtColor(cpuFallbackFrame, rgba, cv::COLOR_BGR2RGBA);
                targetSprite.updateTexture(rgba.ptr(), rgba.cols, rgba.rows, static_cast<int>(rgba.step));
                return true;
            }
        }
#endif
        if (!cap.read(frame) || frame.empty()) {
            return false;
        }
        cv::Mat rgba;
        cv::cvtColor(frame, rgba, cv::COLOR_BGR2RGBA);
        targetSprite.updateTexture(rgba.ptr(), rgba.cols, rgba.rows, static_cast<int>(rgba.step));
        return true;
    }

    bool VK_Capture::read() {
#ifdef MXVK_CUDA
        if (initializeCuda()) {
            return readCuda();
        }
#endif
        if (!cap.read(frame)) {
            return false;
        }
        cv::Mat rgba;
        cv::cvtColor(frame, rgba, cv::COLOR_BGR2RGBA);
        sprite->updateTexture(rgba.ptr(), rgba.cols, rgba.rows, static_cast<int>(rgba.step));
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
