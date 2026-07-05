#include "mutatris_window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>

#include "mxvk/mxvk_exception.hpp"

#ifndef mutatris_ASSET_DIR
#define mutatris_ASSET_DIR "."
#endif

#ifndef mutatris_FONT_PATH
#define mutatris_FONT_PATH "."
#endif

#ifndef mutatris_SHADER_DIR
#define mutatris_SHADER_DIR "."
#endif

namespace mutatris {

    MutatrisWindow::MutatrisWindow(const std::string &path, int width, int height, bool fullscreen, bool enableVsync)
        : mxvk::VK_Window("Mutatris", width, height, fullscreen, MXVK_VALIDATION, enableVsync),
          assetRoot((path.empty() || path == ".") ? std::string(mutatris_ASSET_DIR) : path),
          dataRoot(assetRoot + "/data"),
          shaderRoot(mutatris_SHADER_DIR) {
        setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        setFont(mutatris_FONT_PATH, 24);
        configureConsole();
        loadSprites();
        loadSoundEffects();
        syncControllerConnection();
        playSound(openSound);
        startupStartTick = SDL_GetTicks();
        logMutatris(std::format("Mutatris ready. {} shader effect(s) available.", effectShaders.size()));
    }

    void MutatrisWindow::event(SDL_Event &e) {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
            if (!controller.active()) {
                controller.connectEvent(e);
            }
            syncControllerConnection();
            return;
        }
        if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
            controller.connectEvent(e);
            syncControllerConnection();
            return;
        }

        const bool wasConsoleVisible = console.isVisible();
        const bool consoleToggle = e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F3;
        console.handleEvent(e);
        if (consoleToggle) {
            logMutatris(console.isVisible() ? "Console opened." : "Console closed.");
            return;
        }
        if (wasConsoleVisible) {
            return;
        }
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
            logMutatris("Exit requested.");
            exit();
            return;
        }
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            handleKey(e.key.key);
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_FINGER_DOWN) {
            handleConfirm();
        } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            handleGamepad(e.gbutton.button);
        }
    }

    void MutatrisWindow::proc() {
        const VkExtent2D extent = getSwapchainExtent();
        width = extent.width > 0U ? static_cast<int>(extent.width) : DESIGN_WIDTH;
        height = extent.height > 0U ? static_cast<int>(extent.height) : DESIGN_HEIGHT;

        ensureMusicPlaying();
        switch (screen) {
        case Screen::Startup:
            drawStartup();
            break;
        case Screen::Title:
            drawTitle();
            break;
        case Screen::Difficulty:
            drawDifficulty();
            break;
        case Screen::Playing:
            updatePlaying();
            break;
        case Screen::GameOver:
            drawGameOver();
            break;
        }
        console.draw();
    }

    void MutatrisWindow::configureConsole() {
        console.attach(*this, mutatris_FONT_PATH, 24);
        console.setSpriteYOriginTopLeft(true);
        console.setPrompt("mutatris> ");
        console.printLine("Press F3 to open/close the console.");
        console.printLine("Type 'help' for commands.");
        console.setCommandCallback([this](mxvk::VK_Window &, const std::vector<std::string> &args, std::ostream &out) {
            if (args.empty()) {
                return true;
            }

            if (args[0] == "help") {
                out << "Commands:\n"
                    << "  help            Show this help message\n"
                    << "  clear           Clear the console output\n"
                    << "  echo <text>     Print text to the console\n"
                    << "  switch_shader   Pick a random background and shader\n"
                    << "  about           Show app information\n"
                    << "  quit | exit     Close Mutatris";
                return true;
            }

            if (args[0] == "echo") {
                for (std::size_t i = 1; i < args.size(); ++i) {
                    if (i > 1) {
                        out << ' ';
                    }
                    out << args[i];
                }
                return true;
            }

            if (args[0] == "switch_shader") {
                const std::string shaderName = switchBackgroundShader(true);
                if (shaderName.empty()) {
                    out << "No shader effects are available.";
                } else {
                    logMutatris(std::format("Console command switch_shader selected {} with shader {}", backgroundName(backgroundIndex), shaderName));
                    out << std::format("Switched to {} with shader {}", backgroundName(backgroundIndex), shaderName);
                }
                return true;
            }

            if (args[0] == "about") {
                out << "Mutatris: rotating-grid puzzle game built with MXVK.";
                return true;
            }

            if (args[0] == "quit" || args[0] == "exit") {
                out << "Closing Mutatris...";
                logMutatris("Exit requested from console.");
                exit();
                return true;
            }

            return false;
        });
    }

    void MutatrisWindow::logMutatris(const std::string &message) {
        std::cout << std::format("mutatris: {}\n", message);
        console.printLine(message, SDL_Color{180, 220, 255, 255});
    }

    const char *MutatrisWindow::screenName(Screen value) const {
        switch (value) {
        case Screen::Startup:
            return "Startup";
        case Screen::Title:
            return "Title";
        case Screen::Difficulty:
            return "Difficulty";
        case Screen::Playing:
            return "Playing";
        case Screen::GameOver:
            return "GameOver";
        }
        return "Unknown";
    }

    const char *MutatrisWindow::focusName(int value) const {
        switch (value) {
        case 0:
            return "Top";
        case 1:
            return "Left";
        case 2:
            return "Bottom";
        case 3:
            return "Right";
        default:
            return "Unknown";
        }
    }

    void MutatrisWindow::setScreen(Screen nextScreen, const std::string &reason) {
        if (screen == nextScreen) {
            return;
        }
        const Screen previousScreen = screen;
        screen = nextScreen;
        logMutatris(std::format("Screen changed {} -> {} ({})", screenName(previousScreen), screenName(nextScreen), reason));
    }

    void MutatrisWindow::loadSprites() {
        const std::string backgroundVertexShader = shaderRoot + "/background.vert.spv";
        const std::string backgroundShader = shaderRoot + "/background.frag.spv";
        const std::string fadeShader = shaderRoot + "/fade.frag.spv";
        loadEffectShaders();
        for (std::size_t i = 0; i < backgrounds.size(); ++i) {
            backgrounds[i] = createSprite(dataRoot + "/blocks" + (i == 0 ? std::string("") : std::to_string(i)) + ".png", backgroundVertexShader, backgroundShader);
        }
        intro = createSprite(dataRoot + "/intro.png", backgroundVertexShader, fadeShader);
        start = createSprite(dataRoot + "/start.png");
        lostLogo = createSprite(dataRoot + "/lostlogo.png", backgroundVertexShader, fadeShader);
        jbLogo = createSprite(dataRoot + "/jblogo.png", backgroundVertexShader, fadeShader);
        gameOver = createSprite(dataRoot + "/gameover.png");
        highScore = createSprite(dataRoot + "/highscore.png");

        const std::array<std::string, 8> blockFiles{{
            "block_black.png",
            "block_clear.png",
            "block_dblue.png",
            "block_green.png",
            "block_ltblue.png",
            "block_orange.png",
            "block_red.png",
            "block_yellow.png",
        }};
        for (std::size_t i = 0; i < blockFiles.size(); ++i) {
            blocks[i] = createSprite(dataRoot + "/" + blockFiles[i]);
        }
    }

    void MutatrisWindow::loadEffectShaders() {
        effectShaders.clear();
        const std::filesystem::path effectDir = std::filesystem::path(shaderRoot) / "effects";
        if (!std::filesystem::exists(effectDir)) {
            return;
        }
        for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(effectDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".spv") {
                effectShaders.push_back(entry.path().string());
            }
        }
        std::sort(effectShaders.begin(), effectShaders.end());
        std::cout << std::format("mutatris: loaded {} shader effect(s)\n", effectShaders.size());
    }

    void MutatrisWindow::loadSoundEffects() {
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        soundEffects = std::make_unique<mxvk::VK_Mixer>();
        musicTrack = soundEffects->loadMusic(dataRoot + "/music.ogg");
        lineSound = soundEffects->loadWav(dataRoot + "/line.wav");
        openSound = soundEffects->loadWav(dataRoot + "/open.wav");
        logMutatris("Loaded original Mutatris sound effects.");
        ensureMusicPlaying();
#endif
    }

    void MutatrisWindow::playSound(int soundId) {
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        if (soundEffects == nullptr || soundId < 0) {
            return;
        }
        soundEffects->playWav(soundId);
#else
        (void)soundId;
#endif
    }

    void MutatrisWindow::ensureMusicPlaying() {
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        if (soundEffects == nullptr || musicTrack < 0 || soundEffects->isMusicPlaying(musicTrack)) {
            return;
        }
        if (soundEffects->playMusic(musicTrack, -1) != 0) {
            throw mxvk::Exception("Could not start Mutatris background music");
        }
#endif
    }

    void MutatrisWindow::handleKey(SDL_Keycode key) {
        if (screen == Screen::Startup) {
            if ((key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) && isStartupTitleFullyVisible()) {
                setScreen(Screen::Difficulty, "difficulty select opened");
            } else {
                setScreen(Screen::Title, "startup confirmed");
            }
            return;
        }
        if (screen == Screen::Title && (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)) {
            setScreen(Screen::Difficulty, "difficulty select opened");
            return;
        }
        if (screen == Screen::Difficulty) {
            if (key == SDLK_LEFT && difficulty > 0) {
                --difficulty;
                logMutatris(std::format("Difficulty changed to {}", difficulty));
            } else if (key == SDLK_RIGHT && difficulty < 2) {
                ++difficulty;
                logMutatris(std::format("Difficulty changed to {}", difficulty));
            } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
                startGame();
            }
            return;
        }
        if (screen == Screen::GameOver && (key == SDLK_SPACE || key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
            setScreen(Screen::Title, "returned from game over");
            return;
        }
        if (screen != Screen::Playing || game == nullptr) {
            return;
        }
        if (key == SDLK_W) {
            game->grid[focus].gamePiece.shiftColors();
            return;
        }
        if (key == SDLK_A || key == SDLK_SPACE) {
            game->grid[focus].gamePiece.shiftDirection();
            return;
        }
        if (key == SDLK_S) {
            if (game->grid[focus].gamePiece.drop()) {
                processGrid();
                advanceFocus();
            }
            return;
        }
        handleDirectionalKey(key);
    }

    void MutatrisWindow::handleGamepad(Uint8 button) {
        if (button == SDL_GAMEPAD_BUTTON_BACK) {
            logMutatris("Exit requested.");
            exit();
            return;
        }
        if (screen != Screen::Playing) {
            if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                handleConfirm();
            } else if (screen == Screen::Difficulty && button == SDL_GAMEPAD_BUTTON_DPAD_LEFT && difficulty > 0) {
                --difficulty;
                logMutatris(std::format("Difficulty changed to {}", difficulty));
            } else if (screen == Screen::Difficulty && button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT && difficulty < 2) {
                ++difficulty;
                logMutatris(std::format("Difficulty changed to {}", difficulty));
            }
            return;
        }
        if (game == nullptr) {
            return;
        }
        if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
            game->grid[focus].gamePiece.shiftColors();
        } else if (button == SDL_GAMEPAD_BUTTON_EAST) {
            game->grid[focus].gamePiece.shiftDirection();
        } else if (button == SDL_GAMEPAD_BUTTON_NORTH) {
            if (game->grid[focus].gamePiece.drop()) {
                processGrid();
                advanceFocus();
            }
        } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
            handleGamepadDpad(button);
        } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
            handleGamepadDpad(button);
        } else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
            handleGamepadDpad(button);
        } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
            handleGamepadDpad(button);
        }
    }

    void MutatrisWindow::handleGamepadDpad(Uint8 button) {
        if (game == nullptr) {
            return;
        }
        const Uint32 now = SDL_GetTicks();
        if (now - lastInputTick < KEY_REPEAT_MS) {
            return;
        }

        Piece &piece = game->grid[focus].gamePiece;
        bool handled = true;
        if (focus == 0) {
            if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
                piece.moveLeft();
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                piece.moveRight();
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                softDropActivePiece();
            } else {
                handled = false;
            }
        } else if (focus == 1) {
            if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                piece.moveLeft();
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                piece.moveRight();
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                softDropActivePiece();
            } else {
                handled = false;
            }
        } else if (focus == 2) {
            if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
                piece.moveLeft();
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                piece.moveRight();
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                softDropActivePiece();
            } else {
                handled = false;
            }
        } else if (focus == 3) {
            if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                piece.moveLeft();
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                piece.moveRight();
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
                softDropActivePiece();
            } else {
                handled = false;
            }
        }

        if (handled) {
            lastInputTick = now;
        }
    }

    void MutatrisWindow::handleConfirm() {
        if (screen == Screen::Startup) {
            if (isStartupTitleFullyVisible()) {
                setScreen(Screen::Difficulty, "difficulty select opened");
            } else {
                setScreen(Screen::Title, "startup confirmed");
            }
        } else if (screen == Screen::Title) {
            setScreen(Screen::Difficulty, "difficulty select opened");
        } else if (screen == Screen::Difficulty) {
            startGame();
        } else if (screen == Screen::GameOver) {
            setScreen(Screen::Title, "returned from game over");
        }
    }

    bool MutatrisWindow::isStartupTitleFullyVisible() const {
        const Uint32 elapsed = SDL_GetTicks() - startupStartTick;
        constexpr Uint32 FIRST_FADE_IN_END = STARTUP_FADE_MS;
        constexpr Uint32 FIRST_FADE_OUT_END = FIRST_FADE_IN_END + STARTUP_FADE_MS;
        constexpr Uint32 SECOND_FADE_IN_END = FIRST_FADE_OUT_END + STARTUP_FADE_MS;
        constexpr Uint32 SECOND_HOLD_END = SECOND_FADE_IN_END + STARTUP_HOLD_MS;
        constexpr Uint32 SECOND_FADE_OUT_END = SECOND_HOLD_END + STARTUP_FADE_MS;

        return elapsed >= SECOND_FADE_OUT_END;
    }

    void MutatrisWindow::handleDirectionalKey(SDL_Keycode key) {
        if (game == nullptr) {
            return;
        }
        const Uint32 now = SDL_GetTicks();
        if (now - lastInputTick < KEY_REPEAT_MS) {
            return;
        }
        lastInputTick = now;
        Piece &piece = game->grid[focus].gamePiece;
        if (focus == 0) {
            if (key == SDLK_LEFT) {
                piece.moveLeft();
            } else if (key == SDLK_RIGHT) {
                piece.moveRight();
            } else if (key == SDLK_UP) {
                piece.shiftColors();
            } else if (key == SDLK_DOWN) {
                if (piece.moveDown()) {
                    processGrid();
                    advanceFocus();
                }
            }
        } else if (focus == 1) {
            if (key == SDLK_DOWN) {
                piece.moveRight();
            } else if (key == SDLK_UP) {
                piece.moveLeft();
            } else if (key == SDLK_LEFT) {
                piece.shiftColors();
            } else if (key == SDLK_RIGHT) {
                if (piece.moveDown()) {
                    processGrid();
                    advanceFocus();
                }
            }
        } else if (focus == 2) {
            if (key == SDLK_LEFT) {
                piece.moveLeft();
            } else if (key == SDLK_RIGHT) {
                piece.moveRight();
            } else if (key == SDLK_DOWN) {
                piece.shiftColors();
            } else if (key == SDLK_UP) {
                if (piece.moveDown()) {
                    processGrid();
                    advanceFocus();
                }
            }
        } else if (focus == 3) {
            if (key == SDLK_DOWN) {
                piece.moveLeft();
            } else if (key == SDLK_UP) {
                piece.moveRight();
            } else if (key == SDLK_RIGHT) {
                piece.shiftColors();
            } else if (key == SDLK_LEFT) {
                if (piece.moveDown()) {
                    processGrid();
                    advanceFocus();
                }
            }
        }
    }

    void MutatrisWindow::softDropActivePiece() {
        if (game == nullptr) {
            return;
        }
        if (game->grid[focus].gamePiece.moveDown()) {
            processGrid();
            advanceFocus();
        }
    }

    bool MutatrisWindow::openController() {
        for (int index = 0; index < mxvk::VK_Controller::joysticks(); ++index) {
            if (controller.open(index)) {
                logMutatris("Controller connected: " + controller.name());
                return true;
            }
        }
        return false;
    }

    void MutatrisWindow::syncControllerConnection() {
        if (!controller.active()) {
            openController();
        }
    }

    void MutatrisWindow::processGrid() {
        if (game == nullptr) {
            return;
        }
        const int previousScore = game->score;
        const int previousClears = game->clears;
        const int previousLevel = game->level;
        const unsigned int previousTimeout = game->timeout;
        for (int i = 0; i < 4; ++i) {
            game->procBlocks();
        }
        if (game->score != previousScore || game->clears != previousClears || game->level != previousLevel || game->timeout != previousTimeout) {
            logMutatris(std::format("Grid processed. score {} -> {}, clears {} -> {}, level {} -> {}, timeout {} -> {}",
                                    previousScore,
                                    game->score,
                                    previousClears,
                                    game->clears,
                                    previousLevel + 1,
                                    game->level + 1,
                                    previousTimeout,
                                    game->timeout));
        }
    }

    void MutatrisWindow::advanceFocus() {
        const int previousFocus = focus;
        if (focus == 0) {
            focus = 3;
        } else if (focus == 3) {
            focus = 2;
        } else if (focus == 2) {
            focus = 1;
        } else {
            focus = 0;
        }
        lastDropTick = SDL_GetTicks();
        logMutatris(std::format("Focus advanced {} -> {}.", focusName(previousFocus), focusName(focus)));
    }

    void MutatrisWindow::startGame() {
        game = std::make_unique<PuzzleGame>(difficulty, [this]() {
            playSound(lineSound);
        });
        focus = 0;
        lastDropTick = SDL_GetTicks();
        lastClearAnimationTick = lastDropTick;
        shaderLevel = -1;
        shaderIndex = -1;
        backgroundIndex = -1;
        setScreen(Screen::Playing, "new game");
        logMutatris(std::format("New game started. difficulty={} timeout={}", difficulty, game->timeout));
    }

    void MutatrisWindow::drawStartup() {
        const Uint32 now = SDL_GetTicks();
        const Uint32 elapsed = now - startupStartTick;
        constexpr Uint32 FIRST_FADE_IN_END = STARTUP_FADE_MS;
        constexpr Uint32 FIRST_FADE_OUT_END = FIRST_FADE_IN_END + STARTUP_FADE_MS;
        constexpr Uint32 SECOND_FADE_IN_END = FIRST_FADE_OUT_END + STARTUP_FADE_MS;
        constexpr Uint32 SECOND_HOLD_END = SECOND_FADE_IN_END + STARTUP_HOLD_MS;
        constexpr Uint32 SECOND_FADE_OUT_END = SECOND_HOLD_END + STARTUP_FADE_MS;
        constexpr Uint32 TITLE_FADE_IN_END = SECOND_FADE_OUT_END + STARTUP_FADE_MS;

        if (elapsed < FIRST_FADE_IN_END) {
            drawStartupLogo(lostLogo, static_cast<float>(elapsed) / static_cast<float>(STARTUP_FADE_MS));
        } else if (elapsed < FIRST_FADE_OUT_END) {
            const Uint32 phaseElapsed = elapsed - FIRST_FADE_IN_END;
            drawStartupLogo(lostLogo, 1.0f - (static_cast<float>(phaseElapsed) / static_cast<float>(STARTUP_FADE_MS)));
        } else if (elapsed < SECOND_FADE_IN_END) {
            const Uint32 phaseElapsed = elapsed - FIRST_FADE_OUT_END;
            drawStartupLogo(jbLogo, static_cast<float>(phaseElapsed) / static_cast<float>(STARTUP_FADE_MS));
        } else if (elapsed < SECOND_HOLD_END) {
            drawStartupLogo(jbLogo, 1.0f);
        } else if (elapsed < SECOND_FADE_OUT_END) {
            const Uint32 phaseElapsed = elapsed - SECOND_HOLD_END;
            const float transitionAlpha = static_cast<float>(phaseElapsed) / static_cast<float>(STARTUP_FADE_MS);
            const float titleAlpha = std::clamp((transitionAlpha - 0.78f) / 0.22f, 0.0f, 1.0f);
            drawTitleWithAlpha(titleAlpha);
            drawStartupLogo(jbLogo, 1.0f - transitionAlpha);
        } else if (elapsed < TITLE_FADE_IN_END) {
            drawTitleWithAlpha(1.0f);
        } else {
            setScreen(Screen::Title, "startup sequence complete");
        }
    }

    void MutatrisWindow::drawStartupLogo(mxvk::VK_Sprite *sprite, float alphaValue) {
        if (sprite == nullptr) {
            return;
        }
        sprite->setShaderParams(std::clamp(alphaValue, 0.0f, 1.0f), 0.0f, 0.0f, 0.0f);
        sprite->drawSpriteRect(0, 0, width, height);
    }

    void MutatrisWindow::drawTitle() {
        drawTitleWithAlpha(1.0f);
    }

    void MutatrisWindow::drawTitleWithAlpha(float alphaValue) {
        ensureIntroFonts();
        const Uint8 alphaByte = static_cast<Uint8>(std::clamp(static_cast<int>(std::lround(alphaValue * 255.0f)), 0, 255));
        if (intro != nullptr) {
            intro->setShaderParams(std::clamp(alphaValue, 0.0f, 1.0f), 0.0f, 0.0f, 0.0f);
            intro->drawSpriteRect(0, 0, width, height);
        }
        printScaledText("[Press Enter to Play]", 25, 25, {255, 255, 255, alphaByte}, titleFont);
        printScaledText("Mutatris", 50, 250, {255, 255, 255, alphaByte}, titleLargeFont);
        printScaledText("lostsidedead.biz", 780, 670, {80, 120, 255, alphaByte}, titleFont);
    }

    void MutatrisWindow::drawDifficulty() {
        ensureIntroFonts();
        if (start != nullptr) {
            start->drawSpriteRect(0, 0, width, height);
        }
        printScaledText("Mutatris", 560, 325, {255, 255, 255, 255}, startMediumFont);
        printScaledText("Press Space", 600, 595, {255, 255, 255, 255}, startSmallFont);
        const std::array<std::string, 3> labels{{"Easy", "Medium", "Hard"}};
        for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
            const SDL_Color color = i == difficulty ? SDL_Color{255, 230, 64, 255} : SDL_Color{255, 255, 255, 255};
            printScaledText(labels[static_cast<std::size_t>(i)], 430 + i * 180, 475, color, difficultyFont);
        }
    }

    void MutatrisWindow::updatePlaying() {
        if (game == nullptr) {
            startGame();
        }
        processGrid();
        updateBackgroundShaderForLevel();
        mxvk::VK_Sprite *background = currentBackground();
        if (background != nullptr) {
            background->setShaderParams(static_cast<float>(SDL_GetTicks()) / 1000.0f, 0.0f, 0.0f, 0.0f);
            background->drawSpriteRect(0, 0, width, height);
        }
        twistClearedBlocks();
        drawGrid(0);
        drawGrid(1);
        drawGrid(2);
        drawGrid(3);
        if (!console.isVisible()) {
            printScaledText("Level: " + std::to_string(game->level + 1) + "  Timeout: " + std::to_string(game->timeout), 25, 25, {255, 255, 255, 255});
            printScaledText("Score: " + std::to_string(game->score), 25, 55, {255, 255, 255, 255});
            printScaledText("Direction: " + std::to_string(focus), 25, 85, {255, 255, 255, 255});
            printScaledText("Arrows move  W shift  A rotate  S drop", 25, 670, {220, 220, 220, 255});
        }

        const Uint32 now = SDL_GetTicks();
        if (now - lastDropTick >= game->timeout) {
            if (game->grid[focus].canMoveDown()) {
                if (game->grid[focus].gamePiece.moveDown()) {
                    logMutatris(std::format("Timer drop advanced piece on {} grid.", focusName(focus)));
                    processGrid();
                    advanceFocus();
                }
                lastDropTick = now;
            } else {
                finalScore = game->score;
                finalClears = game->clears;
                setScreen(Screen::GameOver, "active grid blocked");
                logMutatris(std::format("Game over. score={} clears={}", finalScore, finalClears));
            }
        }
    }

    void MutatrisWindow::updateBackgroundShaderForLevel() {
        if (game == nullptr || game->level == shaderLevel) {
            return;
        }
        shaderLevel = game->level;
        const std::string shaderName = switchBackgroundShader(false);
        if (!shaderName.empty()) {
            logMutatris(std::format("Level {} selected {} with shader {}", game->level + 1, backgroundName(backgroundIndex), shaderName));
        }
    }

    std::string MutatrisWindow::switchBackgroundShader(bool force) {
        selectRandomBackground();
        if (effectShaders.empty()) {
            return {};
        }
        std::uniform_int_distribution<int> shaderDistribution(0, static_cast<int>(effectShaders.size()) - 1);
        int nextShaderIndex = shaderDistribution(shaderRandom);
        if (effectShaders.size() > 1U) {
            while (nextShaderIndex == shaderIndex) {
                nextShaderIndex = shaderDistribution(shaderRandom);
            }
        }
        shaderIndex = nextShaderIndex;

        const int targetLevel = game != nullptr ? game->level : 0;
        mxvk::VK_Sprite *background = currentBackground();
        if (background != nullptr) {
            background->setFragmentShaderPath(effectShaders[static_cast<std::size_t>(shaderIndex)]);
        }
        if (force) {
            shaderLevel = targetLevel;
        }
        return std::filesystem::path(effectShaders[static_cast<std::size_t>(shaderIndex)]).filename().string();
    }

    void MutatrisWindow::selectRandomBackground() {
        std::uniform_int_distribution<int> backgroundDistribution(0, static_cast<int>(backgrounds.size()) - 1);
        int nextBackgroundIndex = backgroundDistribution(shaderRandom);
        if (backgrounds.size() > 1U) {
            while (nextBackgroundIndex == backgroundIndex) {
                nextBackgroundIndex = backgroundDistribution(shaderRandom);
            }
        }
        backgroundIndex = nextBackgroundIndex;
    }

    mxvk::VK_Sprite *MutatrisWindow::currentBackground() {
        if (backgroundIndex < 0) {
            selectRandomBackground();
        }
        return backgrounds[static_cast<std::size_t>(std::clamp(backgroundIndex, 0, static_cast<int>(backgrounds.size()) - 1))];
    }

    std::string MutatrisWindow::backgroundName(int index) {
        if (index <= 0) {
            return "blocks.png";
        }
        return std::format("blocks{}.png", index);
    }

    void MutatrisWindow::drawGameOver() {
        mxvk::VK_Sprite *sprite = finalScore >= 200 ? highScore : gameOver;
        if (sprite != nullptr) {
            sprite->drawSpriteRect(0, 0, width, height);
        }
        printScaledText((finalScore >= 200 ? "High Score: " : "Game Over Score: ") + std::to_string(finalScore) + "  Clears: " + std::to_string(finalClears), 25, 25, {255, 255, 255, 255});
        printScaledText("[ Press Space ]", 25, finalScore >= 200 ? 400 : 100, {255, 255, 255, 255});
    }

    void MutatrisWindow::twistClearedBlocks() {
        const Uint32 now = SDL_GetTicks();
        const Uint32 elapsed = lastClearAnimationTick == 0U ? 0U : now - lastClearAnimationTick;
        lastClearAnimationTick = now;
        for (GameGrid &focusGrid : game->grid) {
            for (int y = 0; y < focusGrid.height(); ++y) {
                for (int x = 0; x < focusGrid.width(); ++x) {
                    Block *block = focusGrid.at(x, y);
                    if (block != nullptr && block->color < 0) {
                        block->clearElapsedMs += elapsed;
                        if (block->clearElapsedMs >= CLEAR_ANIMATION_MS) {
                            block->color = 0;
                            block->clearElapsedMs = 0;
                        } else {
                            const Uint32 frame = std::min<Uint32>((block->clearElapsedMs * CLEAR_ANIMATION_FRAMES) / CLEAR_ANIMATION_MS, CLEAR_ANIMATION_FRAMES - 1U);
                            block->color = -static_cast<int>(frame + 1U);
                        }
                    }
                }
            }
        }
    }

    void MutatrisWindow::drawGrid(int gridIndex) {
        const GameGrid &focusGrid = game->grid[static_cast<std::size_t>(gridIndex)];
        const BoardLayout layout = boardLayout(gridIndex);
        drawGridFrame(layout, gridIndex == focus);
        for (int y = 0; y < focusGrid.height(); ++y) {
            for (int x = 0; x < focusGrid.width(); ++x) {
                const Block *block = focusGrid.at(x, y);
                if (block != nullptr) {
                    drawCell(layout, x, y, block->color);
                }
            }
        }
        if (gridIndex == focus) {
            const Piece &piece = focusGrid.gamePiece;
            for (int i = 0; i < 3; ++i) {
                int x = piece.getX();
                int y = piece.getY();
                if (piece.getDirection() == 0) {
                    y += i;
                } else if (piece.getDirection() == 1) {
                    x += i;
                } else if (piece.getDirection() == 2) {
                    x -= i;
                } else if (piece.getDirection() == 3) {
                    y -= i;
                }
                const Block *block = piece.at(i);
                if (block != nullptr) {
                    drawCell(layout, x, y, block->color);
                }
            }
        }
    }

    MutatrisWindow::BoardLayout MutatrisWindow::boardLayout(int gridIndex) const {
        if (gridIndex == 0) {
            return {(DESIGN_WIDTH / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2), 0, gridIndex};
        }
        if (gridIndex == 1) {
            return {0, (DESIGN_HEIGHT / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2), gridIndex};
        }
        if (gridIndex == 2) {
            return {(DESIGN_WIDTH / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2), (DESIGN_HEIGHT / 2) + 5, gridIndex};
        }
        return {DESIGN_WIDTH - (SIDE_GRID_HEIGHT * BLOCK_HEIGHT), (DESIGN_HEIGHT / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2), gridIndex};
    }

    void MutatrisWindow::drawGridFrame(const MutatrisWindow::BoardLayout &layout, bool selected) {
        const bool sideGrid = layout.gridIndex == 1 || layout.gridIndex == 3;
        const int rows = sideGrid ? SIDE_GRID_HEIGHT : TOP_GRID_HEIGHT;
        const int boardW = sideGrid ? rows * BLOCK_HEIGHT : GRID_WIDTH * BLOCK_WIDTH;
        const int boardH = sideGrid ? GRID_WIDTH * BLOCK_WIDTH : rows * BLOCK_HEIGHT;
        const int thickness = selected ? 4 : 2;
        const bool drawTop = layout.gridIndex != 2;
        const bool drawBottom = layout.gridIndex != 0;
        mxvk::VK_Sprite *pixel = blocks[0];
        if (drawTop) {
            pixel->drawSpriteRect(scaleX(layout.x - thickness), scaleY(layout.y - thickness), scaleX(boardW + thickness * 2), scaleY(thickness));
        }
        if (drawBottom) {
            pixel->drawSpriteRect(scaleX(layout.x - thickness), scaleY(layout.y + boardH), scaleX(boardW + thickness * 2), scaleY(thickness));
        }
        pixel->drawSpriteRect(scaleX(layout.x - thickness), scaleY(layout.y), scaleX(thickness), scaleY(boardH));
        pixel->drawSpriteRect(scaleX(layout.x + boardW), scaleY(layout.y), scaleX(thickness), scaleY(boardH));
    }

    void MutatrisWindow::drawCell(const MutatrisWindow::BoardLayout &layout, int cellX, int cellY, int color) {
        if (cellX < 0 || cellY < 0) {
            return;
        }
        const int spriteIndex = color < 0 ? 1 + (std::abs(color) % 6) : std::clamp(color, 0, 7);
        mxvk::VK_Sprite *sprite = blocks[static_cast<std::size_t>(spriteIndex)];
        if (sprite == nullptr) {
            return;
        }
        int px = 0;
        int py = 0;
        int pw = BLOCK_DRAW_WIDTH;
        int ph = BLOCK_DRAW_HEIGHT;
        if (layout.gridIndex == 0) {
            px = layout.x + cellX * BLOCK_WIDTH;
            py = layout.y + cellY * BLOCK_HEIGHT;
        } else if (layout.gridIndex == 1) {
            px = layout.x + cellY * BLOCK_HEIGHT;
            py = layout.y + cellX * BLOCK_WIDTH;
            pw = BLOCK_DRAW_HEIGHT;
            ph = BLOCK_DRAW_WIDTH;
        } else if (layout.gridIndex == 2) {
            px = layout.x + cellX * BLOCK_WIDTH;
            py = layout.y + (BOTTOM_GRID_HEIGHT - 1 - cellY) * BLOCK_HEIGHT;
        } else {
            px = layout.x + (SIDE_GRID_HEIGHT - 1 - cellY) * BLOCK_HEIGHT;
            py = layout.y + (GRID_WIDTH - 1 - cellX) * BLOCK_WIDTH;
            pw = BLOCK_DRAW_HEIGHT;
            ph = BLOCK_DRAW_WIDTH;
        }
        sprite->drawSpriteRect(scaleX(px), scaleY(py), scaleX(pw), scaleY(ph));
    }

    void MutatrisWindow::ensureIntroFonts() {
        const float scale = std::max(0.2f, std::min(static_cast<float>(width) / static_cast<float>(DESIGN_WIDTH), static_cast<float>(height) / static_cast<float>(DESIGN_HEIGHT)));
        const int scaledBaseSize = std::max(1, static_cast<int>(std::lround(54.0f * scale)));
        if (scaledBaseSize == introFontSize) {
            return;
        }
        introFontSize = scaledBaseSize;
        resetScaledFont(titleFont, 44, scale);
        resetScaledFont(titleLargeFont, 220, scale);
        resetScaledFont(startMediumFont, 36, scale);
        resetScaledFont(startSmallFont, 18, scale);
        resetScaledFont(difficultyFont, 30, scale);
    }

    void MutatrisWindow::resetScaledFont(mxvk::Font &font, int designSize, float scale) {
        font.reset(mutatris_FONT_PATH, std::max(1, static_cast<int>(std::lround(static_cast<float>(designSize) * scale))));
    }

    void MutatrisWindow::drawTextCentered(const std::string &text, int y, SDL_Color color) {
        int textWidth = 0;
        int textHeight = 0;
        if (!getTextDimensions(text, textWidth, textHeight)) {
            textWidth = static_cast<int>(text.size()) * 14;
        }
        printScaledText(text, (DESIGN_WIDTH - textWidth) / 2, y, color);
    }

    void MutatrisWindow::printScaledText(const std::string &text, int x, int y, SDL_Color color) {
        printText(text, scaleX(x), scaleY(y), color);
    }

    void MutatrisWindow::printScaledText(const std::string &text, int x, int y, SDL_Color color, const mxvk::Font &font) {
        printText(text, scaleX(x), scaleY(y), color, font);
    }

    int MutatrisWindow::scaleX(int value) const {
        return static_cast<int>(static_cast<float>(value) * static_cast<float>(width) / static_cast<float>(DESIGN_WIDTH));
    }

    int MutatrisWindow::scaleY(int value) const {
        return static_cast<int>(static_cast<float>(value) * static_cast<float>(height) / static_cast<float>(DESIGN_HEIGHT));
    }

} // namespace mutatris
