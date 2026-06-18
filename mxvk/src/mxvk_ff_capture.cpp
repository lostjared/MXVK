/**
 * @file mxvk_ff_capture.cpp
 * @brief Implementation of mxvk::VK_FF_Capture.
 */
#include "mxvk/mxvk_ff_capture.hpp"
#include "mxvk/mxvk_opencv_compat.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <iostream>
#ifdef MXVK_CUDA
#include <cuda_runtime_api.h>
#include <opencv2/cudaarithm.hpp>
#endif

namespace mxvk {

    namespace {
        [[nodiscard]] double rationalToDouble(AVRational rational) {
            if (rational.num <= 0 || rational.den <= 0) {
                return 0.0;
            }
            return av_q2d(rational);
        }
    } // namespace

    VK_FF_Capture::~VK_FF_Capture() {
        close();
    }

    bool VK_FF_Capture::open(const std::string &filename) {
        close();

        if (avformat_open_input(&formatCtx, filename.c_str(), nullptr, nullptr) < 0) {
            std::cout << std::format("mxvk_ff_capture: failed to open file: {}\n", filename);
            close();
            return false;
        }

        if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
            std::cout << std::format("mxvk_ff_capture: failed to read stream info: {}\n", filename);
            close();
            return false;
        }

        const int streamIndex = av_find_best_stream(formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (streamIndex < 0) {
            std::cout << std::format("mxvk_ff_capture: no video stream found: {}\n", filename);
            close();
            return false;
        }
        videoStream = streamIndex;

        AVStream *stream = formatCtx->streams[videoStream];
        const AVCodecParameters *codecParams = stream->codecpar;
        const AVCodec *decoder = avcodec_find_decoder(codecParams->codec_id);
        if (decoder == nullptr) {
            std::cout << "mxvk_ff_capture: no decoder found for video stream\n";
            close();
            return false;
        }

        codecCtx = avcodec_alloc_context3(decoder);
        if (codecCtx == nullptr || avcodec_parameters_to_context(codecCtx, codecParams) < 0) {
            std::cout << "mxvk_ff_capture: failed to create decoder context\n";
            close();
            return false;
        }

        codecCtx->opaque = this;
        if (initHardwareDevice(decoder)) {
            codecCtx->get_format = &VK_FF_Capture::chooseHwFormat;
            codecCtx->hw_device_ctx = av_buffer_ref(hwDeviceCtx);
            hardwareDecode = codecCtx->hw_device_ctx != nullptr;
        }

        if (avcodec_open2(codecCtx, decoder, nullptr) < 0) {
            std::cout << "mxvk_ff_capture: failed to open decoder\n";
            close();
            return false;
        }

        packet = av_packet_alloc();
        frame = av_frame_alloc();
        swFrame = av_frame_alloc();
        if (packet == nullptr || frame == nullptr || swFrame == nullptr) {
            std::cout << "mxvk_ff_capture: failed to allocate decoder frames\n";
            close();
            return false;
        }

        frameWidth = codecCtx->width;
        frameHeight = codecCtx->height;
        frameFps = rationalToDouble(stream->avg_frame_rate);
        if (frameFps <= 0.0) {
            frameFps = rationalToDouble(stream->r_frame_rate);
        }
        if (frameFps <= 0.0) {
            frameFps = 30.0;
        }

        std::cout << std::format(
            "mxvk_ff_capture: opened {} ({}x{}, {:.3f} fps, decode={})\n",
            filename,
            frameWidth,
            frameHeight,
            frameFps,
            hardwareDecode ? "cuda" : "software");
        return true;
    }

    void VK_FF_Capture::close() {
        if (swsCtx != nullptr) {
            sws_freeContext(swsCtx);
            swsCtx = nullptr;
        }
        if (hwDeviceCtx != nullptr) {
            av_buffer_unref(&hwDeviceCtx);
        }
        if (swFrame != nullptr) {
            av_frame_free(&swFrame);
        }
        if (frame != nullptr) {
            av_frame_free(&frame);
        }
        if (packet != nullptr) {
            av_packet_free(&packet);
        }
        if (codecCtx != nullptr) {
            avcodec_free_context(&codecCtx);
        }
        if (formatCtx != nullptr) {
            avformat_close_input(&formatCtx);
        }
        videoStream = -1;
        frameWidth = 0;
        frameHeight = 0;
        frameFps = 30.0;
        hwPixFmt = AV_PIX_FMT_NONE;
        hardwareDecode = false;
    }

    bool VK_FF_Capture::readRgba(std::vector<uint8_t> &rgba, int &width, int &height, int &pitch, bool flipY) {
        if (!is_open()) {
            return false;
        }

        if (!decodeNextFrame()) {
            return false;
        }
        const bool converted = convertFrameToRgba(frame, rgba, width, height, pitch, flipY);
        av_frame_unref(frame);
        return converted;
    }

#ifdef MXVK_CUDA
    bool VK_FF_Capture::readGpuRgba(cv::cuda::GpuMat &rgba, cv::cuda::Stream &stream, bool flipY) {
        if (!is_open()) {
            return false;
        }

        if (!decodeNextFrame()) {
            return false;
        }
        const bool converted = convertFrameToGpuRgba(frame, rgba, stream, flipY);
        av_frame_unref(frame);
        return converted;
    }
#endif

    bool VK_FF_Capture::decodeNextFrame() {
        bool draining = false;
        while (true) {
            const int receiveResult = avcodec_receive_frame(codecCtx, frame);
            if (receiveResult == 0) {
                return true;
            }
            if (receiveResult == AVERROR_EOF) {
                return false;
            }
            if (receiveResult != AVERROR(EAGAIN)) {
                return false;
            }
            if (draining) {
                return false;
            }

            while (true) {
                const int readResult = av_read_frame(formatCtx, packet);
                if (readResult < 0) {
                    draining = true;
                    avcodec_send_packet(codecCtx, nullptr);
                    break;
                }

                if (packet->stream_index == videoStream) {
                    const int sendResult = avcodec_send_packet(codecCtx, packet);
                    av_packet_unref(packet);
                    if (sendResult == 0 || sendResult == AVERROR(EAGAIN)) {
                        break;
                    }
                    return false;
                }
                av_packet_unref(packet);
            }
        }
    }

    AVPixelFormat VK_FF_Capture::chooseHwFormat(AVCodecContext *ctx, const AVPixelFormat *formats) {
        const auto *capture = static_cast<const VK_FF_Capture *>(ctx->opaque);
        if (capture == nullptr) {
            return formats[0];
        }
        return capture->selectHwFormat(formats);
    }

    AVPixelFormat VK_FF_Capture::selectHwFormat(const AVPixelFormat *formats) const {
        for (const AVPixelFormat *format = formats; *format != AV_PIX_FMT_NONE; ++format) {
            if (*format == hwPixFmt) {
                return *format;
            }
        }
        std::cout << "mxvk_ff_capture: requested CUDA pixel format is unavailable; decoder will use software frames\n";
        return formats[0];
    }

    bool VK_FF_Capture::initHardwareDevice(const AVCodec *decoder) {
        const AVHWDeviceType deviceType = av_hwdevice_find_type_by_name("cuda");
        if (deviceType == AV_HWDEVICE_TYPE_NONE) {
            return false;
        }

        for (int index = 0;; ++index) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, index);
            if (config == nullptr) {
                return false;
            }
            const bool hasDeviceCtx = (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0;
            if (hasDeviceCtx && config->device_type == deviceType) {
                hwPixFmt = config->pix_fmt;
                break;
            }
        }

        if (av_hwdevice_ctx_create(&hwDeviceCtx, deviceType, nullptr, nullptr, 0) < 0) {
            hwPixFmt = AV_PIX_FMT_NONE;
            return false;
        }

        return true;
    }

    bool VK_FF_Capture::convertFrameToRgba(const AVFrame *decodedFrame, std::vector<uint8_t> &rgba, int &width, int &height, int &pitch, bool flipY) {
        const AVFrame *sourceFrame = decodedFrame;
        if (decodedFrame->format == hwPixFmt && hwPixFmt != AV_PIX_FMT_NONE) {
            av_frame_unref(swFrame);
            if (av_hwframe_transfer_data(swFrame, decodedFrame, 0) < 0) {
                std::cout << "mxvk_ff_capture: failed to transfer CUDA decoded frame to host memory\n";
                return false;
            }
            sourceFrame = swFrame;
        }

        width = sourceFrame->width;
        height = sourceFrame->height;
        pitch = width * 4;
        if (width <= 0 || height <= 0) {
            return false;
        }

        rgba.resize(static_cast<size_t>(pitch) * static_cast<size_t>(height));
        uint8_t *dstData[4] = {rgba.data(), nullptr, nullptr, nullptr};
        int dstLinesize[4] = {pitch, 0, 0, 0};

        swsCtx = sws_getCachedContext(
            swsCtx,
            sourceFrame->width,
            sourceFrame->height,
            static_cast<AVPixelFormat>(sourceFrame->format),
            width,
            height,
            AV_PIX_FMT_RGBA,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr);
        if (swsCtx == nullptr) {
            return false;
        }

        const int scaledRows = sws_scale(swsCtx, sourceFrame->data, sourceFrame->linesize, 0, sourceFrame->height, dstData, dstLinesize);
        if (scaledRows != height) {
            return false;
        }
        if (flipY) {
            flipRows(rgba, pitch);
        }
        return true;
    }

#ifdef MXVK_CUDA
    bool VK_FF_Capture::convertFrameToGpuRgba(const AVFrame *decodedFrame, cv::cuda::GpuMat &rgba, cv::cuda::Stream &stream, bool flipY) {
        if (decodedFrame->format == hwPixFmt && hwPixFmt != AV_PIX_FMT_NONE && decodedFrame->hw_frames_ctx != nullptr) {
            const auto *framesContext = reinterpret_cast<const AVHWFramesContext *>(decodedFrame->hw_frames_ctx->data);
            if (framesContext != nullptr && framesContext->sw_format == AV_PIX_FMT_NV12) {
                const int width = decodedFrame->width;
                const int height = decodedFrame->height;
                if (width <= 0 || height <= 0 || decodedFrame->data[0] == nullptr || decodedFrame->data[1] == nullptr) {
                    return false;
                }

                gpuNv12.create(height + (height / 2), width, CV_8UC1);
                cudaStream_t cudaStream = mxvk::cuda_stream_handle(stream);
                cudaError_t result = cudaMemcpy2DAsync(
                    gpuNv12.ptr(),
                    gpuNv12.step,
                    decodedFrame->data[0],
                    static_cast<size_t>(decodedFrame->linesize[0]),
                    static_cast<size_t>(width),
                    static_cast<size_t>(height),
                    cudaMemcpyDeviceToDevice,
                    cudaStream);
                if (result != cudaSuccess) {
                    std::cout << "mxvk_ff_capture: CUDA NV12 luma copy failed: " << cudaGetErrorString(result) << "\n";
                    return false;
                }

                result = cudaMemcpy2DAsync(
                    gpuNv12.ptr(height),
                    gpuNv12.step,
                    decodedFrame->data[1],
                    static_cast<size_t>(decodedFrame->linesize[1]),
                    static_cast<size_t>(width),
                    static_cast<size_t>(height / 2),
                    cudaMemcpyDeviceToDevice,
                    cudaStream);
                if (result != cudaSuccess) {
                    std::cout << "mxvk_ff_capture: CUDA NV12 chroma copy failed: " << cudaGetErrorString(result) << "\n";
                    return false;
                }

                try {
                    cv::cuda::cvtColor(gpuNv12, gpuRgba, cv::COLOR_YUV2RGBA_NV12, 0, stream);
                    if (flipY) {
                        cv::cuda::flip(gpuRgba, gpuFlippedRgba, 0, stream);
                        rgba = gpuFlippedRgba;
                    } else {
                        rgba = gpuRgba;
                    }
                    return true;
                } catch (const cv::Exception &e) {
                    std::cout << "mxvk_ff_capture: CUDA NV12 to RGBA conversion failed; falling back to host conversion: " << e.what() << "\n";
                }
            }
        }

        std::vector<uint8_t> hostRgba;
        int width = 0;
        int height = 0;
        int pitch = 0;
        if (!convertFrameToRgba(decodedFrame, hostRgba, width, height, pitch, flipY) || pitch != width * 4) {
            return false;
        }
        cv::Mat hostFrame(height, width, CV_8UC4, hostRgba.data(), static_cast<size_t>(pitch));
        gpuRgba.upload(hostFrame, stream);
        stream.waitForCompletion();
        rgba = gpuRgba;
        return true;
    }
#endif

    void VK_FF_Capture::flipRows(std::vector<uint8_t> &rgba, int pitch) const {
        std::vector<uint8_t> row(static_cast<size_t>(pitch));
        for (int top = 0, bottom = frameHeight - 1; top < bottom; ++top, --bottom) {
            auto *topPtr = rgba.data() + static_cast<size_t>(top) * static_cast<size_t>(pitch);
            auto *bottomPtr = rgba.data() + static_cast<size_t>(bottom) * static_cast<size_t>(pitch);
            std::memcpy(row.data(), topPtr, static_cast<size_t>(pitch));
            std::memcpy(topPtr, bottomPtr, static_cast<size_t>(pitch));
            std::memcpy(bottomPtr, row.data(), static_cast<size_t>(pitch));
        }
    }

} // namespace mxvk
