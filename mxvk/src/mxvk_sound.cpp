/**
 * @file mxvk_sound.cpp
 * @brief Implementation of mxvk::VK_Mixer SDL3_mixer audio manager.
 */

#include "mxvk/mxvk_sound.hpp"

#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)

#include "mxvk/mxvk_exception.hpp"

#include <limits>
#include <string>

namespace mxvk {

    std::size_t VK_Mixer::toIndex(const int value) {
        return static_cast<std::size_t>(value);
    }

    VK_Mixer::VK_Mixer() {
        init();
    }

    VK_Mixer::~VK_Mixer() noexcept {
        cleanup();
    }

    void VK_Mixer::init() {
        if (initialized) {
            return;
        }

        if (!MIX_Init()) {
            throw mxvk::Exception("Could not initialize SDL3_mixer: " + std::string(SDL_GetError()));
        }

        SDL_AudioSpec spec{};
        spec.freq = 44100;
        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;

        MIX_Mixer *raw_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
        if (raw_mixer == nullptr) {
            MIX_Quit();
            throw mxvk::Exception("Could not create mixer device: " + std::string(SDL_GetError()));
        }

        mixer.reset(raw_mixer);
        initialized = true;
    }

    int VK_Mixer::loadMusic(const std::string &filename) {
        if (filename.empty()) {
            throw mxvk::Exception("Music filename cannot be empty");
        }
        if (!initialized || mixer == nullptr) {
            throw mxvk::Exception("Audio mixer is not initialized");
        }

        MIX_Audio *raw_audio = MIX_LoadAudio(mixer.get(), filename.c_str(), false);
        if (raw_audio == nullptr) {
            throw mxvk::Exception("Error loading music file '" + filename + "': " + std::string(SDL_GetError()));
        }

        MIX_Track *raw_track = MIX_CreateTrack(mixer.get());
        if (raw_track == nullptr) {
            MIX_DestroyAudio(raw_audio);
            throw mxvk::Exception("Error creating music track: " + std::string(SDL_GetError()));
        }
        if (!MIX_SetTrackAudio(raw_track, raw_audio)) {
            MIX_DestroyTrack(raw_track);
            MIX_DestroyAudio(raw_audio);
            throw mxvk::Exception("Error assigning music track audio: " + std::string(SDL_GetError()));
        }

        music_files.emplace_back(raw_audio, MIX_DestroyAudio);
        music_tracks.emplace_back(raw_track, MIX_DestroyTrack);
        return static_cast<int>(music_files.size() - 1);
    }

    int VK_Mixer::loadWav(const std::string &filename) {
        if (filename.empty()) {
            throw mxvk::Exception("WAV filename cannot be empty");
        }
        if (!initialized || mixer == nullptr) {
            throw mxvk::Exception("Audio mixer is not initialized");
        }

        MIX_Audio *raw_audio = MIX_LoadAudio(mixer.get(), filename.c_str(), true);
        if (raw_audio == nullptr) {
            throw mxvk::Exception("Error loading WAV file '" + filename + "': " + std::string(SDL_GetError()));
        }

        MIX_Track *raw_track = MIX_CreateTrack(mixer.get());
        if (raw_track == nullptr) {
            MIX_DestroyAudio(raw_audio);
            throw mxvk::Exception("Error creating WAV track: " + std::string(SDL_GetError()));
        }
        if (!MIX_SetTrackAudio(raw_track, raw_audio)) {
            MIX_DestroyTrack(raw_track);
            MIX_DestroyAudio(raw_audio);
            throw mxvk::Exception("Error assigning WAV track audio: " + std::string(SDL_GetError()));
        }

        wav_files.emplace_back(raw_audio, MIX_DestroyAudio);
        wav_tracks.emplace_back(raw_track, MIX_DestroyTrack);
        return static_cast<int>(wav_files.size() - 1);
    }

    int VK_Mixer::playMusic(int id, int value) {
        if (!initialized) {
            return -1;
        }
        if (id < 0 || id >= static_cast<int>(music_tracks.size())) {
            return -1;
        }

        MIX_Track *track = music_tracks[toIndex(id)].get();
        if (track == nullptr) {
            return -1;
        }
        if (!MIX_SetTrackLoops(track, value)) {
            return -1;
        }
        return MIX_PlayTrack(track, 0) ? 0 : -1;
    }

    int VK_Mixer::playWav(int id, int value, int channel) {
        if (!initialized) {
            return -1;
        }
        if (id < 0 || id >= static_cast<int>(wav_files.size())) {
            return -1;
        }

        int target = id;
        if (channel >= 0) {
            if (channel >= static_cast<int>(wav_tracks.size())) {
                return -1;
            }
            target = channel;
        }

        MIX_Track *track = wav_tracks[toIndex(target)].get();
        MIX_Audio *audio = wav_files[toIndex(id)].get();
        if (track == nullptr || audio == nullptr) {
            return -1;
        }
        if (!MIX_SetTrackAudio(track, audio)) {
            return -1;
        }
        if (!MIX_SetTrackLoops(track, value)) {
            return -1;
        }
        return MIX_PlayTrack(track, 0) ? target : -1;
    }

    [[nodiscard]] bool VK_Mixer::isPlaying(int channel) const {
        if (!initialized || channel < 0 || channel >= static_cast<int>(wav_tracks.size())) {
            return false;
        }

        MIX_Track *track = wav_tracks[toIndex(channel)].get();
        if (track == nullptr) {
            return false;
        }
        return MIX_TrackPlaying(track);
    }

    [[nodiscard]] bool VK_Mixer::isMusicPlaying(int id) const {
        if (!initialized || id < 0 || id >= static_cast<int>(music_tracks.size())) {
            return false;
        }

        MIX_Track *track = music_tracks[toIndex(id)].get();
        if (track == nullptr) {
            return false;
        }
        return MIX_TrackPlaying(track);
    }

    void VK_Mixer::stopMusic() {
        if (!initialized || mixer == nullptr) {
            return;
        }

        for (const auto &track : music_tracks) {
            if (track != nullptr) {
                MIX_StopTrack(track.get(), 0);
            }
        }
        MIX_StopAllTracks(mixer.get(), 0);
    }

    void VK_Mixer::cleanup() {
        if (!initialized) {
            return;
        }

        stopMusic();
        music_tracks.clear();
        wav_tracks.clear();
        music_files.clear();
        wav_files.clear();
        mixer.reset();
        MIX_Quit();
        initialized = false;
    }

} // namespace mxvk

#endif
