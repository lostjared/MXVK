#ifndef MUTATRIS_WINDOW_HPP
#define MUTATRIS_WINDOW_HPP

#include <SDL3/SDL.h>

#include <array>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_console.hpp"
#include "mxvk/mxvk_controller.hpp"
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
#include "mxvk/mxvk_sound.hpp"
#endif

#include "common.hpp"
#include "puzzle_game.hpp"

namespace mutatris {

    class MutatrisWindow final : public mxvk::VK_Window {
      public:
        MutatrisWindow(const std::string &path, int width, int height, bool fullscreen, bool enableVsync, bool enableCrt);

        void event(SDL_Event &e) override;
        void proc() override;

      private:
        struct BoardLayout {
            int x = 0;
            int y = 0;
            int gridIndex = 0;
        };

        std::string assetRoot;
        std::string dataRoot;
        std::string shaderRoot;
        std::string fontPath;
        std::array<mxvk::VK_Sprite *, 8> blocks{};
        std::array<mxvk::VK_Sprite *, 11> backgrounds{};
        std::vector<std::string> effectShaders;
        mxvk::VK_Console console;
        mxvk::VK_Controller controller{};
        mxvk::VK_Sprite *intro = nullptr;
        mxvk::VK_Sprite *start = nullptr;
        mxvk::VK_Sprite *lostLogo = nullptr;
        mxvk::VK_Sprite *jbLogo = nullptr;
        mxvk::VK_Sprite *gameOver = nullptr;
        mxvk::VK_Sprite *highScore = nullptr;
        mxvk::Font titleFont;
        mxvk::Font titleLargeFont;
        mxvk::Font startMediumFont;
        mxvk::Font startSmallFont;
        mxvk::Font difficultyFont;
        std::unique_ptr<PuzzleGame> game;
        Screen screen = Screen::Startup;
        int difficulty = 0;
        int focus = 0;
        Uint32 startupStartTick = 0;
        int uiFontSize = 0;
        int introFontSize = 0;
        Uint32 lastDropTick = 0;
        Uint32 lastInputTick = 0;
        Uint32 lastClearAnimationTick = 0;
        int finalScore = 0;
        int finalClears = 0;
        int width = DESIGN_WIDTH;
        int height = DESIGN_HEIGHT;
        std::mt19937 shaderRandom{std::random_device{}()};
        int shaderLevel = -1;
        int shaderIndex = -1;
        int backgroundIndex = -1;
        bool crtEnabled = false;
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        std::unique_ptr<mxvk::VK_Mixer> soundEffects{};
#endif
        int musicTrack = -1;
        int lineSound = -1;
        int openSound = -1;

        void configureConsole();
        void logMutatris(const std::string &message);
        [[nodiscard]] const char *screenName(Screen value) const;
        [[nodiscard]] const char *focusName(int value) const;
        void setScreen(Screen nextScreen, const std::string &reason);
        void loadSprites();
        void loadEffectShaders();
        void loadSoundEffects();
        void playSound(int soundId);
        void ensureMusicPlaying();
        void handleKey(SDL_Keycode key);
        void handleGamepad(Uint8 button);
        void handleGamepadDpad(Uint8 button);
        void handleConfirm();
        [[nodiscard]] bool isStartupTitleFullyVisible() const;
        void handleDirectionalKey(SDL_Keycode key);
        void softDropActivePiece();
        bool openController();
        void syncControllerConnection();
        void processGrid();
        void advanceFocus();
        void startGame();
        void drawStartup();
        void drawStartupLogo(mxvk::VK_Sprite *sprite, float alphaValue);
        void drawTitle();
        void drawTitleWithAlpha(float alphaValue);
        void drawDifficulty();
        void updatePlaying();
        void updateBackgroundShaderForLevel();
        std::string switchBackgroundShader(bool force);
        void selectRandomBackground();
        [[nodiscard]] mxvk::VK_Sprite *currentBackground();
        [[nodiscard]] static std::string backgroundName(int index);
        void drawGameOver();
        void twistClearedBlocks();
        void drawGrid(int gridIndex);
        [[nodiscard]] BoardLayout boardLayout(int gridIndex) const;
        void drawGridFrame(const BoardLayout &layout, bool selected);
        void drawCell(const BoardLayout &layout, int cellX, int cellY, int color);
        [[nodiscard]] float layoutScale() const;
        void ensureUiFont();
        void ensureIntroFonts();
        void resetScaledFont(mxvk::Font &font, int designSize, float scale);
        void drawTextCentered(const std::string &text, int y, SDL_Color color);
        void printScaledText(const std::string &text, int x, int y, SDL_Color color);
        void printScaledText(const std::string &text, int x, int y, SDL_Color color, const mxvk::Font &font);
        [[nodiscard]] int scaleX(int value) const;
        [[nodiscard]] int scaleY(int value) const;
    };

} // namespace mutatris

#endif
