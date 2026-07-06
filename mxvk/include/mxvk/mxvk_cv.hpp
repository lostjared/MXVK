/**
 * @file mxvk_cv.hpp
 * @brief OpenCV video-capture integration for the Vulkan backend.
 *
 * mxvk::VK_Capture (Vulkan variant) wraps cv::VideoCapture and feeds decoded
 * frames into a VK_Sprite for display within a VK_Window.
 */
#ifndef __VK_OPENCV__H_
#define __VK_OPENCV__H_

#include "mxvk.hpp"
#include "mxvk_sprite.hpp"
#include <memory>
#include <opencv2/opencv.hpp>
#ifdef MXVK_CUDA
#include <opencv2/core/cuda.hpp>
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudaimgproc.hpp>
#endif

namespace mxvk {
    class VKAbstractModel;

    /**
     * @class VK_Capture
     * @brief Vulkan OpenCV video capture source.
     *
     * Opens a video file or camera, decodes frames, and uploads them to a
     * VKSprite texture for Vulkan-accelerated rendering inside a VKWindow.
     */
    class VK_Capture {
      public:
        /** @brief Default constructor. */
        VK_Capture() = default;
        /** @brief Destructor. */
        ~VK_Capture() = default;
        VK_Capture &operator=(const VK_Capture &) = delete;
        VK_Capture &operator=(VK_Capture &&) = delete;
        VK_Capture(const VK_Capture &) = delete;
        VK_Capture(VK_Capture &&) = delete;

        /**
         * @brief Open a video file.
         * @param filename Path to the video file.
         * @return @c true on success.
         */
        bool open(const std::string &filename);

        /**
         * @brief Open a camera device.
         * @param id   Camera index.
         * @param mode Backend hint (0 = auto).
         * @return @c true on success.
         */
        bool open(int id, int mode = 0);

        /** @brief Close the capture device. */
        void close();

        /**
         * @brief Destroy the current sprite and its Vulkan resources.
         *
         * Call this before swapchain/command-pool teardown to avoid destroying
         * sprite staging command buffers against an invalid command pool.
         */
        void resetSprite();

        /**
         * @brief Check whether the capture device is open.
         * @return @c true if the VideoCapture is opened.
         */
        bool is_open() const { return cap.isOpened(); }

        /**
         * @brief Allocate the VKSprite and upload the initial texture.
         * @param device     Logical Vulkan device.
         * @param physDev    Physical device.
         * @param gQueue     Graphics queue.
         * @param cmdPool    Command pool.
         * @param width  Display width.
         * @param height Display height.
         * @param vert   Vertex shader path.
         * @param frag   Fragment shader path.
         * @return @c true on success.
         */
        bool createImage(VkDevice device, VkPhysicalDevice physDev, VkQueue gQueue,
                         VkCommandPool cmdPool, size_t width, size_t height,
                         const std::string &vert, const std::string &frag);

        /**
         * @brief Access the backing sprite.
         * @return Pointer to the VKSprite (may be null before createImage).
         */
        VK_Sprite *getSprite() { return sprite.get(); }

        /**
         * @brief Draw the current frame at a specified position and size.
         * @param x Width destination X.
         * @param y Width destination Y.
         * @param width  Display width.
         * @param height Display height.
         */
        void draw(int x, int y, int width, int height);

        /**
         * @brief Draw using the sprite's native dimensions.
         * @param x Destination X.
         * @param y Destination Y.
         */
        void draw(int x, int y);

        /**
         * @brief Replace the vertex/fragment shaders.
         * @param width  Display width.
         * @param height Display height.
         * @param vert   New vertex shader path.
         * @param frag   New fragment shader path.
         * @return @c true on success.
         */
        bool reload(size_t width, size_t height, const std::string &vert, const std::string &frag);

        /**
         * @brief Capture and upload the next video frame.
         * @return @c true if a frame was available.
         */
        bool read();
        bool readRgba(cv::Mat &rgba, bool flipY = false);
        bool readToSprite(VK_Sprite &targetSprite);
        bool readToSprite(VK_Sprite &targetSprite, bool flipY);
        bool readToModelTexture(VKAbstractModel &model, bool flipY = false);
#ifdef MXVK_CUDA
        bool readGpuRgba(cv::cuda::GpuMat &rgba, bool flipY = false);
        cv::cuda::Stream &cudaStream() { return cuda_stream; }
#endif

        /**
         * @brief Capture the next frame into a cv::Mat.
         * @param frame Output matrix.
         * @return @c true if a frame was available.
         */
        bool read(cv::Mat &frame);

        /**
         * @brief Set a VideoCapture property.
         * @param option cv::VideoCaptureProperties constant.
         * @param value  Desired value.
         */
        void set(unsigned int option, double value);

        /**
         * @brief Query a VideoCapture property.
         * @param option cv::VideoCaptureProperties constant.
         * @return Current value.
         */
        double get(unsigned int option);

      private:
#ifdef MXVK_CUDA
        bool initializeCuda();
        bool readCuda();
#endif

        std::unique_ptr<VK_Sprite> sprite; ///< Backing Vulkan sprite (owned).
        cv::VideoCapture cap;              ///< OpenCV capture device.
        cv::Mat frame;                     ///< Most recent decoded frame.
#ifdef MXVK_CUDA
        bool cudaChecked = false;
        bool cudaAvailable = false;
        bool cudaMappedInput = false;
        bool cudaPipelineLogged = false;
        bool cudaFlipYForVulkan = true;
        cv::cuda::Stream cuda_stream{};
        cv::cuda::GpuMat gpuFrame{};
        cv::cuda::GpuMat gpuRgba{};
        cv::cuda::GpuMat gpuVulkanRgba{};
        cv::cuda::HostMem mappedFrame{cv::cuda::HostMem::SHARED};
        cv::cuda::HostMem pinnedRgba{cv::cuda::HostMem::PAGE_LOCKED};
        cv::Mat pinnedRgbaMat{};
#endif
    };
} // namespace mxvk

#endif
