/**
 * @file mxvk_opencv_compat.hpp
 * @brief Small compatibility wrappers around OpenCV CUDA APIs.
 *
 * Keeping these helpers in one place limits the blast radius when OpenCV
 * changes CUDA stream or HostMem APIs in future releases.
 */
#pragma once

#ifdef MXVK_CUDA
#include <cuda_runtime_api.h>
#include <opencv2/core/cuda.hpp>
#include <opencv2/core/cuda_stream_accessor.hpp>

namespace mxvk {

    [[nodiscard]] inline cudaStream_t cuda_stream_handle(cv::cuda::Stream &stream) {
        return cv::cuda::StreamAccessor::getStream(stream);
    }

    [[nodiscard]] inline cv::Mat host_mem_mat_header(cv::cuda::HostMem &hostMem) {
        return hostMem.createMatHeader();
    }

    [[nodiscard]] inline cv::cuda::GpuMat host_mem_gpu_header(cv::cuda::HostMem &hostMem) {
        return hostMem.createGpuMatHeader();
    }

} // namespace mxvk
#endif
