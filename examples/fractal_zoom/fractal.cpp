#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#if defined(MXWRITE_ENABLED)
#include "mxwrite.hpp"
#endif

#include <SDL3/SDL.h>

#include <boost/multiprecision/cpp_dec_float.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifndef fractal_zoom_SHADER_DIR
#define fractal_zoom_SHADER_DIR "."
#endif

namespace example {

    class FractalWindow : public mxvk::VK_Window {
        using ReferenceScalar = boost::multiprecision::cpp_dec_float_100;

        struct OrbitSample {
            float x;
            float y;
            float z;
            float w;
        };

        struct ReferenceTile {
            ReferenceScalar min_uv_x;
            ReferenceScalar min_uv_y;
            ReferenceScalar max_uv_x;
            ReferenceScalar max_uv_y;
            int depth;
        };

      public:
        FractalWindow([[maybe_unused]] const std::string &path, int width, int height, bool fullscreen, bool enable_vsync)
            : mxvk::VK_Window("-[ Fractal Zoom - MXVK ]-", width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              reference_orbit_samples(static_cast<size_t>(reference_orbit_capacity)) {
        }

        ~FractalWindow() override {
#if defined(MXWRITE_ENABLED)
            closeVideoWriter();
#endif
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            destroyFractalResources();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if ((e.key.key == SDLK_F10 || e.key.scancode == SDL_SCANCODE_F10) && !e.key.repeat) {
                    saveFractalSnapshot();
                    return;
                }
                if ((e.key.key == SDLK_P || e.key.scancode == SDL_SCANCODE_P) && !e.key.repeat) {
#if defined(MXWRITE_ENABLED)
                    toggleVideoRecording();
#else
                    std::cout << "fractal_zoom: MXWrite is unavailable; video recording disabled\n";
#endif
                    return;
                }
                handleKey(e.key.key);
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                dragging = true;
                drag_start_mouse_x = e.button.x;
                drag_start_mouse_y = e.button.y;
                drag_start_center_x = center_x;
                drag_start_center_y = center_y;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                dragging = false;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_MOTION && dragging) {
                const VkExtent2D extent = getSwapchainExtent();
                if (extent.width == 0U || extent.height == 0U) {
                    return;
                }
                const int delta_x = e.motion.x - drag_start_mouse_x;
                const int delta_y = e.motion.y - drag_start_mouse_y;
                const ReferenceScalar scale = ReferenceScalar(2) / (zoom * ReferenceScalar(std::min(extent.width, extent.height)));
                center_x = drag_start_center_x - static_cast<ReferenceScalar>(delta_x) * scale;
                center_y = drag_start_center_y + static_cast<ReferenceScalar>(delta_y) * scale;
                reference_orbit_dirty = true;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                applyWheelZoom(e.wheel.y);
            }
        }

        void proc() override {
            updateKeyboardNavigation();
        }

        void render() override {
#if defined(MXWRITE_ENABLED)
            serviceRecordingReadbacks();
#endif
            mxvk::VK_Window::render();
#if defined(MXWRITE_ENABLED)
            recordPresentedFrame();
#endif
        }

        void onSwapchainAboutToRecreate() override {
#if defined(MXWRITE_ENABLED)
            if (video_writer.is_open()) {
                std::cerr << "fractal_zoom: swapchain is changing; closing current video recording\n";
                closeVideoWriter();
            }
#endif
            destroyFractalResources();
        }

        void onSwapchainRecreated() override {
            createFractalPipeline();
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, [[maybe_unused]] uint32_t image_index) override {
            if (fractal_pipeline == VK_NULL_HANDLE || fractal_pipeline_layout == VK_NULL_HANDLE) {
                createFractalPipeline();
            }

            if (image_index >= fractal_descriptor_sets.size()) {
                return;
            }

            if (fractal_pipeline == VK_NULL_HANDLE || fractal_pipeline_layout == VK_NULL_HANDLE || fractal_descriptor_sets[image_index] == VK_NULL_HANDLE) {
                return;
            }

            const VkExtent2D extent = getSwapchainExtent();
            if (extent.width == 0U || extent.height == 0U) {
                return;
            }

            updateReferenceOrbit(image_index, extent);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fractal_pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fractal_pipeline_layout, 0, 1, &fractal_descriptor_sets[image_index], 0, nullptr);

            const FractalPushConstants push_constants{
                center_x.convert_to<PushScalar>(),
                center_y.convert_to<PushScalar>(),
                (ReferenceScalar(1) / zoom).convert_to<PushScalar>(),
                static_cast<PushScalar>(std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count()),
                static_cast<PushScalar>(extent.width),
                static_cast<PushScalar>(extent.height),
                max_iterations,
                palette_index,
                orbit_length,
                0};

            vkCmdPushConstants(
                cmd,
                fractal_pipeline_layout,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(push_constants),
                &push_constants);

            vkCmdDraw(cmd, 3, 1, 0, 0);
        }

      private:
        static ReferenceScalar minReferenceScalar(const ReferenceScalar &a, const ReferenceScalar &b) {
            return (a < b) ? a : b;
        }

        static ReferenceScalar maxReferenceScalar(const ReferenceScalar &a, const ReferenceScalar &b) {
            return (a > b) ? a : b;
        }

        static ReferenceScalar clampReferenceScalar(const ReferenceScalar &value, const ReferenceScalar &minimum, const ReferenceScalar &maximum) {
            return maxReferenceScalar(minReferenceScalar(value, maximum), minimum);
        }

        static float toOrbitSampleScalar(const ReferenceScalar &value) {
            return value.convert_to<float>();
        }

        void handleKey(SDL_Keycode key) {
            switch (key) {
            case SDLK_ESCAPE:
                exit();
                break;
            case SDLK_R:
                resetView();
                break;
            case SDLK_1:
                center_x = ReferenceScalar("-0.5");
                center_y = ReferenceScalar(0);
                zoom = ReferenceScalar(1);
                max_iterations = 256;
                reference_orbit_dirty = true;
                break;
            case SDLK_2:
                center_x = ReferenceScalar("-0.745");
                center_y = ReferenceScalar("0.113");
                zoom = ReferenceScalar(50);
                max_iterations = 512;
                reference_orbit_dirty = true;
                break;
            case SDLK_3:
                center_x = ReferenceScalar("-0.761574");
                center_y = ReferenceScalar("-0.0847596");
                zoom = ReferenceScalar(220);
                max_iterations = 900;
                reference_orbit_dirty = true;
                break;
            case SDLK_EQUALS:
            case SDLK_PLUS:
                max_iterations = std::min(max_iterations + 64, max_reference_iterations);
                reference_orbit_dirty = true;
                break;
            case SDLK_MINUS:
                max_iterations = std::max(max_iterations - 64, 64);
                reference_orbit_dirty = true;
                break;
            case SDLK_LEFTBRACKET:
                palette_index = (palette_index + 2) % 3;
                break;
            case SDLK_RIGHTBRACKET:
                palette_index = (palette_index + 1) % 3;
                break;
            default:
                break;
            }
        }

#if defined(MXWRITE_ENABLED)
        void toggleVideoRecording() {
            if (video_writer.is_open()) {
                closeVideoWriter();
                return;
            }

            const VkExtent2D extent = getSwapchainExtent();
            if (extent.width == 0U || extent.height == 0U) {
                std::cerr << "fractal_zoom: cannot start video recording before the swapchain is ready\n";
                return;
            }

            constexpr float video_fps = 60.0F;
            video_writer.set_block_when_full(false);
            if (!video_writer.open(video_output_path,
                                   static_cast<int>(extent.width),
                                   static_cast<int>(extent.height),
                                   video_fps,
                                   "18")) {
                std::cerr << "fractal_zoom: failed to open MXWrite output file: " << video_output_path << "\n";
                return;
            }

            video_record_width = extent.width;
            video_record_height = extent.height;
            try {
                createRecordingReadbacks(extent);
            } catch (const std::exception &ex) {
                std::cerr << "fractal_zoom: failed to create async recording readback resources: " << ex.what() << "\n";
                video_writer.close();
                video_record_width = 0;
                video_record_height = 0;
                return;
            }
            std::cout << std::format("fractal_zoom: recording video to {} at {}x{} 60 FPS\n",
                                     video_output_path,
                                     video_record_width,
                                     video_record_height);
        }

        void closeVideoWriter() {
            if (!video_writer.is_open()) {
                return;
            }

            destroyRecordingReadbacks(true);
            video_writer.close();
            std::cout << "fractal_zoom: saved video: " << video_output_path << "\n";
            video_record_width = 0;
            video_record_height = 0;
        }

        void serviceRecordingReadbacks() {
            if (!video_writer.is_open()) {
                return;
            }

            try {
                pumpCompletedRecordingReadbacks(false);
                submitPendingRecordingReadbacks();
            } catch (const std::exception &ex) {
                std::cerr << "fractal_zoom: failed to service recording readback: " << ex.what() << "\n";
                closeVideoWriter();
            }
        }

        void recordPresentedFrame() {
            if (!video_writer.is_open()) {
                return;
            }

            try {
                const VkExtent2D extent = getSwapchainExtent();
                if (extent.width != video_record_width || extent.height != video_record_height) {
                    std::cerr << "fractal_zoom: swapchain size changed; closing current video recording\n";
                    closeVideoWriter();
                    return;
                }
                const auto now = std::chrono::steady_clock::now();
                if (now < next_recording_frame_time) {
                    return;
                }
                next_recording_frame_time += recording_frame_interval;
                if (next_recording_frame_time <= now) {
                    next_recording_frame_time = now + recording_frame_interval;
                }
                if (last_presented_image_index < recording_pending_images.size()) {
                    recording_pending_images[last_presented_image_index] = true;
                }
                pumpCompletedRecordingReadbacks(false);
                submitPendingRecordingReadbacks();
            } catch (const std::exception &ex) {
                std::cerr << "fractal_zoom: failed to record video frame: " << ex.what() << "\n";
                closeVideoWriter();
            }
        }

        struct RecordingReadbackSlot {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkCommandBuffer command_buffer = VK_NULL_HANDLE;
            VkFence fence = VK_NULL_HANDLE;
            std::vector<std::uint8_t> pixels{};
            uint32_t image_index = std::numeric_limits<uint32_t>::max();
            bool in_flight = false;
            bool queued = false;
        };

        void createRecordingReadbacks(VkExtent2D extent) {
            destroyRecordingReadbacks(false);
            if (device == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE || graphics_queue == VK_NULL_HANDLE) {
                throw mxvk::Exception("recording requires initialized Vulkan render resources");
            }
            if (!swapchain_supports_transfer_src) {
                throw mxvk::Exception("recording requires swapchain transfer-source support");
            }

            recording_format_is_bgra =
                swapchain_format == VK_FORMAT_B8G8R8A8_UNORM ||
                swapchain_format == VK_FORMAT_B8G8R8A8_SRGB;
            const bool format_is_rgba =
                swapchain_format == VK_FORMAT_R8G8B8A8_UNORM ||
                swapchain_format == VK_FORMAT_R8G8B8A8_SRGB;
            if (!recording_format_is_bgra && !format_is_rgba) {
                throw mxvk::Exception(std::format("unsupported recording swapchain format: {}", static_cast<int>(swapchain_format)));
            }

            recording_row_bytes = static_cast<VkDeviceSize>(extent.width) * 4U;
            recording_image_bytes = recording_row_bytes * static_cast<VkDeviceSize>(extent.height);
            recording_readbacks.resize(recording_readback_slot_count);
            recording_pending_images.assign(swapchain_images.size(), false);
            next_recording_frame_time = std::chrono::steady_clock::now();

            VkCommandBufferAllocateInfo command_info{};
            command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            command_info.commandPool = command_pool;
            command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            command_info.commandBufferCount = recording_readback_slot_count;

            std::array<VkCommandBuffer, recording_readback_slot_count> command_buffers{};
            if (vkAllocateCommandBuffers(device, &command_info, command_buffers.data()) != VK_SUCCESS) {
                destroyRecordingReadbacks(false);
                throw mxvk::Exception("failed to allocate recording readback command buffers");
            }

            VkFenceCreateInfo fence_info{};
            fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            for (size_t i = 0; i < recording_readbacks.size(); ++i) {
                RecordingReadbackSlot &slot = recording_readbacks[i];
                slot.command_buffer = command_buffers[i];
                createBuffer(
                    recording_image_bytes,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    slot.buffer,
                    slot.memory);
                if (vkCreateFence(device, &fence_info, nullptr, &slot.fence) != VK_SUCCESS) {
                    destroyRecordingReadbacks(false);
                    throw mxvk::Exception("failed to create recording readback fence");
                }
                slot.pixels.resize(static_cast<size_t>(recording_image_bytes));
            }

            recording_worker_stop = false;
            recording_worker = std::jthread([this](std::stop_token stop_token) {
                recordingWorkerLoop(stop_token);
            });
            recording_readbacks_ready = true;
        }

        void destroyRecordingReadbacks(bool drain) {
            if (!recording_readbacks_ready && recording_readbacks.empty()) {
                return;
            }

            if (drain) {
                drainRecordingReadbacks();
            }

            {
                std::lock_guard<std::mutex> lock(recording_mutex);
                recording_worker_stop = true;
            }
            recording_cv.notify_all();
            if (recording_worker.joinable()) {
                recording_worker.request_stop();
                recording_worker.join();
            }

            if (device != VK_NULL_HANDLE) {
                for (RecordingReadbackSlot &slot : recording_readbacks) {
                    if (slot.in_flight && slot.fence != VK_NULL_HANDLE) {
                        vkWaitForFences(device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
                    }
                    if (slot.image_index < image_fences.size() && image_fences[slot.image_index] == slot.fence) {
                        image_fences[slot.image_index] = VK_NULL_HANDLE;
                    }
                    if (slot.command_buffer != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE) {
                        vkFreeCommandBuffers(device, command_pool, 1, &slot.command_buffer);
                        slot.command_buffer = VK_NULL_HANDLE;
                    }
                    if (slot.fence != VK_NULL_HANDLE) {
                        vkDestroyFence(device, slot.fence, nullptr);
                        slot.fence = VK_NULL_HANDLE;
                    }
                    if (slot.buffer != VK_NULL_HANDLE) {
                        vkDestroyBuffer(device, slot.buffer, nullptr);
                        slot.buffer = VK_NULL_HANDLE;
                    }
                    if (slot.memory != VK_NULL_HANDLE) {
                        vkFreeMemory(device, slot.memory, nullptr);
                        slot.memory = VK_NULL_HANDLE;
                    }
                }
            }

            recording_readbacks.clear();
            recording_pending_images.clear();
            {
                std::lock_guard<std::mutex> lock(recording_mutex);
                std::queue<size_t> empty_queue;
                recording_ready_slots.swap(empty_queue);
                recording_worker_stop = false;
            }
            recording_readbacks_ready = false;
            recording_row_bytes = 0;
            recording_image_bytes = 0;
            recording_format_is_bgra = false;
        }

        void submitPendingRecordingReadbacks() {
            for (uint32_t image_index = 0; image_index < recording_pending_images.size(); ++image_index) {
                if (!recording_pending_images[image_index]) {
                    continue;
                }
                if (submitRecordingReadback(image_index)) {
                    recording_pending_images[image_index] = false;
                }
            }
        }

        void drainRecordingReadbacks() {
            while (true) {
                pumpCompletedRecordingReadbacks(true);
                std::unique_lock<std::mutex> lock(recording_mutex);
                const bool idle = std::ranges::all_of(recording_readbacks, [](const RecordingReadbackSlot &slot) {
                    return !slot.in_flight && !slot.queued;
                });
                if (idle) {
                    return;
                }
                recording_idle_cv.wait(lock);
            }
        }

        void pumpCompletedRecordingReadbacks(bool wait_for_copy) {
            for (size_t i = 0; i < recording_readbacks.size(); ++i) {
                RecordingReadbackSlot &slot = recording_readbacks[i];
                VkFence slot_fence = VK_NULL_HANDLE;
                {
                    std::lock_guard<std::mutex> lock(recording_mutex);
                    if (!slot.in_flight || slot.queued || slot.fence == VK_NULL_HANDLE) {
                        continue;
                    }
                    slot_fence = slot.fence;
                }

                VkResult fence_result = VK_SUCCESS;
                if (wait_for_copy) {
                    fence_result = vkWaitForFences(device, 1, &slot_fence, VK_TRUE, UINT64_MAX);
                } else {
                    fence_result = vkGetFenceStatus(device, slot_fence);
                }

                if (fence_result == VK_NOT_READY) {
                    continue;
                }
                if (fence_result != VK_SUCCESS) {
                    throw mxvk::Exception(std::format("recording readback fence failed: {}", static_cast<int>(fence_result)));
                }

                {
                    std::lock_guard<std::mutex> lock(recording_mutex);
                    if (!slot.in_flight || slot.queued || slot.fence != slot_fence) {
                        continue;
                    }
                    if (slot.image_index < image_fences.size() && image_fences[slot.image_index] == slot.fence) {
                        image_fences[slot.image_index] = VK_NULL_HANDLE;
                    }
                    slot.queued = true;
                    recording_ready_slots.push(i);
                }
                recording_cv.notify_one();
            }
        }

        bool submitRecordingReadback(uint32_t image_index) {
            if (!recording_readbacks_ready || image_index == std::numeric_limits<uint32_t>::max()) {
                return false;
            }
            if (image_index >= swapchain_images.size() || image_index >= image_fences.size()) {
                return false;
            }

            VkFence source_fence = image_fences[image_index];
            if (source_fence != VK_NULL_HANDLE && vkGetFenceStatus(device, source_fence) == VK_NOT_READY) {
                return false;
            }

            RecordingReadbackSlot *free_slot = nullptr;
            {
                std::lock_guard<std::mutex> lock(recording_mutex);
                for (size_t i = 0; i < recording_readbacks.size(); ++i) {
                    if (!recording_readbacks[i].in_flight && !recording_readbacks[i].queued) {
                        free_slot = &recording_readbacks[i];
                        break;
                    }
                }
            }
            if (free_slot == nullptr) {
                ++recording_dropped_frames;
                if (recording_dropped_frames % 60U == 0U) {
                    std::cerr << "fractal_zoom: dropped " << recording_dropped_frames << " recording frames (readback queue full)\n";
                }
                return true;
            }

            RecordingReadbackSlot &slot = *free_slot;
            VK_CHECK_RESULT(vkResetFences(device, 1, &slot.fence));
            VK_CHECK_RESULT(vkResetCommandBuffer(slot.command_buffer, 0));

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VK_CHECK_RESULT(vkBeginCommandBuffer(slot.command_buffer, &begin_info));

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
            to_transfer_barrier.image = swapchain_images[image_index];
            to_transfer_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            to_transfer_barrier.subresourceRange.baseMipLevel = 0;
            to_transfer_barrier.subresourceRange.levelCount = 1;
            to_transfer_barrier.subresourceRange.baseArrayLayer = 0;
            to_transfer_barrier.subresourceRange.layerCount = 1;

            VkDependencyInfo to_transfer_dependency{};
            to_transfer_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            to_transfer_dependency.imageMemoryBarrierCount = 1;
            to_transfer_dependency.pImageMemoryBarriers = &to_transfer_barrier;
            vkCmdPipelineBarrier2(slot.command_buffer, &to_transfer_dependency);

            VkBufferImageCopy copy_region{};
            copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy_region.imageSubresource.mipLevel = 0;
            copy_region.imageSubresource.baseArrayLayer = 0;
            copy_region.imageSubresource.layerCount = 1;
            copy_region.imageExtent = {video_record_width, video_record_height, 1};
            vkCmdCopyImageToBuffer(
                slot.command_buffer,
                swapchain_images[image_index],
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                slot.buffer,
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
            to_present_barrier.image = swapchain_images[image_index];
            to_present_barrier.subresourceRange = to_transfer_barrier.subresourceRange;

            VkDependencyInfo to_present_dependency{};
            to_present_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            to_present_dependency.imageMemoryBarrierCount = 1;
            to_present_dependency.pImageMemoryBarriers = &to_present_barrier;
            vkCmdPipelineBarrier2(slot.command_buffer, &to_present_dependency);

            VK_CHECK_RESULT(vkEndCommandBuffer(slot.command_buffer));

            VkCommandBufferSubmitInfo command_submit_info{};
            command_submit_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            command_submit_info.commandBuffer = slot.command_buffer;

            VkSubmitInfo2 submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit_info.commandBufferInfoCount = 1;
            submit_info.pCommandBufferInfos = &command_submit_info;

            slot.image_index = image_index;
            const VkResult submit_result = vkQueueSubmit2(graphics_queue, 1, &submit_info, slot.fence);
            if (submit_result != VK_SUCCESS) {
                throw mxvk::Exception(std::format("failed to submit recording readback: {}", static_cast<int>(submit_result)));
            }
            {
                std::lock_guard<std::mutex> lock(recording_mutex);
                slot.in_flight = true;
                slot.queued = false;
            }
            image_fences[slot.image_index] = slot.fence;
            return true;
        }

        void recordingWorkerLoop(std::stop_token stop_token) {
            std::vector<std::uint8_t> frame_pixels;
            while (true) {
                size_t slot_index = 0;
                {
                    std::unique_lock<std::mutex> lock(recording_mutex);
                    recording_cv.wait(lock, [this, &stop_token] {
                        return recording_worker_stop || stop_token.stop_requested() || !recording_ready_slots.empty();
                    });
                    if ((recording_worker_stop || stop_token.stop_requested()) && recording_ready_slots.empty()) {
                        break;
                    }
                    slot_index = recording_ready_slots.front();
                    recording_ready_slots.pop();
                }

                if (slot_index >= recording_readbacks.size()) {
                    continue;
                }
                RecordingReadbackSlot &slot = recording_readbacks[slot_index];
                void *mapped = nullptr;
                if (vkMapMemory(device, slot.memory, 0, recording_image_bytes, 0, &mapped) == VK_SUCCESS) {
                    const auto *src = static_cast<const std::uint8_t *>(mapped);
                    frame_pixels.resize(static_cast<size_t>(recording_image_bytes));
                    if (recording_format_is_bgra) {
                        for (size_t i = 0; i < frame_pixels.size(); i += 4U) {
                            frame_pixels[i + 0U] = src[i + 2U];
                            frame_pixels[i + 1U] = src[i + 1U];
                            frame_pixels[i + 2U] = src[i + 0U];
                            frame_pixels[i + 3U] = src[i + 3U];
                        }
                    } else {
                        std::memcpy(frame_pixels.data(), src, frame_pixels.size());
                    }
                    vkUnmapMemory(device, slot.memory);
                } else {
                    std::cerr << "fractal_zoom: failed to map recording readback memory\n";
                    frame_pixels.clear();
                }

                {
                    std::lock_guard<std::mutex> lock(recording_mutex);
                    slot.in_flight = false;
                    slot.queued = false;
                    slot.image_index = std::numeric_limits<uint32_t>::max();
                }
                recording_idle_cv.notify_all();

                if (!frame_pixels.empty()) {
                    video_writer.write(frame_pixels.data());
                }
            }
        }
#endif

        void saveFractalSnapshot() {
            const char *home = std::getenv("HOME");
            if (home == nullptr || std::strlen(home) == 0U) {
                std::cerr << "fractal_zoom: HOME is not set; cannot save snapshot\n";
                return;
            }

            std::filesystem::path snapshot_dir = std::filesystem::path(home) / "Pictures";
            std::error_code error;
            std::filesystem::create_directories(snapshot_dir, error);
            if (error) {
                std::cerr << std::format("fractal_zoom: failed to create snapshot directory '{}': {}\n",
                                         snapshot_dir.string(),
                                         error.message());
                return;
            }

            const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm local_time{};
#if defined(_WIN32)
            localtime_s(&local_time, &now);
#else
            localtime_r(&now, &local_time);
#endif

            const std::string filename = std::format(
                "fractal_zoom_snapshot.{:04d}-{:02d}-{:02d}.{:02d}-{:02d}-{:02d}-{:04d}.png",
                local_time.tm_year + 1900,
                local_time.tm_mon + 1,
                local_time.tm_mday,
                local_time.tm_hour,
                local_time.tm_min,
                local_time.tm_sec,
                snapshot_index++);

            const std::filesystem::path snapshot_path = snapshot_dir / filename;
            try {
                saveSnapshot(snapshot_path.string());
                std::cout << "fractal_zoom: saved snapshot: " << snapshot_path.string() << "\n";
            } catch (const std::exception &ex) {
                std::cerr << "fractal_zoom: failed to save snapshot: " << ex.what() << "\n";
            }
        }

        void applyWheelZoom(float wheel_y) {
            const VkExtent2D extent = getSwapchainExtent();
            if (extent.width == 0U || extent.height == 0U || window == nullptr) {
                return;
            }

            float mouse_x = 0.0f;
            float mouse_y = 0.0f;
            SDL_GetMouseState(&mouse_x, &mouse_y);

            const ReferenceScalar base_scale = ReferenceScalar(2) / (zoom * ReferenceScalar(std::min(extent.width, extent.height)));
            const ReferenceScalar before_x = (static_cast<ReferenceScalar>(mouse_x) - ReferenceScalar(extent.width) * ReferenceScalar("0.5")) * base_scale + center_x;
            const ReferenceScalar before_y = (ReferenceScalar(extent.height) * ReferenceScalar("0.5") - static_cast<ReferenceScalar>(mouse_y)) * base_scale + center_y;

            const ReferenceScalar zoom_factor = (wheel_y > 0.0f) ? ReferenceScalar("1.2") : (ReferenceScalar(1) / ReferenceScalar("1.2"));
            zoom = clampReferenceScalar(zoom * zoom_factor, ReferenceScalar("0.5"), MAX_ZOOM);

            const ReferenceScalar new_scale = ReferenceScalar(2) / (zoom * ReferenceScalar(std::min(extent.width, extent.height)));
            const ReferenceScalar after_x = (static_cast<ReferenceScalar>(mouse_x) - ReferenceScalar(extent.width) * ReferenceScalar("0.5")) * new_scale + center_x;
            const ReferenceScalar after_y = (ReferenceScalar(extent.height) * ReferenceScalar("0.5") - static_cast<ReferenceScalar>(mouse_y)) * new_scale + center_y;

            center_x += before_x - after_x;
            center_y += before_y - after_y;

            if (wheel_y > 0.0f && zoom > ReferenceScalar(10)) {
                max_iterations = std::min(max_iterations + 12, max_reference_iterations);
            }
            reference_orbit_dirty = true;
        }

        void updateKeyboardNavigation() {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys == nullptr) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const ReferenceScalar dt = ReferenceScalar(std::chrono::duration<double>(now - last_tick).count());
            last_tick = now;

            const ReferenceScalar move_speed = ReferenceScalar("0.85") * minReferenceScalar(dt, ReferenceScalar("0.1")) / zoom;
            bool moved = false;
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) {
                center_x -= move_speed;
                moved = true;
            }
            if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) {
                center_x += move_speed;
                moved = true;
            }
            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) {
                center_y += move_speed;
                moved = true;
            }
            if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) {
                center_y -= move_speed;
                moved = true;
            }
            if (keys[SDL_SCANCODE_Z]) {
                zoom = minReferenceScalar(zoom * (ReferenceScalar(1) + ReferenceScalar("1.9") * dt), MAX_ZOOM);
                moved = true;
            }
            if (keys[SDL_SCANCODE_X]) {
                zoom = maxReferenceScalar(zoom * (ReferenceScalar(1) - ReferenceScalar("1.9") * dt), ReferenceScalar("0.5"));
                moved = true;
            }
            if (moved) {
                reference_orbit_dirty = true;
            }
        }

        void resetView() {
            center_x = ReferenceScalar("-0.5");
            center_y = ReferenceScalar(0);
            zoom = ReferenceScalar(1);
            max_iterations = 256;
            reference_orbit_dirty = true;
        }

        void destroyFractalResources() {
            if (device == VK_NULL_HANDLE) {
                fractal_pipeline = VK_NULL_HANDLE;
                fractal_pipeline_layout = VK_NULL_HANDLE;
                fractal_descriptor_set_layout = VK_NULL_HANDLE;
                fractal_descriptor_pool = VK_NULL_HANDLE;
                fractal_descriptor_sets.clear();
                reference_orbit_buffers.clear();
                reference_orbit_memories.clear();
                reference_orbit_mapped.clear();
                reference_orbit_coherent.clear();
                reference_orbit_uploaded_generations.clear();
                return;
            }

            if (fractal_pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, fractal_pipeline, nullptr);
                fractal_pipeline = VK_NULL_HANDLE;
            }
            if (fractal_pipeline_layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, fractal_pipeline_layout, nullptr);
                fractal_pipeline_layout = VK_NULL_HANDLE;
            }
            if (fractal_descriptor_pool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, fractal_descriptor_pool, nullptr);
                fractal_descriptor_pool = VK_NULL_HANDLE;
                fractal_descriptor_sets.clear();
            }
            if (fractal_descriptor_set_layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, fractal_descriptor_set_layout, nullptr);
                fractal_descriptor_set_layout = VK_NULL_HANDLE;
            }
            for (size_t i = 0; i < reference_orbit_memories.size(); ++i) {
                if (reference_orbit_memories[i] != VK_NULL_HANDLE) {
                    if (i < reference_orbit_mapped.size() && reference_orbit_mapped[i] != nullptr) {
                        vkUnmapMemory(device, reference_orbit_memories[i]);
                    }
                    vkFreeMemory(device, reference_orbit_memories[i], nullptr);
                }
            }
            for (VkBuffer buffer : reference_orbit_buffers) {
                if (buffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, buffer, nullptr);
                }
            }
            reference_orbit_buffers.clear();
            reference_orbit_memories.clear();
            reference_orbit_mapped.clear();
            reference_orbit_coherent.clear();
            reference_orbit_uploaded_generations.clear();
        }

        void destroyFractalPipeline() {
            if (device == VK_NULL_HANDLE) {
                fractal_pipeline = VK_NULL_HANDLE;
                fractal_pipeline_layout = VK_NULL_HANDLE;
                return;
            }
            if (fractal_pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, fractal_pipeline, nullptr);
                fractal_pipeline = VK_NULL_HANDLE;
            }
            if (fractal_pipeline_layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, fractal_pipeline_layout, nullptr);
                fractal_pipeline_layout = VK_NULL_HANDLE;
            }
        }

        void createFractalPipeline() {
            if (device == VK_NULL_HANDLE) {
                return;
            }

            const VkFormat color_format = getSwapchainFormat();
            const VkFormat depth_attachment_format = getDepthFormat();
            if (color_format == VK_FORMAT_UNDEFINED || depth_attachment_format == VK_FORMAT_UNDEFINED) {
                return;
            }

            ensureFractalResources();
            if (fractal_descriptor_set_layout == VK_NULL_HANDLE || fractal_descriptor_sets.empty()) {
                return;
            }

            destroyFractalPipeline();

            const std::string vert_path = std::string(fractal_zoom_SHADER_DIR) + "/fractal.vert.spv";
            const std::string frag_path =
#if defined(MXVK_USE_MOLTENVK)
                std::string(fractal_zoom_SHADER_DIR) + "/fractal_float.frag.spv";
#else
                std::string(fractal_zoom_SHADER_DIR) + "/fractal.frag.spv";
#endif
            const std::vector<char> vert_bytes = loadSpv(vert_path);
            const std::vector<char> frag_bytes = loadSpv(frag_path);

            const VkShaderModule vert_module = createShaderModule(device, vert_bytes);
            VkShaderModule frag_module = VK_NULL_HANDLE;

            try {
                frag_module = createShaderModule(device, frag_bytes);

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

                VkPipelineVertexInputStateCreateInfo vertex_input{};
                vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

                VkPipelineInputAssemblyStateCreateInfo input_assembly{};
                input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                input_assembly.primitiveRestartEnable = VK_FALSE;

                VkPipelineViewportStateCreateInfo viewport_state{};
                viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewport_state.viewportCount = 1;
                viewport_state.scissorCount = 1;

                const VkDynamicState dynamic_states[] = {
                    VK_DYNAMIC_STATE_VIEWPORT,
                    VK_DYNAMIC_STATE_SCISSOR,
                };
                VkPipelineDynamicStateCreateInfo dynamic_state{};
                dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamic_state.dynamicStateCount = 2;
                dynamic_state.pDynamicStates = dynamic_states;

                VkPipelineRasterizationStateCreateInfo rasterizer{};
                rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                rasterizer.depthClampEnable = VK_FALSE;
                rasterizer.rasterizerDiscardEnable = VK_FALSE;
                rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
                rasterizer.lineWidth = 1.0f;
                rasterizer.cullMode = VK_CULL_MODE_NONE;
                rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

                VkPipelineMultisampleStateCreateInfo multisampling{};
                multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                multisampling.sampleShadingEnable = VK_FALSE;
                multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

                VkPipelineDepthStencilStateCreateInfo depth_stencil{};
                depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depth_stencil.depthTestEnable = VK_FALSE;
                depth_stencil.depthWriteEnable = VK_FALSE;
                depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
                depth_stencil.depthBoundsTestEnable = VK_FALSE;
                depth_stencil.stencilTestEnable = VK_FALSE;

                VkPipelineColorBlendAttachmentState color_blend_attachment{};
                color_blend_attachment.colorWriteMask =
                    VK_COLOR_COMPONENT_R_BIT |
                    VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT |
                    VK_COLOR_COMPONENT_A_BIT;
                color_blend_attachment.blendEnable = VK_FALSE;

                VkPipelineColorBlendStateCreateInfo color_blending{};
                color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                color_blending.logicOpEnable = VK_FALSE;
                color_blending.attachmentCount = 1;
                color_blending.pAttachments = &color_blend_attachment;

                VkPushConstantRange push_constant_range{};
                push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                push_constant_range.offset = 0;
                push_constant_range.size = static_cast<uint32_t>(sizeof(FractalPushConstants));

                VkPipelineLayoutCreateInfo pipeline_layout_info{};
                pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                pipeline_layout_info.setLayoutCount = 1;
                pipeline_layout_info.pSetLayouts = &fractal_descriptor_set_layout;
                pipeline_layout_info.pushConstantRangeCount = 1;
                pipeline_layout_info.pPushConstantRanges = &push_constant_range;

                if (vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &fractal_pipeline_layout) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create fractal pipeline layout");
                }

                VkPipelineRenderingCreateInfo pipeline_rendering_info{};
                pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                pipeline_rendering_info.viewMask = 0;
                pipeline_rendering_info.colorAttachmentCount = 1;
                pipeline_rendering_info.pColorAttachmentFormats = &color_format;
                pipeline_rendering_info.depthAttachmentFormat = depth_attachment_format;
                pipeline_rendering_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

                VkGraphicsPipelineCreateInfo pipeline_info{};
                pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipeline_info.pNext = &pipeline_rendering_info;
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
                pipeline_info.layout = fractal_pipeline_layout;
                pipeline_info.renderPass = VK_NULL_HANDLE;
                pipeline_info.subpass = 0;

                if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &fractal_pipeline) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create fractal graphics pipeline");
                }
            } catch (...) {
                if (fractal_pipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device, fractal_pipeline, nullptr);
                    fractal_pipeline = VK_NULL_HANDLE;
                }
                if (fractal_pipeline_layout != VK_NULL_HANDLE) {
                    vkDestroyPipelineLayout(device, fractal_pipeline_layout, nullptr);
                    fractal_pipeline_layout = VK_NULL_HANDLE;
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

        void ensureFractalResources() {
            const size_t required_count = std::max<size_t>(getSwapchainImageCount(), 1);
            if (reference_orbit_buffers.size() != required_count || fractal_descriptor_sets.size() != required_count) {
                destroyFractalResources();
            }

            if (reference_orbit_buffers.empty()) {
                createReferenceOrbitBuffers(required_count);
            }
            if (fractal_descriptor_set_layout == VK_NULL_HANDLE) {
                createDescriptorSetLayout();
            }
            if (fractal_descriptor_pool == VK_NULL_HANDLE) {
                createDescriptorPool();
            }
            if (fractal_descriptor_sets.empty()) {
                allocateDescriptorSets(required_count);
                writeDescriptorSets();
            }
        }

        void createReferenceOrbitBuffers(size_t buffer_count) {
            const VkDeviceSize buffer_size = static_cast<VkDeviceSize>(reference_orbit_capacity * sizeof(OrbitSample));

            reference_orbit_buffers.assign(buffer_count, VK_NULL_HANDLE);
            reference_orbit_memories.assign(buffer_count, VK_NULL_HANDLE);
            reference_orbit_mapped.assign(buffer_count, nullptr);
            reference_orbit_coherent.assign(buffer_count, false);
            reference_orbit_uploaded_generations.assign(buffer_count, 0);

            for (size_t i = 0; i < buffer_count; ++i) {
                try {
                    createBuffer(buffer_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, reference_orbit_buffers[i], reference_orbit_memories[i]);
                    reference_orbit_coherent[i] = true;
                } catch (...) {
                    createBuffer(buffer_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, reference_orbit_buffers[i], reference_orbit_memories[i]);
                    reference_orbit_coherent[i] = false;
                }

                if (vkMapMemory(device, reference_orbit_memories[i], 0, buffer_size, 0, &reference_orbit_mapped[i]) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to map fractal reference orbit buffer");
                }
            }
        }

        void createDescriptorSetLayout() {
            VkDescriptorSetLayoutBinding orbit_binding{};
            orbit_binding.binding = 0;
            orbit_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            orbit_binding.descriptorCount = 1;
            orbit_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo layout_info{};
            layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout_info.bindingCount = 1;
            layout_info.pBindings = &orbit_binding;

            if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &fractal_descriptor_set_layout) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create fractal descriptor set layout");
            }
        }

        void createDescriptorPool() {
            VkDescriptorPoolSize pool_size{};
            pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            pool_size.descriptorCount = static_cast<uint32_t>(reference_orbit_buffers.size());

            VkDescriptorPoolCreateInfo pool_info{};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.maxSets = static_cast<uint32_t>(reference_orbit_buffers.size());
            pool_info.poolSizeCount = 1;
            pool_info.pPoolSizes = &pool_size;

            if (vkCreateDescriptorPool(device, &pool_info, nullptr, &fractal_descriptor_pool) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create fractal descriptor pool");
            }
        }

        void allocateDescriptorSets(size_t descriptor_count) {
            std::vector<VkDescriptorSetLayout> layouts(descriptor_count, fractal_descriptor_set_layout);
            fractal_descriptor_sets.assign(descriptor_count, VK_NULL_HANDLE);

            VkDescriptorSetAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc_info.descriptorPool = fractal_descriptor_pool;
            alloc_info.descriptorSetCount = static_cast<uint32_t>(descriptor_count);
            alloc_info.pSetLayouts = layouts.data();

            if (vkAllocateDescriptorSets(device, &alloc_info, fractal_descriptor_sets.data()) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to allocate fractal descriptor sets");
            }
        }

        void writeDescriptorSets() {
            std::vector<VkDescriptorBufferInfo> buffer_infos(fractal_descriptor_sets.size());
            std::vector<VkWriteDescriptorSet> writes(fractal_descriptor_sets.size());

            for (size_t i = 0; i < fractal_descriptor_sets.size(); ++i) {
                buffer_infos[i].buffer = reference_orbit_buffers[i];
                buffer_infos[i].offset = 0;
                buffer_infos[i].range = static_cast<VkDeviceSize>(reference_orbit_capacity * sizeof(OrbitSample));

                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = fractal_descriptor_sets[i];
                writes[i].dstBinding = 0;
                writes[i].dstArrayElement = 0;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[i].pBufferInfo = &buffer_infos[i];
            }

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

        void updateReferenceOrbit(uint32_t image_index, VkExtent2D extent) {
            if (image_index >= reference_orbit_mapped.size() || reference_orbit_mapped[image_index] == nullptr) {
                return;
            }

            const bool needs_deep_references = zoom >= direct_reference_zoom_threshold;
            if (!needs_deep_references) {
                if (reference_orbit_dirty || cached_reference_count != 0) {
                    std::fill(reference_orbit_samples.begin(), reference_orbit_samples.end(), OrbitSample{});
                    reference_orbit_samples[0] = {0.0f, static_cast<float>(reference_metadata_capacity), static_cast<float>(reference_orbit_stride), 0.0f};
                    cached_reference_count = 0;
                    reference_orbit_dirty = false;
                    ++reference_orbit_generation;
                    orbit_length = 0;
                }
                uploadReferenceOrbit(image_index);
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const bool throttle_rebuild = reference_orbit_generation != 0 && (now - last_reference_rebuild_time) < reference_rebuild_interval;
            if (!reference_orbit_dirty || throttle_rebuild) {
                uploadReferenceOrbit(image_index);
                return;
            }

            const int iteration_count = std::clamp(max_iterations, 1, max_reference_iterations);
            const ReferenceScalar min_dimension = ReferenceScalar(std::max<uint32_t>(std::min(extent.width, extent.height), 1U));
            const ReferenceScalar width = ReferenceScalar(std::max<uint32_t>(extent.width, 1U));
            const ReferenceScalar height = ReferenceScalar(std::max<uint32_t>(extent.height, 1U));
            const ReferenceScalar half_width_uv = width / (ReferenceScalar(2) * min_dimension);
            const ReferenceScalar half_height_uv = height / (ReferenceScalar(2) * min_dimension);

            std::fill(reference_orbit_samples.begin(), reference_orbit_samples.end(), OrbitSample{});
            std::vector<ReferenceTile> tiles;
            tiles.reserve(max_adaptive_references);

            for (int root_y = 0; root_y < adaptive_root_rows; ++root_y) {
                for (int root_x = 0; root_x < adaptive_root_cols; ++root_x) {
                    const ReferenceScalar tile_min_x = -half_width_uv + ReferenceScalar(root_x) * (ReferenceScalar(2) * half_width_uv) / ReferenceScalar(adaptive_root_cols);
                    const ReferenceScalar tile_max_x = -half_width_uv + ReferenceScalar(root_x + 1) * (ReferenceScalar(2) * half_width_uv) / ReferenceScalar(adaptive_root_cols);
                    const ReferenceScalar tile_min_y = -half_height_uv + ReferenceScalar(root_y) * (ReferenceScalar(2) * half_height_uv) / ReferenceScalar(adaptive_root_rows);
                    const ReferenceScalar tile_max_y = -half_height_uv + ReferenceScalar(root_y + 1) * (ReferenceScalar(2) * half_height_uv) / ReferenceScalar(adaptive_root_rows);

                    tiles.push_back({tile_min_x, tile_min_y, tile_max_x, tile_max_y, 0});
                }
            }

            bool refined = true;
            const int validation_iteration_count = std::min(iteration_count, max_validation_iterations);
            while (refined && tiles.size() + 3 <= static_cast<size_t>(max_adaptive_references)) {
                refined = false;

                for (size_t tile_index = 0; tile_index < tiles.size(); ++tile_index) {
                    const ReferenceTile tile = tiles[tile_index];
                    if (tile.depth >= max_adaptive_depth || isAdaptiveReferenceTileStable(tile, validation_iteration_count)) {
                        continue;
                    }

                    const ReferenceScalar mid_x = (tile.min_uv_x + tile.max_uv_x) * ReferenceScalar("0.5");
                    const ReferenceScalar mid_y = (tile.min_uv_y + tile.max_uv_y) * ReferenceScalar("0.5");
                    const int child_depth = tile.depth + 1;
                    const std::array<ReferenceTile, 4> children{
                        ReferenceTile{tile.min_uv_x, tile.min_uv_y, mid_x, mid_y, child_depth},
                        ReferenceTile{mid_x, tile.min_uv_y, tile.max_uv_x, mid_y, child_depth},
                        ReferenceTile{tile.min_uv_x, mid_y, mid_x, tile.max_uv_y, child_depth},
                        ReferenceTile{mid_x, mid_y, tile.max_uv_x, tile.max_uv_y, child_depth}};

                    tiles.erase(tiles.begin() + static_cast<std::ptrdiff_t>(tile_index));
                    tiles.insert(tiles.begin() + static_cast<std::ptrdiff_t>(tile_index), children.begin(), children.end());
                    refined = true;
                    break;
                }
            }

            const int reference_count = static_cast<int>(std::min<size_t>(tiles.size(), max_adaptive_references));
            for (int reference_index = 0; reference_index < reference_count; ++reference_index) {
                const ReferenceTile &tile = tiles[static_cast<size_t>(reference_index)];
                const size_t orbit_base = referenceOrbitBase(reference_index);
                const ReferenceScalar ref_uv_x = (tile.min_uv_x + tile.max_uv_x) * ReferenceScalar("0.5");
                const ReferenceScalar ref_uv_y = (tile.min_uv_y + tile.max_uv_y) * ReferenceScalar("0.5");
                const int sample_count = writeReferenceOrbit(orbit_base, ref_uv_x, ref_uv_y, iteration_count);
                writeReferenceMetadata(reference_index, tile, orbit_base, sample_count, ref_uv_x, ref_uv_y);
            }

            reference_orbit_samples[0] = {
                static_cast<float>(reference_count),
                static_cast<float>(reference_metadata_capacity),
                static_cast<float>(reference_orbit_stride),
                0.0f};

            cached_reference_count = reference_count;
            reference_orbit_dirty = false;
            last_reference_rebuild_time = now;
            ++reference_orbit_generation;
            orbit_length = iteration_count + 1;
            uploadReferenceOrbit(image_index);
        }

        void uploadReferenceOrbit(uint32_t image_index) {
            if (image_index >= reference_orbit_mapped.size() || reference_orbit_mapped[image_index] == nullptr) {
                return;
            }
            if (image_index < reference_orbit_uploaded_generations.size() && reference_orbit_uploaded_generations[image_index] == reference_orbit_generation) {
                return;
            }

            const VkDeviceSize upload_size = static_cast<VkDeviceSize>(reference_orbit_samples.size() * sizeof(OrbitSample));
            std::memcpy(reference_orbit_mapped[image_index], reference_orbit_samples.data(), static_cast<size_t>(upload_size));

            if (!reference_orbit_coherent[image_index]) {
                VkMappedMemoryRange range{};
                range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                range.memory = reference_orbit_memories[image_index];
                range.offset = 0;
                range.size = upload_size;
                vkFlushMappedMemoryRanges(device, 1, &range);
            }

            if (image_index < reference_orbit_uploaded_generations.size()) {
                reference_orbit_uploaded_generations[image_index] = reference_orbit_generation;
            }
        }

        bool isAdaptiveReferenceTileStable(const ReferenceTile &tile, int iteration_count) {
            const size_t orbit_base = referenceOrbitBase(max_adaptive_references);
            const ReferenceScalar ref_uv_x = (tile.min_uv_x + tile.max_uv_x) * ReferenceScalar("0.5");
            const ReferenceScalar ref_uv_y = (tile.min_uv_y + tile.max_uv_y) * ReferenceScalar("0.5");
            const int sample_count = writeReferenceOrbit(orbit_base, ref_uv_x, ref_uv_y, iteration_count);
            return isReferenceTileStable(tile, orbit_base, sample_count, ref_uv_x, ref_uv_y, iteration_count);
        }

        int writeReferenceOrbit(size_t orbit_base, const ReferenceScalar &ref_uv_x, const ReferenceScalar &ref_uv_y, int iteration_count) {
            const ReferenceScalar c_x = center_x + ref_uv_x / zoom;
            const ReferenceScalar c_y = center_y + ref_uv_y / zoom;

            ReferenceScalar z_x = 0;
            ReferenceScalar z_y = 0;
            int sample_count = 1;

            reference_orbit_samples[orbit_base] = {
                toOrbitSampleScalar(z_x),
                toOrbitSampleScalar(z_y),
                1.0f,
                0.0f};

            for (int i = 0; i < iteration_count && sample_count < reference_orbit_stride; ++i) {
                const ReferenceScalar next_x = z_x * z_x - z_y * z_y + c_x;
                const ReferenceScalar next_y = ReferenceScalar(2) * z_x * z_y + c_y;

                z_x = next_x;
                z_y = next_y;

                reference_orbit_samples[orbit_base + static_cast<size_t>(sample_count)] = {
                    toOrbitSampleScalar(z_x),
                    toOrbitSampleScalar(z_y),
                    0.0f,
                    0.0f};
                ++sample_count;

                const ReferenceScalar mag2 = z_x * z_x + z_y * z_y;
                if (mag2 > ReferenceScalar(4)) {
                    break;
                }
            }

            reference_orbit_samples[orbit_base].z = static_cast<float>(sample_count);
            return sample_count;
        }

        void writeReferenceMetadata(int reference_index, const ReferenceTile &tile, size_t orbit_base, int sample_count, const ReferenceScalar &ref_uv_x, const ReferenceScalar &ref_uv_y) {
            const size_t metadata_base = 1 + static_cast<size_t>(reference_index) * 2;
            reference_orbit_samples[metadata_base] = {
                toOrbitSampleScalar(tile.min_uv_x),
                toOrbitSampleScalar(tile.min_uv_y),
                toOrbitSampleScalar(tile.max_uv_x),
                toOrbitSampleScalar(tile.max_uv_y)};
            reference_orbit_samples[metadata_base + 1] = {
                toOrbitSampleScalar(ref_uv_x),
                toOrbitSampleScalar(ref_uv_y),
                static_cast<float>(orbit_base),
                static_cast<float>(sample_count)};
        }

        bool isReferenceTileStable(const ReferenceTile &tile, size_t orbit_base, int sample_count, const ReferenceScalar &ref_uv_x, const ReferenceScalar &ref_uv_y, int iteration_count) const {
            const std::array<std::pair<ReferenceScalar, ReferenceScalar>, 5> sample_points{
                std::pair{tile.min_uv_x, tile.min_uv_y},
                std::pair{tile.max_uv_x, tile.min_uv_y},
                std::pair{tile.min_uv_x, tile.max_uv_y},
                std::pair{tile.max_uv_x, tile.max_uv_y},
                std::pair{(tile.min_uv_x + tile.max_uv_x) * ReferenceScalar("0.5"), (tile.min_uv_y + tile.max_uv_y) * ReferenceScalar("0.5")}};

            for (const auto &[sample_uv_x, sample_uv_y] : sample_points) {
                const float delta_c_x = ((sample_uv_x - ref_uv_x) / zoom).convert_to<float>();
                const float delta_c_y = ((sample_uv_y - ref_uv_y) / zoom).convert_to<float>();
                if (!isPerturbationSampleStable(orbit_base, sample_count, iteration_count, delta_c_x, delta_c_y)) {
                    return false;
                }
            }

            return true;
        }

        bool isPerturbationSampleStable(size_t orbit_base, int sample_count, int iteration_count, float delta_c_x, float delta_c_y) const {
            float dz_x = 0.0f;
            float dz_y = 0.0f;
            const int count = std::min(iteration_count, sample_count - 1);

            for (int i = 0; i < count; ++i) {
                const OrbitSample &ref = reference_orbit_samples[orbit_base + static_cast<size_t>(i)];
                const float z_dz_x = ref.x * dz_x - ref.y * dz_y;
                const float z_dz_y = ref.x * dz_y + ref.y * dz_x;
                const float dz_sq_x = dz_x * dz_x - dz_y * dz_y;
                const float dz_sq_y = 2.0f * dz_x * dz_y;

                dz_x = 2.0f * z_dz_x + dz_sq_x + delta_c_x;
                dz_y = 2.0f * z_dz_y + dz_sq_y + delta_c_y;

                if (!std::isfinite(dz_x) || !std::isfinite(dz_y)) {
                    return false;
                }

                const OrbitSample &next_ref = reference_orbit_samples[orbit_base + static_cast<size_t>(i + 1)];
                const float true_x = next_ref.x + dz_x;
                const float true_y = next_ref.y + dz_y;
                const float true_mag2 = true_x * true_x + true_y * true_y;
                if (!std::isfinite(true_mag2) || true_mag2 > 4.0f) {
                    return true;
                }

                const float dz_mag2 = dz_x * dz_x + dz_y * dz_y;
                const float ref_mag2 = ref.x * ref.x + ref.y * ref.y;
                if (!std::isfinite(dz_mag2) || dz_mag2 > perturbation_breakdown_limit2 || (ref_mag2 > 0.0f && dz_mag2 > ref_mag2 * 0.25f)) {
                    return false;
                }
            }

            return sample_count > iteration_count;
        }

        void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer, VkDeviceMemory &bufferMemory) {
            VkBufferCreateInfo buffer_info{};
            buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer_info.size = size;
            buffer_info.usage = usage;
            buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(device, &buffer_info, nullptr, &buffer) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create fractal buffer");
            }

            VkMemoryRequirements mem_requirements{};
            vkGetBufferMemoryRequirements(device, buffer, &mem_requirements);

            VkMemoryAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc_info.allocationSize = mem_requirements.size;
            try {
                alloc_info.memoryTypeIndex = findMemoryType(mem_requirements.memoryTypeBits, properties);
            } catch (...) {
                vkDestroyBuffer(device, buffer, nullptr);
                buffer = VK_NULL_HANDLE;
                throw;
            }

            if (vkAllocateMemory(device, &alloc_info, nullptr, &bufferMemory) != VK_SUCCESS) {
                vkDestroyBuffer(device, buffer, nullptr);
                buffer = VK_NULL_HANDLE;
                throw mxvk::Exception("Failed to allocate fractal buffer memory");
            }

            if (vkBindBufferMemory(device, buffer, bufferMemory, 0) != VK_SUCCESS) {
                vkFreeMemory(device, bufferMemory, nullptr);
                bufferMemory = VK_NULL_HANDLE;
                vkDestroyBuffer(device, buffer, nullptr);
                buffer = VK_NULL_HANDLE;
                throw mxvk::Exception("Failed to bind fractal buffer memory");
            }
        }

        uint32_t findMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
            VkPhysicalDeviceMemoryProperties mem_properties{};
            vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

            for (uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
                const bool type_matches = (type_filter & (1U << i)) != 0U;
                const bool property_matches = (mem_properties.memoryTypes[i].propertyFlags & properties) == properties;
                if (type_matches && property_matches) {
                    return i;
                }
            }

            throw mxvk::Exception("Failed to find suitable memory type for fractal buffer");
        }

        using PushScalar =
#if defined(MXVK_USE_MOLTENVK)
            float;
#else
            double;
#endif
        ;

        struct FractalPushConstants {
            PushScalar center_x;
            PushScalar center_y;
            PushScalar inverse_zoom;
            PushScalar time;
            PushScalar resolution_x;
            PushScalar resolution_y;
            int max_iterations;
            int palette;
            int orbit_length;
            int reserved;
        };

        VkPipeline fractal_pipeline = VK_NULL_HANDLE;
        VkPipelineLayout fractal_pipeline_layout = VK_NULL_HANDLE;
        VkDescriptorSetLayout fractal_descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool fractal_descriptor_pool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> fractal_descriptor_sets{};
        std::vector<VkBuffer> reference_orbit_buffers{};
        std::vector<VkDeviceMemory> reference_orbit_memories{};
        std::vector<void *> reference_orbit_mapped{};
        std::vector<bool> reference_orbit_coherent{};
        std::vector<uint64_t> reference_orbit_uploaded_generations{};

        static constexpr int max_reference_iterations = 4096;
        static constexpr int adaptive_root_cols = 3;
        static constexpr int adaptive_root_rows = 2;
        static constexpr int max_adaptive_references = 32;
        static constexpr int max_adaptive_depth = 4;
        static constexpr int max_validation_iterations = 768;
        static constexpr int reference_metadata_capacity = 1 + max_adaptive_references * 2;
        static constexpr int reference_orbit_stride = max_reference_iterations + 1;
        static constexpr int reference_orbit_capacity = reference_metadata_capacity + (max_adaptive_references + 1) * reference_orbit_stride;
        static constexpr float perturbation_breakdown_limit2 = 0.0625f;
        static constexpr std::chrono::milliseconds reference_rebuild_interval{100};
#if defined(MXVK_USE_MOLTENVK)
        static inline const ReferenceScalar direct_reference_zoom_threshold = ReferenceScalar(4096);
#else
        static inline const ReferenceScalar direct_reference_zoom_threshold = ReferenceScalar("1e15");
#endif
        static inline const ReferenceScalar MAX_ZOOM = ReferenceScalar("1e1000");
        std::vector<OrbitSample> reference_orbit_samples{};
        int orbit_length = 0;
        int cached_reference_count = -1;
        bool reference_orbit_dirty = true;
        uint64_t reference_orbit_generation = 1;
        std::chrono::steady_clock::time_point last_reference_rebuild_time{};

        static constexpr size_t referenceOrbitBase(int reference_index) {
            return static_cast<size_t>(reference_metadata_capacity) + static_cast<size_t>(reference_index) * static_cast<size_t>(reference_orbit_stride);
        }

        ReferenceScalar center_x = ReferenceScalar("-0.5");
        ReferenceScalar center_y = ReferenceScalar(0);
        ReferenceScalar zoom = ReferenceScalar(1);
        int max_iterations = 256;
        int palette_index = 0;

        bool dragging = false;
        int drag_start_mouse_x = 0;
        int drag_start_mouse_y = 0;
        ReferenceScalar drag_start_center_x = ReferenceScalar(0);
        ReferenceScalar drag_start_center_y = ReferenceScalar(0);

        std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point last_tick{std::chrono::steady_clock::now()};
        uint32_t snapshot_index = 0;
#if defined(MXWRITE_ENABLED)
        Writer video_writer{};
        uint32_t video_record_width = 0;
        uint32_t video_record_height = 0;
        std::string video_output_path = "output.mp4";
        static constexpr uint32_t recording_readback_slot_count = 4;
        std::vector<RecordingReadbackSlot> recording_readbacks{};
        std::vector<bool> recording_pending_images{};
        VkDeviceSize recording_row_bytes = 0;
        VkDeviceSize recording_image_bytes = 0;
        std::chrono::steady_clock::time_point next_recording_frame_time{};
        std::chrono::steady_clock::duration recording_frame_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / 60.0));
        bool recording_readbacks_ready = false;
        bool recording_format_is_bgra = false;
        uint64_t recording_dropped_frames = 0;
        std::jthread recording_worker{};
        std::mutex recording_mutex{};
        std::condition_variable recording_cv{};
        std::condition_variable recording_idle_cv{};
        std::queue<size_t> recording_ready_slots{};
        bool recording_worker_stop = false;
#endif
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::FractalWindow window(args.path, args.width, args.height, args.fullscreen, args.enable_vsync);
        window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
