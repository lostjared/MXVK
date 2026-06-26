#include "defender_window.hpp"

#include <algorithm>
#include <format>
#include <ostream>
#include <string>
#include <vector>

namespace defender {

    void DefenderWindow::configure_console() {
        console.attach(*this, asset_root + "/data/font.ttf", HUD_FONT_SIZE);
        console.setSpriteYOriginTopLeft(true);
        console.setPrompt("defender> ");
        console_ready = true;
        console.printLine("Press F3 to open/close the console.");
        console.printLine("Type 'help' for Defender commands.");
        log_game("Console attached.");
        log_game("Defender initialized.");
        console.setCommandCallback([this](mxvk::VK_Window &, const std::vector<std::string> &args, std::ostream &out) {
            return handle_console_command(args, out);
        });
    }

    bool DefenderWindow::handle_console_command(const std::vector<std::string> &args, std::ostream &out) {
        if (args.empty()) {
            return true;
        }

        const std::string &cmd = args.front();
        if (cmd == "help") {
            out << "Defender commands:\n"
                << "  clear                  Clear console output\n"
                << "  echo <text>            Print text to the console\n"
                << "  status                 Print current game state\n"
                << "  restart                Restart the game and countdown\n"
                << "  intro                  Return to the intro screen\n"
                << "  play                   Start playing immediately\n"
                << "  gameover               Force game over\n"
                << "  lives <count>          Set remaining lives\n"
                << "  score <points>         Set score\n"
                << "  killship               Destroy the ship once\n"
                << "  clear_enemies          Remove active UFOs and asteroids\n"
                << "  spawn_ufo              Spawn one UFO near the camera\n"
                << "  spawn_asteroid [scale] Spawn one asteroid near the camera\n"
                << "  about                  Print program banner\n"
                << "  quit / exit            Close the window\n";
            return true;
        }

        if (cmd == "echo") {
            for (std::size_t i = 1; i < args.size(); ++i) {
                if (i > 1) {
                    out << ' ';
                }
                out << args[i];
            }
            return true;
        }

        if (cmd == "status") {
            out << "Mode: " << mode_name() << '\n'
                << "Score: " << score << '\n'
                << "Lives: " << lives << '\n'
                << "Game over: " << (game_over ? "yes" : "no") << '\n'
                << "Ship respawning: " << (ship_respawning ? "yes" : "no") << '\n'
                << "Ship position: " << format_vec3(ship.position) << '\n'
                << "Active UFOs: " << active_ufo_count() << '\n'
                << "Active asteroids: " << active_asteroid_count() << '\n'
                << "Projectiles: " << active_projectile_count() << '\n';
            return true;
        }

        if (cmd == "restart") {
            restart_game();
            log_game("Game restarted from console.");
            out << "Game restarted.";
            return true;
        }

        if (cmd == "intro") {
            clear_projectiles();
            clear_particles();
            clear_input_state();
            game_over = false;
            ship_respawning = false;
            reset_ship_to_origin();
            reset_intro_screen();
            log_game("Returned to intro screen from console.");
            out << "Intro screen active.";
            return true;
        }

        if (cmd == "play") {
            clear_input_state();
            game_over = false;
            ship_respawning = false;
            mode = GameMode::Playing;
            ship.visible = true;
            log_game("Play mode activated from console.");
            out << "Playing.";
            return true;
        }

        if (cmd == "gameover") {
            game_over = true;
            mode = GameMode::Playing;
            ship_respawning = false;
            ship.visible = false;
            ship.velocity = glm::vec3(0.0f);
            ship.current_speed = 0.0f;
            clear_projectiles();
            log_game("Game over forced from console.", SDL_Color{255, 120, 80, 255});
            out << "Game over forced.";
            return true;
        }

        if (cmd == "lives") {
            int value = 0;
            if (!parse_int_arg(args, 1, "lives", value, out)) {
                return true;
            }
            lives = std::clamp(value, 0, 99);
            if (lives > 0) {
                game_over = false;
                ship.visible = true;
            }
            log_game(std::format("Lives set to {} from console.", lives));
            out << "Lives set to " << lives << '.';
            return true;
        }

        if (cmd == "score") {
            int value = 0;
            if (!parse_int_arg(args, 1, "score", value, out)) {
                return true;
            }
            score = std::max(0, value);
            log_game(std::format("Score set to {} from console.", score));
            out << "Score set to " << score << '.';
            return true;
        }

        if (cmd == "killship") {
            if (game_over) {
                out << "Ship is already destroyed.";
                return true;
            }
            spawn_ship_explosion(ship.position);
            lose_life();
            log_game("Ship destroyed from console.", SDL_Color{255, 120, 80, 255});
            out << "Ship destroyed.";
            return true;
        }

        if (cmd == "clear_enemies") {
            const int cleared_ufos = active_ufo_count();
            const int cleared_asteroids = active_asteroid_count();
            for (auto &ufo : ufos) {
                ufo.active = false;
                ufo.respawn_timer = space::random_float(0.8f, 3.0f);
            }
            for (auto &asteroid : asteroids) {
                asteroid.active = false;
                asteroid.respawn_timer = space::random_float(0.8f, 3.0f);
            }
            log_game(std::format("Cleared {} UFO(s) and {} asteroid(s) from console.", cleared_ufos, cleared_asteroids));
            out << "Enemies cleared.";
            return true;
        }

        if (cmd == "spawn_ufo") {
            Ufo *ufo = find_inactive_ufo();
            if (ufo == nullptr) {
                out << "No free UFO slots.";
                return true;
            }
            respawn_ufo(*ufo);
            log_game(std::format("UFO spawned from console at {}.", format_vec3(ufo->position)));
            out << "UFO spawned.";
            return true;
        }

        if (cmd == "spawn_asteroid") {
            float scale = space::random_float(0.72f, 2.15f);
            if (args.size() > 1 && !parse_float_arg(args, 1, "scale", scale, out)) {
                return true;
            }
            Asteroid *asteroid = find_inactive_asteroid();
            if (asteroid == nullptr) {
                out << "No free asteroid slots.";
                return true;
            }
            respawn_asteroid(*asteroid);
            asteroid->scale = std::clamp(scale, 0.25f, 4.0f);
            asteroid->collision_radius = asteroid->scale * 1.25f;
            log_game(std::format("Asteroid spawned from console at {} scale {:.2f}.", format_vec3(asteroid->position), asteroid->scale));
            out << "Asteroid spawned.";
            return true;
        }

        if (cmd == "about") {
            out << "defender: MXVK side-scrolling starfield shooter.\n";
            return true;
        }

        if (cmd == "quit" || cmd == "exit") {
            log_game("Exit requested from console.");
            out << "Closing window...";
            exit();
            return true;
        }

        return false;
    }

    void DefenderWindow::log_game(const std::string &message, SDL_Color color) {
        if (!console_ready) {
            return;
        }
        console.printLine("[game] " + message, color);
    }

    [[nodiscard]] const char *DefenderWindow::mode_name() const {
        switch (mode) {
        case GameMode::Intro:
            return "intro";
        case GameMode::IntroFadeIn:
            return "intro_fade_in";
        case GameMode::Countdown:
            return "countdown";
        case GameMode::Playing:
            return "playing";
        }
        return "unknown";
    }

    [[nodiscard]] std::string DefenderWindow::format_vec3(const glm::vec3 &value) {
        return std::format("({:.1f}, {:.1f}, {:.1f})", value.x, value.y, value.z);
    }

    bool DefenderWindow::parse_int_arg(const std::vector<std::string> &args, std::size_t index, const char *name, int &value, std::ostream &out) const {
        if (args.size() <= index) {
            out << "Missing " << name << " value.";
            return false;
        }
        try {
            std::size_t parsed = 0;
            const int parsed_value = std::stoi(args[index], &parsed);
            if (parsed != args[index].size()) {
                out << "Invalid " << name << " value: " << args[index];
                return false;
            }
            value = parsed_value;
            return true;
        } catch (...) {
            out << "Invalid " << name << " value: " << args[index];
            return false;
        }
    }

    bool DefenderWindow::parse_float_arg(const std::vector<std::string> &args, std::size_t index, const char *name, float &value, std::ostream &out) const {
        if (args.size() <= index) {
            out << "Missing " << name << " value.";
            return false;
        }
        try {
            std::size_t parsed = 0;
            const float parsed_value = std::stof(args[index], &parsed);
            if (parsed != args[index].size()) {
                out << "Invalid " << name << " value: " << args[index];
                return false;
            }
            value = parsed_value;
            return true;
        } catch (...) {
            out << "Invalid " << name << " value: " << args[index];
            return false;
        }
    }

    [[nodiscard]] int DefenderWindow::active_ufo_count() const {
        int count = 0;
        for (const auto &ufo : ufos) {
            if (ufo.active) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] int DefenderWindow::active_asteroid_count() const {
        int count = 0;
        for (const auto &asteroid : asteroids) {
            if (asteroid.active) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] int DefenderWindow::active_projectile_count() const {
        int count = 0;
        for (const auto &projectile : projectiles) {
            if (projectile.active) {
                ++count;
            }
        }
        return count;
    }

    Ufo *DefenderWindow::find_inactive_ufo() {
        for (auto &ufo : ufos) {
            if (!ufo.active) {
                return &ufo;
            }
        }
        return nullptr;
    }

    Asteroid *DefenderWindow::find_inactive_asteroid() {
        for (auto &asteroid : asteroids) {
            if (!asteroid.active) {
                return &asteroid;
            }
        }
        return nullptr;
    }

    void DefenderWindow::clear_input_state() {
        reverse_pressed = false;
        propulsion_pressed = false;
        up_pressed = false;
        down_pressed = false;
        fire_pressed = false;
        roll_left_pressed = false;
        roll_right_pressed = false;
    }

} // namespace defender
