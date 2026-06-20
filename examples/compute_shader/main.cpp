#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_cv.hpp"
#include "mxvk/mxvk_exception.hpp"
#if defined(MXVK_WITH_FFMPEG_CAPTURE)
#include "mxvk/mxvk_ff_capture.hpp"
#endif
#include "mxvk/mxvk_opencv_compat.hpp"
#if defined(MXWRITE_ENABLED)
#include "mxwrite.hpp"
#endif
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <vector>
#include <string_view>
#ifdef MXVK_CUDA
#include <cuda_runtime_api.h>
#include <opencv2/core/cuda.hpp>
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/cudawarping.hpp>
#include <unistd.h>
#endif

#ifndef compute_shader_ASSET_DIR
#define compute_shader_ASSET_DIR "."
#endif

static constexpr int HISTORY_SIZE = 8;
static constexpr const char *MODE_SHADER_NAME = "acidcam_filters.spv";
static constexpr std::array<std::string_view, 50> ACIDCAM_FILTER_MODE_NAMES = {
    "Block Pixelate",
    "Block Mirror X",
    "Block Mirror Y",
    "Combine Pixels",
    "History XOR",
    "Temporal Blend",
    "Scanline Warp",
    "RGB Split",
    "Horizontal Mirror",
    "Vertical Mirror",
    "Kaleidoscope",
    "Dynamic Kaleidoscope",
    "Negate",
    "Posterize",
    "Threshold",
    "Gamma Darken",
    "Brightness Contrast",
    "Sepia",
    "Solarize",
    "Hue Rotate",
    "Saturate",
    "Desaturate",
    "Box Blur",
    "Sharpen",
    "Emboss",
    "Sobel",
    "Edge Detect",
    "Dilate",
    "Erode",
    "Posterize Scale",
    "Wave",
    "Ripple",
    "Twirl",
    "Zoom Pulse",
    "Crosshatch",
    "Noise Grain",
    "Strobe Bars",
    "Scanline XOR",
    "Block Shuffle",
    "Diagonal Slice",
    "Frame Blend",
    "Trail Blend",
    "History Median",
    "Row Blend",
    "Column Blend",
    "Color Cycle",
    "Gradient Ramp",
    "Flash Invert",
    "XOR Grid",
    "Mirror Trail",
};

struct ComputePC {
    int32_t mode;
    int32_t historyCount;
    int32_t historyIdx;
    int32_t square_size;
    int32_t history_dir;
    float alpha;
    int32_t do_swap;
    int32_t do_invert;
};

class ComputeWindow : public mxvk::VK_Window {
  public:
    explicit ComputeWindow(const Arguments &args)
        : mxvk::VK_Window("-[ VK Compute CV ]-", args.width, args.height, args.fullscreen, MXVK_VALIDATION),
          assetRoot(args.path.empty() ? std::string(compute_shader_ASSET_DIR) : args.path),
          inputFilename(args.filename),
          usingFile(!inputFilename.empty()),
          fastMode(args.fast),
          explicitResolution(args.resolutionSpecified),
          fullscreenMode(args.fullscreen),
          outputFilename(args.output),
          outputCrf(args.crf),
          encodePreset(args.encodePreset),
          encodeTune(args.encodeTune),
          encodeCodec(args.encodeCodec),
          encodeRealtime(args.encodeRealtime),
          mxwriteBlockWhenFull(args.mxwriteBlockWhenFull),
          repeat(args.repeat),
          cameraIndex(args.camera_index),
          requestedShaderIndex(args.shader_index),
          initialShaderMode(
              args.index > 0
                  ? std::clamp(args.index - 1, 0, static_cast<int>(ACIDCAM_FILTER_MODE_NAMES.size()) - 1)
                  : 0) {
        recordWidth = args.width;
        recordHeight = args.height;
        shaderMode = initialShaderMode;
        initComputeResources();
    }

    ~ComputeWindow() override {
        capture.close();
#if defined(MXVK_WITH_FFMPEG_CAPTURE)
        ffCapture.close();
#endif
        destroyComputeResources();
    }

    void proc() override {
        bool frameUploaded = false;
        if (usingFile && !fastMode) {
            throttleVideoPlayback();
        }

#if defined(MXVK_WITH_FFMPEG_CAPTURE)
        if (usingFfCapture) {
            frameUploaded = readFfFrameToCompute();
            if (!frameUploaded && usingFile) {
                if (repeat) {
                    restartFfCapture();
                    frameUploaded = readFfFrameToCompute();
                } else {
                    std::cout << "compute_shader: video file reached EOF, shutting down\n";
                    exit();
                    return;
                }
            }
        } else
#endif
        {
            cv::Mat frame;
#ifdef MXVK_CUDA
            cv::cuda::GpuMat gpuFrame;
            if (capture.readGpuRgba(gpuFrame) && !gpuFrame.empty()) {
                frameUploaded = uploadGpuFrameToCompute(gpuFrame, capture.cudaStream());
                if (frameUploaded && !captureUploadPathLogged) {
                    std::cout << "compute_shader: CUDA interop capture path active: capture -> GpuMat -> optional CUDA resize -> cudaMemcpy2DToArrayAsync -> Vulkan compute storage image (no download)\n";
                    captureUploadPathLogged = true;
                }
            }
#endif
            if (!frameUploaded && capture.readRgba(frame) && !frame.empty()) {
                if (!captureUploadPathLogged) {
#ifdef MXVK_CUDA
                    std::cout << "compute_shader: CUDA interop unavailable; fallback path active: CUDA/CPU RGBA -> optional CPU resize -> Vulkan staging upload\n";
#else
                    std::cout << "compute_shader: CPU capture path active: readRgba converts to RGBA, optional CPU resize, then uploads through Vulkan staging\n";
#endif
                    captureUploadPathLogged = true;
                }
                uploadCpuFrameToCompute(frame);
                frameUploaded = true;
            } else if (!frameUploaded && usingFile) {
                if (repeat) {
                    capture.close();
                    if (capture.open(inputFilename)) {
                        configureVideoPlaybackRate();
                        cv::Mat restartedFrame;
                        if (capture.readRgba(restartedFrame) && !restartedFrame.empty()) {
                            if (!captureUploadPathLogged) {
#ifdef MXVK_CUDA
                                std::cout << "compute_shader: CUDA interop unavailable; fallback path active: CUDA/CPU RGBA -> optional CPU resize -> Vulkan staging upload\n";
#else
                                std::cout << "compute_shader: CPU capture path active: readRgba converts to RGBA, optional CPU resize, then uploads through Vulkan staging\n";
#endif
                                captureUploadPathLogged = true;
                            }
                            uploadCpuFrameToCompute(restartedFrame);
                            frameUploaded = true;
                        }
                    }
                } else {
                    std::cout << "compute_shader: video file reached EOF, shutting down\n";
                    exit();
                    return;
                }
            }
        }

        if (frameUploaded) {
            tickAnimState();
            runComputeFrame();
            recordProcessedFrame();
        }
        updateFpsOverlay(frameUploaded);
    }

    void onSwapchainRecreated() override {
        rebuildDisplayPipeline();
    }

    void onRecordCustomRendering(VkCommandBuffer cmd, [[maybe_unused]] uint32_t imageIndex) override {
        renderComputeOutput(cmd);
    }

    void event(SDL_Event &e) override {
        if (e.type == SDL_EVENT_QUIT ||
            (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) {
            exit();
            return;
        }

        if (e.type == SDL_EVENT_KEY_DOWN && !spvFiles.empty()) {
            if (e.key.key == SDLK_UP) {
                currentSpvIndex =
                    (currentSpvIndex - 1 + static_cast<int>(spvFiles.size())) % static_cast<int>(spvFiles.size());
                std::cout << "Current index: " << spvFiles[currentSpvIndex] << "\n";
                reloadPipeline();
            } else if (e.key.key == SDLK_DOWN) {
                currentSpvIndex = (currentSpvIndex + 1) % static_cast<int>(spvFiles.size());
                std::cout << "Current index: " << spvFiles[currentSpvIndex] << "\n";
                reloadPipeline();
            } else if (spvFiles[currentSpvIndex] == MODE_SHADER_NAME &&
                       (e.key.key == SDLK_LEFT || e.key.key == SDLK_RIGHT)) {
                const int delta = (e.key.key == SDLK_LEFT) ? -1 : 1;
                shaderMode = (shaderMode + delta + 50) % 50;
                std::cout << "Mode shader mode: " << shaderMode << "\n";
            }
        }
    }

  private:
    struct ComputeImage {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
#ifdef MXVK_CUDA
        VkDeviceSize cudaExportMemorySize = 0;
        cudaExternalMemory_t cudaExternalMemory = nullptr;
        cudaMipmappedArray_t cudaMipmappedArray = nullptr;
        cudaArray_t cudaArray = nullptr;
        bool cudaInteropEnabled = false;
        bool cudaInteropUnavailableLogged = false;
        bool cudaUploadLogged = false;
        bool cudaBarrierLogged = false;
#endif
    };

    std::string assetRoot;
    mxvk::VK_Capture capture{};
#if defined(MXVK_WITH_FFMPEG_CAPTURE)
    mxvk::VK_FF_Capture ffCapture{};
    bool usingFfCapture = false;
    std::vector<uint8_t> ffFrameRgba{};
    int ffFramePitch = 0;
#ifdef MXVK_CUDA
    cv::cuda::Stream ffCudaStream{};
#endif
#endif
    std::string inputFilename;
    bool usingFile = false;
    bool fastMode = false;
    bool explicitResolution = false;
    bool fullscreenMode = false;
    int recordWidth = 1920;
    int recordHeight = 1080;
    int sourceWidth = 1920;
    int sourceHeight = 1080;
    std::string outputFilename;
    std::string outputCrf;
    std::string encodePreset;
    std::string encodeTune;
    std::string encodeCodec;
    bool encodeRealtime = false;
    bool mxwriteBlockWhenFull = false;
    bool repeat = false;
    mxvk::Font fpsFont{};
    int cameraIndex = 0;
    int texWidth = 1920;
    int texHeight = 1080;

    std::array<ComputeImage, 2> workImg{};
    std::array<ComputeImage, HISTORY_SIZE> histImg{};
    ComputeImage outImg{};

    VkSampler computeSampler = VK_NULL_HANDLE;

    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VkBuffer readbackBuf = VK_NULL_HANDLE;
    VkDeviceMemory readbackMem = VK_NULL_HANDLE;

    VkDescriptorSetLayout compDSLayout = VK_NULL_HANDLE;
    VkPipelineLayout compPipeLayout = VK_NULL_HANDLE;
    VkPipeline compPipeline = VK_NULL_HANDLE;
    VkDescriptorPool compDSPool = VK_NULL_HANDLE;

    std::array<VkDescriptorSet, 2> blurDS{};
    std::array<VkDescriptorSet, 2> blendDS{};

    VkDescriptorSetLayout displayDSLayout = VK_NULL_HANDLE;
    VkDescriptorPool displayDSPool = VK_NULL_HANDLE;
    VkDescriptorSet displayDS = VK_NULL_HANDLE;
    VkPipelineLayout displayPipeLayout = VK_NULL_HANDLE;
    VkPipeline displayPipeline = VK_NULL_HANDLE;
    VkBuffer displayVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory displayVertexMemory = VK_NULL_HANDLE;
    VkBuffer displayIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory displayIndexMemory = VK_NULL_HANDLE;

    int historyIndex = 0;
    int historyCount = 0;
    int currentSquare = 4;
    int squareDir = 1;
    int currentHistIdx = 0;
    int currentDir = 1;
    int requestedShaderIndex = 0;
    int shaderMode = 0;
    int initialShaderMode = 0;
    float alpha = 1.0F;
    bool captureUploadPathLogged = false;
    double videoFps = 0.0;
    double sourceFps = 0.0;
    std::chrono::duration<double> videoFrameInterval{0.0};
    std::chrono::steady_clock::time_point nextVideoFrameDeadline{std::chrono::steady_clock::now()};
    double currentFps = 0.0;
    uint32_t fpsFrameCount = 0;
    std::chrono::steady_clock::time_point fpsSampleTime{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point playbackStartTime{std::chrono::steady_clock::now()};
    std::string fpsText = "FPS: --";
    uint64_t processedVideoFrames = 0;
    bool recordingEnabled = false;
    bool recordingWarningLogged = false;
    bool processedRecordPathLogged = false;
    std::vector<uint8_t> recordScratch{};
#ifdef MXVK_CUDA
    cv::cuda::Stream processedRecordStream{};
    cv::cuda::GpuMat processedRecordGpuFrame{};
    cv::cuda::GpuMat computeInputGpuFrame{};
#endif
#if defined(MXWRITE_ENABLED)
    Writer videoWriter{};
    bool videoWriterOpen = false;
#endif

    std::vector<std::string> spvFiles{};
    int currentSpvIndex = 0;

    void loadSPV() {
        std::ifstream file(assetRoot + "/index.txt");
        if (!file.is_open()) {
            throw mxvk::Exception("Cannot open: " + assetRoot + "/index.txt");
        }

        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty()) {
                spvFiles.push_back(line);
            }
        }

        if (spvFiles.empty()) {
            throw mxvk::Exception("index.txt contains no entries");
        }

        currentSpvIndex =
            std::clamp(requestedShaderIndex, 0, static_cast<int>(spvFiles.size()) - 1);
    }

    [[nodiscard]] double configureCameraFps() {
        static constexpr std::array<double, 3> fpsChoices = {60.0, 30.0, 24.0};

        for (const double requestedFps : fpsChoices) {
            capture.set(cv::CAP_PROP_FPS, requestedFps);
            const double reportedFps = capture.get(cv::CAP_PROP_FPS);
            if (reportedFps > 0.0 && reportedFps + 0.5 >= requestedFps) {
                return reportedFps;
            }
        }

        capture.set(cv::CAP_PROP_FPS, fpsChoices.back());
        const double reportedFps = capture.get(cv::CAP_PROP_FPS);
        return (reportedFps > 0.0) ? reportedFps : fpsChoices.back();
    }

    void resetVideoPlaybackClock() {
        nextVideoFrameDeadline = std::chrono::steady_clock::now();
    }

    void configureVideoPlaybackRate() {
        double reportedFps = 0.0;
#if defined(MXVK_WITH_FFMPEG_CAPTURE)
        if (usingFfCapture) {
            reportedFps = ffCapture.fps();
        } else
#endif
        {
            reportedFps = capture.get(cv::CAP_PROP_FPS);
        }
        videoFps = (reportedFps > 0.0) ? reportedFps : 30.0;
        sourceFps = videoFps;
        if (videoFps <= 0.0) {
            videoFps = 30.0;
            sourceFps = videoFps;
        }
        videoFrameInterval = std::chrono::duration<double>(1.0 / videoFps);
        resetVideoPlaybackClock();
        std::cout << "compute_shader: video file FPS " << videoFps
                  << " fps, fast=" << (fastMode ? "true" : "false") << "\n";
    }

    bool openVideoSource() {
#if defined(MXVK_WITH_FFMPEG_CAPTURE)
        usingFfCapture = false;
        if (ffCapture.open(inputFilename)) {
            usingFfCapture = true;
            std::cout << "compute_shader: FFmpeg capture path active for file input"
                      << (ffCapture.using_hardware_decode() ? " (CUDA decode)\n" : " (software decode)\n");
            return true;
        }
        std::cout << "compute_shader: FFmpeg capture failed; falling back to VK_Capture/OpenCV file input\n";
#endif
        return capture.open(inputFilename);
    }

    void uploadCpuFrameToCompute(const cv::Mat &frame) {
        if (frame.empty()) {
            return;
        }
        if (frame.cols == texWidth && frame.rows == texHeight) {
            uploadToImage(frame.ptr(), static_cast<int>(frame.step), workImg[0]);
            return;
        }

        cv::Mat resizedFrame;
        cv::resize(frame, resizedFrame, cv::Size(texWidth, texHeight), 0.0, 0.0, cv::INTER_LINEAR);
        uploadToImage(resizedFrame.ptr(), static_cast<int>(resizedFrame.step), workImg[0]);
    }

#ifdef MXVK_CUDA
    bool uploadGpuFrameToCompute(const cv::cuda::GpuMat &gpuFrame, cv::cuda::Stream &stream) {
        if (gpuFrame.empty()) {
            return false;
        }
        if (gpuFrame.cols == texWidth && gpuFrame.rows == texHeight) {
            return uploadGpuToImage(gpuFrame, stream, workImg[0]);
        }

        cv::cuda::resize(gpuFrame, computeInputGpuFrame, cv::Size(texWidth, texHeight), 0.0, 0.0, cv::INTER_LINEAR, stream);
        stream.waitForCompletion();
        return uploadGpuToImage(computeInputGpuFrame, stream, workImg[0]);
    }
#endif

#if defined(MXVK_WITH_FFMPEG_CAPTURE)
    void restartFfCapture() {
        ffCapture.close();
        if (ffCapture.open(inputFilename)) {
            usingFfCapture = true;
            configureVideoPlaybackRate();
        }
    }

    bool readFfFrameToCompute() {
#ifdef MXVK_CUDA
        if (ffCapture.using_hardware_decode()) {
            cv::cuda::GpuMat gpuFrame;
            if (ffCapture.readGpuRgba(gpuFrame, ffCudaStream) && !gpuFrame.empty()) {
                const bool uploadedWithInterop = uploadGpuFrameToCompute(gpuFrame, ffCudaStream);
                if (uploadedWithInterop) {
                    if (!captureUploadPathLogged) {
                        std::cout << "compute_shader: FFmpeg CUDA path active: NVDEC/CUDA decode -> CUDA NV12/RGBA conversion -> optional CUDA resize -> cudaMemcpy2DToArrayAsync -> Vulkan compute storage image (no CPU download)\n";
                        captureUploadPathLogged = true;
                    }
                    return true;
                }

                cv::Mat cpuFrame;
                gpuFrame.download(cpuFrame, ffCudaStream);
                ffCudaStream.waitForCompletion();
                if (!cpuFrame.empty()) {
                    if (!captureUploadPathLogged) {
                        std::cout << "compute_shader: FFmpeg CUDA decode active, Vulkan CUDA interop unavailable; downloading RGBA for optional CPU resize and staging upload\n";
                        captureUploadPathLogged = true;
                    }
                    uploadCpuFrameToCompute(cpuFrame);
                    return true;
                }
            }
        }
#endif
        int frameWidth = 0;
        int frameHeight = 0;
        if (!ffCapture.readRgba(ffFrameRgba, frameWidth, frameHeight, ffFramePitch) || ffFrameRgba.empty()) {
            return false;
        }
        if (frameWidth <= 0 || frameHeight <= 0) {
            return false;
        }
        if (!captureUploadPathLogged) {
            std::cout << "compute_shader: FFmpeg capture path active: decoded RGBA -> optional CPU resize -> Vulkan staging upload\n";
            captureUploadPathLogged = true;
        }
        cv::Mat frame(frameHeight, frameWidth, CV_8UC4, ffFrameRgba.data(), static_cast<size_t>(ffFramePitch));
        uploadCpuFrameToCompute(frame);
        return true;
    }
#endif

    void configureRecordingDefaults() {
        if (outputFilename.empty()) {
            outputFilename = assetRoot + "/compute_shader_output.mp4";
        }
        if (outputCrf.empty()) {
            outputCrf = "24";
        }
    }

    [[nodiscard]] int overlayFontSizeForCanvas() const {
        const int canvasMinDim = std::min(texWidth, texHeight);
        return std::clamp(canvasMinDim / 30, 8, 36);
    }

    void maybeResizeWindowToSource() {
        if (usingFile && !explicitResolution && !fullscreenMode && getSDLWindow() != nullptr) {
            SDL_SetWindowSize(getSDLWindow(), texWidth, texHeight);
            std::cout << "compute_shader: window resized to source frame size " << texWidth << "x" << texHeight
                      << " (pass -r/--resolution to override)\n";
        }
    }

#if defined(MXWRITE_ENABLED)
    [[nodiscard]] int parseCrf() const {
        try {
            size_t parsedChars = 0;
            const int value = std::stoi(outputCrf, &parsedChars);
            if (parsedChars == outputCrf.size() && value >= 0 && value <= 51) {
                return value;
            }
        } catch (const std::exception &) {
        }
        throw mxvk::Exception("compute_shader: invalid CRF '" + outputCrf + "'; expected integer 0..51");
    }

    void openVideoWriter() {
        if (videoWriterOpen) {
            return;
        }
        if (sourceFps <= 0.0) {
            sourceFps = usingFile ? videoFps : 30.0;
        }
        if (sourceFps <= 0.0) {
            sourceFps = 30.0;
        }
        EncodeOptions encodeOptions{};
        encodeOptions.crf = parseCrf();
        if (!encodePreset.empty()) {
            encodeOptions.preset = encodePreset;
        }
        if (!encodeTune.empty()) {
            encodeOptions.tune = encodeTune;
        }
        if (!encodeCodec.empty()) {
            encodeOptions.codec = encodeCodec;
        }
        encodeOptions.realtime = encodeRealtime;

        videoWriter.set_block_when_full(mxwriteBlockWhenFull);
        if (!videoWriter.open(outputFilename, recordWidth, recordHeight, static_cast<float>(sourceFps), encodeOptions)) {
            throw mxvk::Exception("compute_shader: failed to open MXWrite output file '" + outputFilename + "'");
        }
        videoWriterOpen = true;
        std::cout << "compute_shader: recording to " << outputFilename << " at " << sourceFps
                  << " fps with crf " << encodeOptions.crf
                  << ", codec=" << encodeOptions.codec
                  << ", preset=" << encodeOptions.preset
                  << ", tune=" << (encodeOptions.tune.empty() ? "none" : encodeOptions.tune)
                  << ", realtime=" << (encodeOptions.realtime ? "true" : "false")
                  << ", block_when_full=" << (mxwriteBlockWhenFull ? "true" : "false") << "\n";
    }

    void recordFrame(const cv::Mat &frame) {
        if (videoWriterOpen && !frame.empty()) {
            recordFrame(frame.ptr(), frame.cols, frame.rows, static_cast<int>(frame.step));
        }
    }

    void recordFrame(const uint8_t *data, int width, int height, int pitch) {
        if (!videoWriterOpen || data == nullptr || width != recordWidth || height != recordHeight) {
            return;
        }

        const int tightPitch = recordWidth * 4;
        if (pitch == tightPitch) {
            videoWriter.write(const_cast<uint8_t *>(data));
            return;
        }

        if (pitch < tightPitch) {
            return;
        }

        const int recordTightPitch = recordWidth * 4;
        recordScratch.resize(static_cast<size_t>(recordTightPitch) * static_cast<size_t>(recordHeight));
        for (int row = 0; row < recordHeight; ++row) {
            std::memcpy(recordScratch.data() + static_cast<size_t>(row) * static_cast<size_t>(tightPitch),
                        data + static_cast<size_t>(row) * static_cast<size_t>(pitch),
                        static_cast<size_t>(recordTightPitch));
        }
        videoWriter.write(recordScratch.data());
    }

#ifdef MXVK_CUDA
    void recordGpuFrame(cv::cuda::GpuMat &gpuFrame) {
        if (!videoWriterOpen || gpuFrame.empty()) {
            return;
        }
#if defined(MXWRITE_HAS_CUDA_COPY)
        if (videoWriter.is_hardware_encode() &&
            videoWriter.write_cuda_rgba(gpuFrame.ptr(), static_cast<int>(gpuFrame.step))) {
            return;
        }
#endif
        cv::Mat cpuFrame;
        gpuFrame.download(cpuFrame);
        recordFrame(cpuFrame);
    }
#endif
#else
    void openVideoWriter() {
        if (!recordingEnabled) {
            return;
        }
        if (!recordingWarningLogged) {
            std::cout << "compute_shader: MXWrite is unavailable; video recording disabled\n";
            recordingWarningLogged = true;
        }
        recordingEnabled = false;
    }

    void recordFrame(const cv::Mat &) {}
    void recordFrame(const uint8_t *, int, int, int) {}
#ifdef MXVK_CUDA
    void recordGpuFrame(cv::cuda::GpuMat &) {}
#endif
#endif

#ifdef MXVK_CUDA
    bool recordProcessedFrameCuda() {
        if (!recordingEnabled || !ensureCudaInterop(outImg)) {
            return false;
        }

        const VkCommandBuffer cmd = beginSingleTimeCommands();
        transitionImageLayout(
            cmd,
            outImg.image,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT);
        endSingleTimeCommands(cmd);

        processedRecordGpuFrame.create(texHeight, texWidth, CV_8UC4);
        cudaStream_t cudaStream = mxvk::cuda_stream_handle(processedRecordStream);
        cudaError_t cudaResult = cudaMemcpy2DFromArrayAsync(
            processedRecordGpuFrame.ptr(),
            processedRecordGpuFrame.step,
            outImg.cudaArray,
            0,
            0,
            static_cast<size_t>(texWidth) * 4U,
            static_cast<size_t>(texHeight),
            cudaMemcpyDeviceToDevice,
            cudaStream);
        if (cudaResult != cudaSuccess) {
            std::cout << "compute_shader: CUDA processed-frame readback failed: " << cudaGetErrorString(cudaResult) << "\n";
            return false;
        }

        cudaResult = cudaStreamSynchronize(cudaStream);
        if (cudaResult != cudaSuccess) {
            std::cout << "compute_shader: CUDA processed-frame readback sync failed: " << cudaGetErrorString(cudaResult) << "\n";
            return false;
        }

        if (!processedRecordPathLogged) {
            std::cout << "compute_shader: processed recording path active: Vulkan compute output image -> CUDA array -> RGBA GpuMat -> MXWrite"
#if defined(MXWRITE_HAS_CUDA_COPY)
                      << " CUDA ingestion when hardware encode is active"
#else
                      << " CPU fallback when MXWrite CUDA ingestion is unavailable"
#endif
                      << "\n";
            processedRecordPathLogged = true;
        }
        recordGpuFrame(processedRecordGpuFrame);
        return true;
    }
#endif

    void recordProcessedFrame() {
        if (!recordingEnabled || readbackBuf == VK_NULL_HANDLE || readbackMem == VK_NULL_HANDLE) {
            return;
        }

#ifdef MXVK_CUDA
        if (recordProcessedFrameCuda()) {
            return;
        }
#endif

        const int tightPitch = texWidth * 4;
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(tightPitch) * static_cast<VkDeviceSize>(texHeight);
        const VkCommandBuffer cmd = beginSingleTimeCommands();

        transitionImageLayout(
            cmd,
            outImg.image,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT);

        VkBufferImageCopy2 region{};
        region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1};

        VkCopyImageToBufferInfo2 copyInfo{};
        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
        copyInfo.srcImage = outImg.image;
        copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copyInfo.dstBuffer = readbackBuf;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;
        vkCmdCopyImageToBuffer2(cmd, &copyInfo);

        transitionImageLayout(
            cmd,
            outImg.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        endSingleTimeCommands(cmd);

        void *mapped = nullptr;
        VK_CHECK_RESULT(vkMapMemory(device, readbackMem, 0, bytes, 0, &mapped));
        cv::Mat sourceFrame(texHeight, texWidth, CV_8UC4, mapped, tightPitch);
        if (!processedRecordPathLogged) {
            std::cout << "compute_shader: processed recording path active: Vulkan compute output image -> readback buffer -> MXWrite\n";
            processedRecordPathLogged = true;
        }
        recordFrame(sourceFrame);
        vkUnmapMemory(device, readbackMem);
    }

    void throttleVideoPlayback() {
        if (!usingFile || fastMode || videoFps <= 0.0) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (nextVideoFrameDeadline > now) {
            std::this_thread::sleep_until(nextVideoFrameDeadline);
        }
        nextVideoFrameDeadline += std::chrono::duration_cast<std::chrono::steady_clock::duration>(videoFrameInterval);
    }

    void initComputeResources() {
        try {
            if (device == VK_NULL_HANDLE) {
                throw mxvk::Exception("Compute resources require an initialized Vulkan device");
            }
            if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
                createDevice();
            }
            setFont(assetRoot + "/font.ttf", 20);

            if (usingFile) {
                if (!openVideoSource()) {
                    throw mxvk::Exception("Failed to open video file " + inputFilename);
                }
                configureVideoPlaybackRate();
            } else if (!capture.open(cameraIndex)) {
                throw mxvk::Exception("Failed to open camera " + std::to_string(cameraIndex));
            }

            if (!usingFile) {
                capture.set(cv::CAP_PROP_FRAME_WIDTH, 1920.0);
                capture.set(cv::CAP_PROP_FRAME_HEIGHT, 1080.0);
                const double selectedFps = configureCameraFps();
                sourceFps = selectedFps;
                std::cout << "compute_shader: requested camera FPS fallback order 60 -> 30 -> 24; selected "
                          << selectedFps << " fps\n";
            }

#if defined(MXVK_WITH_FFMPEG_CAPTURE)
            if (usingFfCapture) {
                sourceWidth = ffCapture.width();
                sourceHeight = ffCapture.height();
                if (sourceWidth <= 0 || sourceHeight <= 0) {
                    throw mxvk::Exception("Failed to query FFmpeg video dimensions");
                }
            } else
#endif
            {
                cv::Mat frame;
                if (!capture.read(frame) || frame.empty()) {
                    throw mxvk::Exception(usingFile ? "Failed to read initial video frame" : "Failed to read initial camera frame");
                }

                sourceWidth = frame.cols;
                sourceHeight = frame.rows;
            }

            if (explicitResolution) {
                texWidth = recordWidth;
                texHeight = recordHeight;
                std::cout << "compute_shader: compute canvas set from explicit resolution " << texWidth << "x" << texHeight
                          << "; source frames are " << sourceWidth << "x" << sourceHeight << "\n";
            } else {
                texWidth = sourceWidth;
                texHeight = sourceHeight;
                recordWidth = texWidth;
                recordHeight = texHeight;
            }

            configureRecordingDefaults();
            recordingEnabled = true;
            openVideoWriter();
            fpsFont.reset(assetRoot + "/font.ttf", overlayFontSizeForCanvas());
            playbackStartTime = std::chrono::steady_clock::now();
            maybeResizeWindowToSource();

            const VkDeviceSize imgBytes = static_cast<VkDeviceSize>(texWidth) * texHeight * 4;

            createBuffer(
                imgBytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuf,
                stagingMem);
            createBuffer(
                imgBytes,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                readbackBuf,
                readbackMem);

            {
                const VkCommandBuffer cmd = beginSingleTimeCommands();
#ifdef MXVK_CUDA
                allocCImg(workImg[0], cmd, true);
#else
                allocCImg(workImg[0], cmd);
#endif
                allocCImg(workImg[1], cmd);
                for (ComputeImage &img : histImg) {
                    allocCImg(img, cmd);
                }
#ifdef MXVK_CUDA
                allocCImg(outImg, cmd, true);
#else
                allocCImg(outImg, cmd);
#endif
                endSingleTimeCommands(cmd);
            }

            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.maxAnisotropy = 1.0F;
            VK_CHECK_RESULT(vkCreateSampler(device, &samplerInfo, nullptr, &computeSampler));

            loadSPV();
            buildDescriptorSetLayout();
            buildComputePipeline();
            buildDescriptorSets();
            createDisplayResources();
        } catch (...) {
            // Constructor failure bypasses ~ComputeWindow; cleanup partial Vulkan state here.
            capture.close();
#if defined(MXVK_WITH_FFMPEG_CAPTURE)
            ffCapture.close();
#endif
            destroyComputeResources();
            throw;
        }
    }

    void updateFpsOverlay(bool frameUploaded) {
        if (!active || !frameUploaded) {
            return;
        }

        try {
            clearTextQueue();
        } catch (const std::exception &ex) {
            std::cerr << "compute_shader: failed to clear stale text overlay queue: " << ex.what() << "\n";
        }

        if (frameUploaded) {
            ++fpsFrameCount;
            ++processedVideoFrames;
        }

        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - fpsSampleTime).count();
        if (elapsed >= 0.25) {
            currentFps = static_cast<double>(fpsFrameCount) / elapsed;
            fpsFrameCount = 0;
            fpsSampleTime = now;
            fpsText = std::format("FPS: {:.1f}", currentFps);
        }

        const auto recElapsed = now - playbackStartTime;
        const uint64_t recTotalSeconds =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(recElapsed).count());
        const uint64_t recHours = recTotalSeconds / 3600U;
        const uint64_t recMinutes = (recTotalSeconds / 60U) % 60U;
        const uint64_t recSeconds = recTotalSeconds % 60U;
        const std::string recText = std::format("Rec: {:02}:{:02}:{:02}", recHours, recMinutes, recSeconds);

        const double effectiveSourceFps = (sourceFps > 0.0) ? sourceFps : videoFps;
        const uint64_t totalTenths = (effectiveSourceFps > 0.0)
                                         ? static_cast<uint64_t>(std::llround((static_cast<double>(processedVideoFrames) / effectiveSourceFps) * 10.0))
                                         : 0U;
        const uint64_t hours = totalTenths / 36000U;
        const uint64_t minutes = (totalTenths / 600U) % 60U;
        const uint64_t seconds = (totalTenths / 10U) % 60U;
        const uint64_t tenths = totalTenths % 10U;
        const std::string timeText = std::format("Output: {:02}:{:02}:{:02}.{:01}", hours, minutes, seconds, tenths);
        const std::string overlayText = std::format(
            "Source FPS: {:.1f} Current FPS: {:.1f} | {} | {}",
            sourceFps,
            currentFps,
            recText,
            timeText);

        printText(overlayText, 18, 18, SDL_Color{255, 240, 0, 255}, fpsFont);
        if (!spvFiles.empty()) {
            if (spvFiles[currentSpvIndex] == MODE_SHADER_NAME) {
                const int modeIndex = std::clamp(shaderMode, 0, static_cast<int>(ACIDCAM_FILTER_MODE_NAMES.size()) - 1);
                const std::string modeText = std::format(
                    "Mode: {} {}/{}",
                    ACIDCAM_FILTER_MODE_NAMES[modeIndex],
                    modeIndex + 1,
                    ACIDCAM_FILTER_MODE_NAMES.size());
                printText(modeText, 15, 68, SDL_Color{255, 105, 180, 255});
            } else {
                const std::string spvText = std::format("{}: {}", currentSpvIndex, spvFiles[currentSpvIndex]);
                printText(spvText, 15, 68, SDL_Color{80, 160, 255, 255});
            }
        }
    }

    [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memProperties{};
        vkGetPhysicalDeviceMemoryProperties(physical_device, &memProperties);

        for (uint32_t index = 0; index < memProperties.memoryTypeCount; ++index) {
            const bool typeMatches = (typeFilter & (1U << index)) != 0U;
            const bool propertyMatches =
                (memProperties.memoryTypes[index].propertyFlags & properties) == properties;
            if (typeMatches && propertyMatches) {
                return index;
            }
        }

        throw mxvk::Exception("Failed to find suitable memory type");
    }

    void createBuffer(VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer &buffer,
                      VkDeviceMemory &bufferMemory) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VK_CHECK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer));

        VkMemoryRequirements memRequirements{};
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory));
        VK_CHECK_RESULT(vkBindBufferMemory(device, buffer, bufferMemory, 0));
    }

    [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = command_pool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo));

        return commandBuffer;
    }

    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
        VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

        VkCommandBufferSubmitInfo commandBufferInfo{};
        commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandBufferInfo.commandBuffer = commandBuffer;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandBufferInfo;

        VK_CHECK_RESULT(vkQueueSubmit2(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK_RESULT(vkQueueWaitIdle(graphics_queue));
        vkFreeCommandBuffers(device, command_pool, 1, &commandBuffer);
    }

    void createImage(uint32_t width,
                     uint32_t height,
                     VkFormat format,
                     VkImageTiling tiling,
                     VkImageUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     VkImage &image,
                     VkDeviceMemory &imageMemory) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = tiling;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VK_CHECK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &image));

        VkMemoryRequirements memRequirements{};
        vkGetImageMemoryRequirements(device, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory));
        VK_CHECK_RESULT(vkBindImageMemory(device, image, imageMemory, 0));
    }

#ifdef MXVK_CUDA
    void createCudaExportableImage(ComputeImage &img) {
        std::cout << "compute_shader: CUDA interop init: requesting exportable compute input image "
                  << texWidth << "x" << texHeight << " RGBA8 optimal-tiled OPAQUE_FD\n";

        VkExternalMemoryImageCreateInfo externalImageInfo{};
        externalImageInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        externalImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.pNext = &externalImageInfo;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = static_cast<uint32_t>(texWidth);
        imageInfo.extent.height = static_cast<uint32_t>(texHeight);
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VK_CHECK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &img.image));

        VkMemoryRequirements memRequirements{};
        vkGetImageMemoryRequirements(device, img.image, &memRequirements);

        VkExportMemoryAllocateInfo exportMemoryInfo{};
        exportMemoryInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
        exportMemoryInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = &exportMemoryInfo;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        try {
            VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &img.memory));
            VK_CHECK_RESULT(vkBindImageMemory(device, img.image, img.memory, 0));
            img.cudaExportMemorySize = memRequirements.size;
            img.cudaInteropUnavailableLogged = false;
            std::cout << "compute_shader: CUDA interop init: exportable compute input image allocated (memorySize="
                      << static_cast<unsigned long long>(memRequirements.size)
                      << " bytes, memoryType=" << allocInfo.memoryTypeIndex
                      << "); optimal image memory will be imported as cudaArray\n";
        } catch (...) {
            if (img.image != VK_NULL_HANDLE) {
                vkDestroyImage(device, img.image, nullptr);
                img.image = VK_NULL_HANDLE;
            }
            if (img.memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, img.memory, nullptr);
                img.memory = VK_NULL_HANDLE;
            }
            img.cudaExportMemorySize = 0;
            throw;
        }
    }

    void destroyCudaInterop(ComputeImage &img) {
        if (img.cudaInteropEnabled || img.cudaExternalMemory != nullptr || img.cudaMipmappedArray != nullptr) {
            std::cout << "compute_shader: CUDA interop: destroying imported compute input image resources\n";
        }
        if (img.cudaMipmappedArray != nullptr) {
            cudaFreeMipmappedArray(img.cudaMipmappedArray);
            img.cudaMipmappedArray = nullptr;
            img.cudaArray = nullptr;
        }
        if (img.cudaExternalMemory != nullptr) {
            cudaDestroyExternalMemory(img.cudaExternalMemory);
            img.cudaExternalMemory = nullptr;
        }
        img.cudaInteropEnabled = false;
        img.cudaExportMemorySize = 0;
        img.cudaUploadLogged = false;
        img.cudaBarrierLogged = false;
    }

    bool ensureCudaInterop(ComputeImage &img) {
        if (img.cudaInteropEnabled) {
            return true;
        }
        if (img.memory == VK_NULL_HANDLE || img.cudaExportMemorySize == 0) {
            if (!img.cudaInteropUnavailableLogged) {
                std::cout << "compute_shader: CUDA interop init: compute input image is not exportable\n";
                img.cudaInteropUnavailableLogged = true;
            }
            return false;
        }
        if (vkGetMemoryFdKHR == nullptr) {
            if (!img.cudaInteropUnavailableLogged) {
                std::cout << "compute_shader: CUDA interop init: vkGetMemoryFdKHR was not loaded\n";
                img.cudaInteropUnavailableLogged = true;
            }
            return false;
        }

        VkMemoryGetFdInfoKHR fdInfo{};
        fdInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
        fdInfo.memory = img.memory;
        fdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        int memoryFd = -1;
        const VkResult fdResult = vkGetMemoryFdKHR(device, &fdInfo, &memoryFd);
        if (fdResult != VK_SUCCESS) {
            if (!img.cudaInteropUnavailableLogged) {
                std::cout << "compute_shader: CUDA interop init: vkGetMemoryFdKHR failed (" << static_cast<int>(fdResult) << ")\n";
                img.cudaInteropUnavailableLogged = true;
            }
            return false;
        }
        std::cout << "compute_shader: CUDA interop init: exported compute input image memory fd=" << memoryFd << "\n";

        cudaExternalMemoryHandleDesc externalMemoryDesc{};
        externalMemoryDesc.type = cudaExternalMemoryHandleTypeOpaqueFd;
        externalMemoryDesc.handle.fd = memoryFd;
        externalMemoryDesc.size = img.cudaExportMemorySize;

        cudaError_t cudaResult = cudaImportExternalMemory(&img.cudaExternalMemory, &externalMemoryDesc);
        if (cudaResult != cudaSuccess) {
            close(memoryFd);
            if (!img.cudaInteropUnavailableLogged) {
                std::cout << "compute_shader: CUDA interop init: cudaImportExternalMemory failed: "
                          << cudaGetErrorString(cudaResult) << "\n";
                img.cudaInteropUnavailableLogged = true;
            }
            img.cudaExternalMemory = nullptr;
            return false;
        }
        std::cout << "compute_shader: CUDA interop init: imported compute input image external memory into CUDA ("
                  << static_cast<unsigned long long>(img.cudaExportMemorySize) << " bytes)\n";

        cudaExternalMemoryMipmappedArrayDesc arrayDesc{};
        arrayDesc.offset = 0;
        arrayDesc.formatDesc = cudaCreateChannelDesc<uchar4>();
        arrayDesc.extent = make_cudaExtent(static_cast<size_t>(texWidth), static_cast<size_t>(texHeight), 0);
        arrayDesc.flags = cudaArrayColorAttachment;
        arrayDesc.numLevels = 1;

        cudaResult = cudaExternalMemoryGetMappedMipmappedArray(&img.cudaMipmappedArray, img.cudaExternalMemory, &arrayDesc);
        if (cudaResult != cudaSuccess) {
            if (!img.cudaInteropUnavailableLogged) {
                std::cout << "compute_shader: CUDA interop init: cudaExternalMemoryGetMappedMipmappedArray failed: "
                          << cudaGetErrorString(cudaResult) << "\n";
                img.cudaInteropUnavailableLogged = true;
            }
            destroyCudaInterop(img);
            return false;
        }
        std::cout << "compute_shader: CUDA interop init: mapped compute input CUDA mipmapped array "
                  << texWidth << "x" << texHeight << " uchar4\n";

        cudaResult = cudaGetMipmappedArrayLevel(&img.cudaArray, img.cudaMipmappedArray, 0);
        if (cudaResult != cudaSuccess) {
            if (!img.cudaInteropUnavailableLogged) {
                std::cout << "compute_shader: CUDA interop init: cudaGetMipmappedArrayLevel failed: "
                          << cudaGetErrorString(cudaResult) << "\n";
                img.cudaInteropUnavailableLogged = true;
            }
            destroyCudaInterop(img);
            return false;
        }

        img.cudaInteropEnabled = true;
        std::cout << "compute_shader: CUDA interop init: direct CUDA-to-compute-input upload is ready\n";
        return true;
    }
#endif

    [[nodiscard]] VkImageView createImageView(VkImage image,
                                              VkFormat format,
                                              VkImageAspectFlags aspectFlags) const {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView = VK_NULL_HANDLE;
        VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &imageView));
        return imageView;
    }

    void allocCImg(ComputeImage &img, VkCommandBuffer cmd, [[maybe_unused]] bool cudaExportable = false) {
#ifdef MXVK_CUDA
        if (cudaExportable) {
            try {
                createCudaExportableImage(img);
            } catch (const std::exception &ex) {
                std::cout << "compute_shader: CUDA exportable input image unavailable: " << ex.what()
                          << "; using Vulkan staging fallback\n";
                createImage(
                    static_cast<uint32_t>(texWidth),
                    static_cast<uint32_t>(texHeight),
                    VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    img.image,
                    img.memory);
            }
        } else
#else
        {
            createImage(
                static_cast<uint32_t>(texWidth),
                static_cast<uint32_t>(texHeight),
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                img.image,
                img.memory);
        }
        img.view = createImageView(img.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

        transitionImageLayout(
            cmd,
            img.image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_NONE,
            VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    }

    void reloadPipeline() {
        vkDeviceWaitIdle(device);
        if (compPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, compPipeline, nullptr);
            compPipeline = VK_NULL_HANDLE;
        }
        if (compPipeLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, compPipeLayout, nullptr);
            compPipeLayout = VK_NULL_HANDLE;
        }
        shaderMode = initialShaderMode;
        buildComputePipeline();
    }

    void buildDescriptorSetLayout() {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = HISTORY_SIZE;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        createInfo.pBindings = bindings.data();
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &compDSLayout));
    }

    void buildComputePipeline() {
        const std::string spvPath = assetRoot + "/" + spvFiles[currentSpvIndex];
        std::ifstream spvFile(spvPath, std::ios::binary | std::ios::ate);
        if (!spvFile.is_open()) {
            throw mxvk::Exception("Cannot open compute SPIR-V: " + spvPath);
        }

        const size_t size = static_cast<size_t>(spvFile.tellg());
        std::vector<char> spv(size);
        spvFile.seekg(0);
        spvFile.read(spv.data(), static_cast<std::streamsize>(size));

        VkShaderModuleCreateInfo moduleInfo{};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = spv.size();
        moduleInfo.pCode = reinterpret_cast<const uint32_t *>(spv.data());
        VkShaderModule module = VK_NULL_HANDLE;
        VK_CHECK_RESULT(vkCreateShaderModule(device, &moduleInfo, nullptr, &module));

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(ComputePC);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &compDSLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &compPipeLayout));

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = module;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = compPipeLayout;
        VK_CHECK_RESULT(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compPipeline));

        vkDestroyShaderModule(device, module, nullptr);
    }

    void buildDescriptorSets() {
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4};
        poolSizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * (1 + HISTORY_SIZE)};

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 4;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &compDSPool));

        std::array<VkDescriptorSetLayout, 4> layouts{};
        layouts.fill(compDSLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = compDSPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();

        std::array<VkDescriptorSet, 4> raw{};
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, raw.data()));
        blurDS[0] = raw[0];
        blurDS[1] = raw[1];
        blendDS[0] = raw[2];
        blendDS[1] = raw[3];

        writeBlurDS(blurDS[0], workImg[0].view, workImg[1].view);
        writeBlurDS(blurDS[1], workImg[1].view, workImg[0].view);
        writeBlendDS(blendDS[0], workImg[0].view);
        writeBlendDS(blendDS[1], workImg[1].view);
    }

    void writeBlurDS(VkDescriptorSet descriptorSet, VkImageView destView, VkImageView srcView) {
        VkDescriptorImageInfo destInfo{VK_NULL_HANDLE, destView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo srcInfo{computeSampler, srcView, VK_IMAGE_LAYOUT_GENERAL};
        std::vector<VkDescriptorImageInfo> historyInfos(
            HISTORY_SIZE,
            VkDescriptorImageInfo{computeSampler, srcView, VK_IMAGE_LAYOUT_GENERAL});

        std::array<VkWriteDescriptorSet, 3> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1,
                     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &destInfo, nullptr, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &srcInfo, nullptr, nullptr};
        writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 2, 0, HISTORY_SIZE,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, historyInfos.data(), nullptr, nullptr};
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    [[nodiscard]] std::vector<char> readDisplayShader(const std::string &name) const {
        const std::array<std::string, 3> candidates = {
            assetRoot + "/data/" + name,
            assetRoot + "/" + name,
            std::string("data/") + name,
        };

        for (const std::string &path : candidates) {
            std::ifstream file(path, std::ios::binary);
            if (file.is_open()) {
                std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                if (!bytes.empty()) {
                    return bytes;
                }
            }
        }

        throw mxvk::Exception("Cannot open compute display shader: " + name);
    }

    [[nodiscard]] VkShaderModule createShaderModule(const std::vector<char> &bytes) const {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = bytes.size();
        info.pCode = reinterpret_cast<const uint32_t *>(bytes.data());

        VkShaderModule module = VK_NULL_HANDLE;
        VK_CHECK_RESULT(vkCreateShaderModule(device, &info, nullptr, &module));
        return module;
    }

    void createDisplayBuffers() {
        if (displayVertexBuffer != VK_NULL_HANDLE && displayIndexBuffer != VK_NULL_HANDLE) {
            return;
        }

        const std::array<float, 16> vertices = {
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            1.0F,
            0.0F,
            1.0F,
            0.0F,
            1.0F,
            1.0F,
            1.0F,
            1.0F,
            0.0F,
            1.0F,
            0.0F,
            1.0F,
        };
        const std::array<uint16_t, 6> indices = {0, 1, 2, 0, 2, 3};

        createBuffer(sizeof(float) * vertices.size(),
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     displayVertexBuffer,
                     displayVertexMemory);
        createBuffer(sizeof(uint16_t) * indices.size(),
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     displayIndexBuffer,
                     displayIndexMemory);

        void *mapped = nullptr;
        VK_CHECK_RESULT(vkMapMemory(device, displayVertexMemory, 0, sizeof(float) * vertices.size(), 0, &mapped));
        std::memcpy(mapped, vertices.data(), sizeof(float) * vertices.size());
        vkUnmapMemory(device, displayVertexMemory);

        VK_CHECK_RESULT(vkMapMemory(device, displayIndexMemory, 0, sizeof(uint16_t) * indices.size(), 0, &mapped));
        std::memcpy(mapped, indices.data(), sizeof(uint16_t) * indices.size());
        vkUnmapMemory(device, displayIndexMemory);
    }

    void createDisplayDescriptorSet() {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &displayDSLayout));

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &displayDSPool));

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = displayDSPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &displayDSLayout;
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &displayDS));

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = computeSampler;
        imageInfo.imageView = outImg.view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = displayDS;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    void rebuildDisplayPipeline() {
        if (displayPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, displayPipeline, nullptr);
            displayPipeline = VK_NULL_HANDLE;
        }
        if (displayPipeLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, displayPipeLayout, nullptr);
            displayPipeLayout = VK_NULL_HANDLE;
        }
        if (displayDSLayout == VK_NULL_HANDLE || swapchain_format == VK_FORMAT_UNDEFINED) {
            return;
        }

        const VkShaderModule vertModule = createShaderModule(readDisplayShader("sprite.vert.spv"));
        const VkShaderModule fragModule = createShaderModule(readDisplayShader("sprite.frag.spv"));

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;
        fragStage.pName = "main";

        const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(float) * 4;
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> attrs{};
        attrs[0].binding = 0;
        attrs[0].location = 0;
        attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[0].offset = 0;
        attrs[1].binding = 0;
        attrs[1].location = 1;
        attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[1].offset = sizeof(float) * 2;

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
        vertexInput.pVertexAttributeDescriptions = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        const std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicInfo{};
        dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicInfo.pDynamicStates = dynamicStates.data();

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0F;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachment;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.size = sizeof(float) * 12;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &displayDSLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &displayPipeLayout));

        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &swapchain_format;
        if (depth_format != VK_FORMAT_UNDEFINED) {
            renderingInfo.depthAttachmentFormat = depth_format;
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicInfo;
        pipelineInfo.layout = displayPipeLayout;
        pipelineInfo.renderPass = VK_NULL_HANDLE;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &displayPipeline));

        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
    }

    void createDisplayResources() {
        createDisplayBuffers();
        createDisplayDescriptorSet();
        rebuildDisplayPipeline();
        std::cout << "compute_shader: display path active: Vulkan compute output image -> fullscreen sampled draw (no readback, no sprite upload)\n";
    }

    void writeBlendDS(VkDescriptorSet descriptorSet, VkImageView srcView) {
        VkDescriptorImageInfo destInfo{VK_NULL_HANDLE, outImg.view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo srcInfo{computeSampler, srcView, VK_IMAGE_LAYOUT_GENERAL};

        std::vector<VkDescriptorImageInfo> historyInfos(HISTORY_SIZE);
        for (int index = 0; index < HISTORY_SIZE; ++index) {
            historyInfos[index] = {computeSampler, histImg[index].view, VK_IMAGE_LAYOUT_GENERAL};
        }

        std::array<VkWriteDescriptorSet, 3> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1,
                     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &destInfo, nullptr, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &srcInfo, nullptr, nullptr};
        writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 2, 0, HISTORY_SIZE,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, historyInfos.data(), nullptr, nullptr};
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void uploadToImage(const void *data, int srcPitch, ComputeImage &img) {
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(texWidth) * texHeight * 4;
        const int tightPitch = texWidth * 4;
        if (data == nullptr || srcPitch < tightPitch) {
            return;
        }

        void *mapped = nullptr;
        VK_CHECK_RESULT(vkMapMemory(device, stagingMem, 0, bytes, 0, &mapped));
        if (srcPitch == tightPitch) {
            std::memcpy(mapped, data, static_cast<size_t>(bytes));
        } else {
            const auto *src = static_cast<const uint8_t *>(data);
            auto *dst = static_cast<uint8_t *>(mapped);
            for (int row = 0; row < texHeight; ++row) {
                std::memcpy(dst + static_cast<size_t>(row) * tightPitch,
                            src + static_cast<size_t>(row) * srcPitch,
                            static_cast<size_t>(tightPitch));
            }
        }
        vkUnmapMemory(device, stagingMem);

        const VkCommandBuffer cmd = beginSingleTimeCommands();

        transitionImageLayout(
            cmd,
            img.image,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);

        VkBufferImageCopy2 region{};
        region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1};

        VkCopyBufferToImageInfo2 copyInfo{};
        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
        copyInfo.srcBuffer = stagingBuf;
        copyInfo.dstImage = img.image;
        copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;
        vkCmdCopyBufferToImage2(cmd, &copyInfo);

        transitionImageLayout(
            cmd,
            img.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

        endSingleTimeCommands(cmd);
    }

#ifdef MXVK_CUDA
    bool uploadGpuToImage(const cv::cuda::GpuMat &rgba, cv::cuda::Stream &stream, ComputeImage &img) {
        if (rgba.empty() || rgba.type() != CV_8UC4 || rgba.cols != texWidth || rgba.rows != texHeight) {
            return false;
        }
        if (!ensureCudaInterop(img)) {
            return false;
        }

        cudaStream_t cudaStream = mxvk::cuda_stream_handle(stream);
        if (!img.cudaUploadLogged) {
            std::cout << "compute_shader: CUDA interop upload: copying "
                      << rgba.cols << "x" << rgba.rows
                      << " RGBA GpuMat to Vulkan compute storage image via cudaArray (source pitch="
                      << static_cast<unsigned long long>(rgba.step)
                      << " bytes, copy row bytes="
                      << static_cast<unsigned long long>(static_cast<size_t>(rgba.cols) * 4U)
                      << ")\n";
            img.cudaUploadLogged = true;
        }

        cudaError_t cudaResult = cudaMemcpy2DToArrayAsync(
            img.cudaArray, 0, 0, rgba.ptr(), rgba.step,
            static_cast<size_t>(rgba.cols) * 4U, static_cast<size_t>(rgba.rows),
            cudaMemcpyDeviceToDevice, cudaStream);
        if (cudaResult != cudaSuccess) {
            std::cout << "compute_shader: CUDA interop compute input copy failed: "
                      << cudaGetErrorString(cudaResult) << "\n";
            return false;
        }

        cudaResult = cudaStreamSynchronize(cudaStream);
        if (cudaResult != cudaSuccess) {
            std::cout << "compute_shader: CUDA interop compute input sync failed: "
                      << cudaGetErrorString(cudaResult) << "\n";
            return false;
        }

        const VkCommandBuffer cmd = beginSingleTimeCommands();
        transitionImageLayout(
            cmd,
            img.image,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        endSingleTimeCommands(cmd);

        if (!img.cudaBarrierLogged) {
            std::cout << "compute_shader: CUDA interop sync: CUDA stream synchronized; Vulkan records GENERAL -> GENERAL memory barrier before compute sampling\n";
            img.cudaBarrierLogged = true;
        }
        return true;
    }
#endif

    void dispatchOne(VkCommandBuffer cmd, VkDescriptorSet descriptorSet, int mode) {
        ComputePC pc{};
        pc.mode = (!spvFiles.empty() && spvFiles[currentSpvIndex] == MODE_SHADER_NAME) ? shaderMode : mode;
        pc.historyCount = historyCount;
        pc.historyIdx = currentHistIdx;
        pc.square_size = currentSquare;
        pc.history_dir = currentDir;
        pc.alpha = alpha;
        pc.do_invert = 0;
        pc.do_swap = 0;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compPipeline);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            compPipeLayout,
            0,
            1,
            &descriptorSet,
            0,
            nullptr);
        vkCmdPushConstants(cmd, compPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupX = (static_cast<uint32_t>(texWidth) + 15) / 16;
        const uint32_t groupY = (static_cast<uint32_t>(texHeight) + 15) / 16;
        vkCmdDispatch(cmd, groupX, groupY, 1);
    }

    void renderComputeOutput(VkCommandBuffer cmd) {
        if (displayPipeline == VK_NULL_HANDLE || displayPipeLayout == VK_NULL_HANDLE ||
            displayDS == VK_NULL_HANDLE || displayVertexBuffer == VK_NULL_HANDLE ||
            displayIndexBuffer == VK_NULL_HANDLE) {
            return;
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, displayPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, displayPipeLayout,
                                0, 1, &displayDS, 0, nullptr);

        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &displayVertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, displayIndexBuffer, 0, VK_INDEX_TYPE_UINT16);

        struct DisplayPC {
            float screenWidth;
            float screenHeight;
            float spritePosX;
            float spritePosY;
            float spriteSizeW;
            float spriteSizeH;
            float effectsOn;
            float padding2;
            float params[4];
        } pc{
            static_cast<float>(swapchain_extent.width),
            static_cast<float>(swapchain_extent.height),
            0.0F,
            0.0F,
            static_cast<float>(swapchain_extent.width),
            static_cast<float>(swapchain_extent.height),
            0.0F,
            0.0F,
            {0.0F, 0.0F, 0.0F, 0.0F},
        };

        vkCmdPushConstants(cmd, displayPipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(DisplayPC), &pc);
        vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
    }

    void computeBarrier(VkCommandBuffer cmd, VkImage img) const {
        transitionImageLayout(
            cmd,
            img,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    }

    void runComputeFrame() {
        const VkCommandBuffer cmd = beginSingleTimeCommands();
        int srcIdx = 0;
        int dstIdx = 1;
        const bool isModeShader = !spvFiles.empty() && spvFiles[currentSpvIndex] == MODE_SHADER_NAME;

        if (isModeShader) {
            std::array<VkImageMemoryBarrier2, 2> barriers{};
            barriers[0] = makeImageBarrier(
                workImg[srcIdx].image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);

            barriers[1] = makeImageBarrier(
                histImg[historyIndex].image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT);

            VkDependencyInfo dependencyInfo{};
            dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
            dependencyInfo.pImageMemoryBarriers = barriers.data();
            vkCmdPipelineBarrier2(cmd, &dependencyInfo);

            VkImageCopy2 copy{};
            copy.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
            copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.extent = {static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1};

            VkCopyImageInfo2 copyInfo{};
            copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
            copyInfo.srcImage = workImg[srcIdx].image;
            copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            copyInfo.dstImage = histImg[historyIndex].image;
            copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            copyInfo.regionCount = 1;
            copyInfo.pRegions = &copy;
            vkCmdCopyImage2(cmd, &copyInfo);

            barriers[0] = makeImageBarrier(
                workImg[srcIdx].image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

            barriers[1] = makeImageBarrier(
                histImg[historyIndex].image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT);

            dependencyInfo = {};
            dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
            dependencyInfo.pImageMemoryBarriers = barriers.data();
            vkCmdPipelineBarrier2(cmd, &dependencyInfo);

            if (historyCount < HISTORY_SIZE) {
                ++historyCount;
            }
            historyIndex = (historyIndex + 1) % HISTORY_SIZE;
            dispatchOne(cmd, blendDS[srcIdx], shaderMode);
        } else {
            const int passes = 3 + (std::rand() % 7);
            for (int pass = 0; pass < passes; ++pass) {
                dispatchOne(cmd, blurDS[dstIdx], 0);
                computeBarrier(cmd, workImg[dstIdx].image);
                std::swap(srcIdx, dstIdx);
            }

            std::array<VkImageMemoryBarrier2, 2> barriers{};
            barriers[0] = makeImageBarrier(
                workImg[srcIdx].image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);

            barriers[1] = makeImageBarrier(
                histImg[historyIndex].image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT);

            VkDependencyInfo dependencyInfo{};
            dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
            dependencyInfo.pImageMemoryBarriers = barriers.data();
            vkCmdPipelineBarrier2(cmd, &dependencyInfo);

            VkImageCopy2 copy{};
            copy.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
            copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.extent = {static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1};

            VkCopyImageInfo2 copyInfo{};
            copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
            copyInfo.srcImage = workImg[srcIdx].image;
            copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            copyInfo.dstImage = histImg[historyIndex].image;
            copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            copyInfo.regionCount = 1;
            copyInfo.pRegions = &copy;
            vkCmdCopyImage2(cmd, &copyInfo);

            barriers[0] = makeImageBarrier(
                workImg[srcIdx].image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

            barriers[1] = makeImageBarrier(
                histImg[historyIndex].image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT);

            dependencyInfo = {};
            dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
            dependencyInfo.pImageMemoryBarriers = barriers.data();
            vkCmdPipelineBarrier2(cmd, &dependencyInfo);

            if (historyCount < HISTORY_SIZE) {
                ++historyCount;
            }
            historyIndex = (historyIndex + 1) % HISTORY_SIZE;

            const bool isMetalMedian =
                !spvFiles.empty() && spvFiles[currentSpvIndex].find("metalmedianblend") != std::string::npos;
            dispatchOne(cmd, blendDS[srcIdx], isMetalMedian ? 2 : 1);
        }

        transitionImageLayout(
            cmd,
            outImg.image,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        endSingleTimeCommands(cmd);
    }

    void tickAnimState() {
        if (currentDir == 1) {
            if (++currentHistIdx >= HISTORY_SIZE - 1) {
                currentHistIdx = HISTORY_SIZE - 1;
                currentDir = -1;
            }
        } else if (--currentHistIdx <= 0) {
            currentHistIdx = 0;
            currentDir = 1;
        }

        if (squareDir == 1) {
            currentSquare += 2;
            if (currentSquare >= 64) {
                currentSquare = 64;
                squareDir = 0;
            }
        } else {
            currentSquare -= 2;
            if (currentSquare <= 2) {
                currentSquare = 2;
                squareDir = 1;
            }
        }

        static int alphaDir = 1;
        if (alphaDir == 1) {
            alpha += 0.005F;
            if (alpha >= (255.0F / 32.0F)) {
                alpha = 255.0F / 32.0F;
                alphaDir = -1;
            }
        } else {
            alpha -= 0.005F;
            if (alpha <= 1.0F) {
                alpha = 1.0F;
                alphaDir = 1;
            }
        }
    }

    static VkImageMemoryBarrier2 makeImageBarrier(VkImage img,
                                                  VkImageLayout oldLayout,
                                                  VkImageLayout newLayout,
                                                  VkPipelineStageFlags2 srcStage,
                                                  VkAccessFlags2 srcAccess,
                                                  VkPipelineStageFlags2 dstStage,
                                                  VkAccessFlags2 dstAccess) {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = srcStage;
        barrier.srcAccessMask = srcAccess;
        barrier.dstStageMask = dstStage;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = img;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        return barrier;
    }

    static void transitionImageLayout(VkCommandBuffer cmd,
                                      VkImage img,
                                      VkImageLayout oldLayout,
                                      VkImageLayout newLayout,
                                      VkPipelineStageFlags2 srcStage,
                                      VkAccessFlags2 srcAccess,
                                      VkPipelineStageFlags2 dstStage,
                                      VkAccessFlags2 dstAccess) {
        const VkImageMemoryBarrier2 barrier = makeImageBarrier(
            img,
            oldLayout,
            newLayout,
            srcStage,
            srcAccess,
            dstStage,
            dstAccess);

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    }

    void destroyDisplayResources() {
        if (displayPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, displayPipeline, nullptr);
            displayPipeline = VK_NULL_HANDLE;
        }
        if (displayPipeLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, displayPipeLayout, nullptr);
            displayPipeLayout = VK_NULL_HANDLE;
        }
        if (displayDSPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, displayDSPool, nullptr);
            displayDSPool = VK_NULL_HANDLE;
            displayDS = VK_NULL_HANDLE;
        }
        if (displayDSLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, displayDSLayout, nullptr);
            displayDSLayout = VK_NULL_HANDLE;
        }
        if (displayVertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, displayVertexBuffer, nullptr);
            displayVertexBuffer = VK_NULL_HANDLE;
        }
        if (displayVertexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, displayVertexMemory, nullptr);
            displayVertexMemory = VK_NULL_HANDLE;
        }
        if (displayIndexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, displayIndexBuffer, nullptr);
            displayIndexBuffer = VK_NULL_HANDLE;
        }
        if (displayIndexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, displayIndexMemory, nullptr);
            displayIndexMemory = VK_NULL_HANDLE;
        }
    }

    void destroyComputeResources() {
        if (device == VK_NULL_HANDLE) {
            return;
        }

        vkDeviceWaitIdle(device);
        destroyDisplayResources();

        auto destroyImage = [&](ComputeImage &img) {
#ifdef MXVK_CUDA
            destroyCudaInterop(img);
#endif
            if (img.view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, img.view, nullptr);
            }
            if (img.image != VK_NULL_HANDLE) {
                vkDestroyImage(device, img.image, nullptr);
            }
            if (img.memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, img.memory, nullptr);
            }
            img = {};
        };

        destroyImage(workImg[0]);
        destroyImage(workImg[1]);
        for (ComputeImage &img : histImg) {
            destroyImage(img);
        }
        destroyImage(outImg);

        if (computeSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, computeSampler, nullptr);
            computeSampler = VK_NULL_HANDLE;
        }
        if (compDSPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, compDSPool, nullptr);
            compDSPool = VK_NULL_HANDLE;
        }
        if (compPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, compPipeline, nullptr);
            compPipeline = VK_NULL_HANDLE;
        }
        if (compPipeLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, compPipeLayout, nullptr);
            compPipeLayout = VK_NULL_HANDLE;
        }
        if (compDSLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, compDSLayout, nullptr);
            compDSLayout = VK_NULL_HANDLE;
        }
        if (stagingBuf != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, stagingBuf, nullptr);
            stagingBuf = VK_NULL_HANDLE;
        }
        if (stagingMem != VK_NULL_HANDLE) {
            vkFreeMemory(device, stagingMem, nullptr);
            stagingMem = VK_NULL_HANDLE;
        }
        if (readbackBuf != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, readbackBuf, nullptr);
            readbackBuf = VK_NULL_HANDLE;
        }
        if (readbackMem != VK_NULL_HANDLE) {
            vkFreeMemory(device, readbackMem, nullptr);
            readbackMem = VK_NULL_HANDLE;
        }
    }
};

int main(int argc, char **argv) {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    try {
        Arguments args = proc_args(argc, argv);
        if (args.path == ".") {
            args.path = compute_shader_ASSET_DIR;
        }

        ComputeWindow window(args);
        window.loop();
    } catch (const mxvk::Exception &e) {
        std::cerr << "mxvk: Exception: " << e.text() << "\n";
        return EXIT_FAILURE;
    } catch (const ArgException<std::string> &e) {
        std::cerr << "Argument Exception: " << e.text() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
