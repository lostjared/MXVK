/**
 * @file mxvk_ff_capture.hpp
 * @brief FFmpeg video-file capture with optional CUDA hardware decoding.
 */
#ifndef MXVK_FF_CAPTURE_HPP
#define MXVK_FF_CAPTURE_HPP

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}
#ifdef MXVK_CUDA
#include <opencv2/core/cuda.hpp>
#include <opencv2/cudaimgproc.hpp>
#endif

namespace mxvk {

    /**
     * @brief FFmpeg-backed video-file capture source.
     *
     * VK_FF_Capture opens video files directly through FFmpeg, prefers CUDA
     * hardware decoding when available, and returns tightly packed RGBA8 frames
     * suitable for Vulkan staging uploads or encoder input.
     */
    class VK_FF_Capture {
      public:
        /** @brief Construct a closed capture source. */
        VK_FF_Capture() = default;
        /** @brief Close and release FFmpeg resources. */
        ~VK_FF_Capture();
        VK_FF_Capture(const VK_FF_Capture &) = delete;
        VK_FF_Capture(VK_FF_Capture &&) = delete;
        VK_FF_Capture &operator=(const VK_FF_Capture &) = delete;
        VK_FF_Capture &operator=(VK_FF_Capture &&) = delete;

        /**
         * @brief Open a video file.
         * @param filename Path to the input video file.
         * @return true on success.
         */
        bool open(const std::string &filename);
        /** @brief Close the active file and release decoder resources. */
        void close();
        /** @brief Check whether a decoder is open. */
        [[nodiscard]] bool is_open() const { return formatCtx != nullptr && codecCtx != nullptr; }

        /**
         * @brief Decode the next frame as tightly packed RGBA8.
         * @param rgba Output byte buffer, resized to pitch * height.
         * @param width Output frame width in pixels.
         * @param height Output frame height in pixels.
         * @param pitch Output row pitch in bytes.
         * @param flipY Flip rows vertically after conversion when true.
         * @return true when a frame was decoded, false on EOF or failure.
         */
        bool readRgba(std::vector<uint8_t> &rgba, int &width, int &height, int &pitch, bool flipY = false);
#ifdef MXVK_CUDA
        /**
         * @brief Decode the next frame as a CUDA-resident RGBA8 image.
         * @param rgba Output CUDA GpuMat containing RGBA8 pixels.
         * @param stream CUDA stream used for device copies and color conversion.
         * @param flipY Flip rows vertically after conversion when true.
         * @return true when a frame was decoded and converted.
         */
        bool readGpuRgba(cv::cuda::GpuMat &rgba, cv::cuda::Stream &stream, bool flipY = false);
#endif

        /** @brief Source width in pixels. */
        [[nodiscard]] int width() const { return frameWidth; }
        /** @brief Source height in pixels. */
        [[nodiscard]] int height() const { return frameHeight; }
        /** @brief Source frame rate, falling back to 30 fps when unknown. */
        [[nodiscard]] double fps() const { return frameFps; }
        /** @brief True when CUDA hardware decode is active. */
        [[nodiscard]] bool using_hardware_decode() const { return hardwareDecode; }

      private:
        static AVPixelFormat chooseHwFormat(AVCodecContext *ctx, const AVPixelFormat *formats);
        [[nodiscard]] AVPixelFormat selectHwFormat(const AVPixelFormat *formats) const;
        bool initHardwareDevice(const AVCodec *decoder);
        bool decodeNextFrame();
        bool convertFrameToRgba(const AVFrame *decodedFrame, std::vector<uint8_t> &rgba, int &width, int &height, int &pitch, bool flipY);
#ifdef MXVK_CUDA
        bool convertFrameToGpuRgba(const AVFrame *decodedFrame, cv::cuda::GpuMat &rgba, cv::cuda::Stream &stream, bool flipY);
#endif
        void flipRows(std::vector<uint8_t> &rgba, int pitch) const;

        AVFormatContext *formatCtx = nullptr;
        AVCodecContext *codecCtx = nullptr;
        AVPacket *packet = nullptr;
        AVFrame *frame = nullptr;
        AVFrame *swFrame = nullptr;
        AVBufferRef *hwDeviceCtx = nullptr;
        SwsContext *swsCtx = nullptr;
        int videoStream = -1;
        int frameWidth = 0;
        int frameHeight = 0;
        double frameFps = 30.0;
        AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;
        bool hardwareDecode = false;
#ifdef MXVK_CUDA
        cv::cuda::GpuMat gpuNv12{};
        cv::cuda::GpuMat gpuRgba{};
        cv::cuda::GpuMat gpuFlippedRgba{};
#endif
    };

} // namespace mxvk

#endif
