/**
 * @file mxvk_sound.hpp
 * @brief SDL3_mixer audio subsystem wrapper.
 */
#ifndef _MXVK_SOUND_H_
#define _MXVK_SOUND_H_

#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace mxvk {

    /**
     * @class Mixer
     * @brief SDL3_mixer-based audio manager.
     *
     * Provides facilities for loading WAV sound chunks and streamed music
     * tracks, playing them, and querying playback state.
     * Only available when the library is built with MIXER enabled.
     */
    class Mixer {
      public:
        /** @brief Initialise SDL3_mixer and open the audio device. */
        Mixer();

        /** @brief Halt all playback and free all loaded audio resources. */
        ~Mixer() noexcept;

        Mixer(const Mixer &) = delete;
        Mixer &operator=(const Mixer &) = delete;
        Mixer(Mixer &&) = delete;
        Mixer &operator=(Mixer &&) = delete;

        /** @brief Open the audio device (called automatically by constructor). */
        void init();

        /**
         * @brief Load a WAV file as a sound effect chunk.
         * @param filename Path to the WAV file.
         * @return Index used to reference this chunk in playWav().
         */
        int loadWav(const std::string &filename);

        /**
         * @brief Load an audio file as streaming background music.
         * @param filename Path to the music file (OGG, MP3, WAV, etc.).
         * @return Index used to reference this track in playMusic().
         */
        int loadMusic(const std::string &filename);

        /**
         * @brief Start playing a previously loaded music track.
         * @param id    Music index returned by loadMusic().
         * @param value Number of additional loops (0 = play once, -1 = infinite).
         * @return 0 on success, negative on failure.
         */
        int playMusic(int id, int value = 0);

        /**
         * @brief Play a previously loaded WAV chunk.
         * @param id      Chunk index returned by loadWav().
         * @param value   Number of additional loops (0 = play once).
         * @param channel Track index to use (-1 = use track for @p id).
         * @return The channel used for playback, or -1 on failure.
         */
        int playWav(int id, int value = 0, int channel = -1);

        /**
         * @brief Query whether a sound track is currently playing.
         * @param channel Track index.
         * @return @c true if the channel is active.
         */
        [[nodiscard]] bool isPlaying(int channel) const;

        /** @brief Free all loaded audio chunks and music tracks. */
        void cleanup();

        /** @brief Stop background music playback immediately. */
        void stopMusic();

      private:
        using MixerHandle = std::unique_ptr<MIX_Mixer, decltype(&MIX_DestroyMixer)>;
        using AudioHandle = std::unique_ptr<MIX_Audio, decltype(&MIX_DestroyAudio)>;
        using TrackHandle = std::unique_ptr<MIX_Track, decltype(&MIX_DestroyTrack)>;

        bool init_ = false; ///< Whether the mixer is initialized.
        MixerHandle mixer_{nullptr, MIX_DestroyMixer};
        std::vector<AudioHandle> music_files_{};  ///< Loaded music audio files.
        std::vector<AudioHandle> wav_files_{};    ///< Loaded sound effect audio files.
        std::vector<TrackHandle> music_tracks_{}; ///< Tracks assigned to music files.
        std::vector<TrackHandle> wav_tracks_{};   ///< Tracks assigned to wav files.

        [[nodiscard]] static std::size_t toIndex(int value);
    };
} // namespace mxvk

#endif
#endif