/**
 * @file mxvk_controller.cpp
 * @brief Implementation of mxvk::VK_Joystick and mxvk::VK_Controller SDL3 wrappers.
 */

#include "mxvk/mxvk_controller.hpp"

#include "mxvk/mxvk_exception.hpp"

#include <SDL3/SDL_stdinc.h>

namespace mxvk {

    VK_Joystick::VK_Joystick() = default;

    VK_Joystick::~VK_Joystick() noexcept {
        close();
    }

    int VK_Joystick::joysticks() {
        int count = 0;
        SDL_JoystickID *ids = SDL_GetJoysticks(&count);
        if (ids != nullptr) {
            SDL_free(ids);
        }
        return count;
    }

    bool VK_Joystick::open(const int index) {
        close();

        int count = 0;
        SDL_JoystickID *ids = SDL_GetJoysticks(&count);
        if (ids == nullptr || index < 0 || index >= count) {
            if (ids != nullptr) {
                SDL_free(ids);
            }
            return false;
        }

        const SDL_JoystickID instance_id = ids[index];
        SDL_free(ids);

        stick_ = SDL_OpenJoystick(instance_id);
        if (stick_ == nullptr) {
            return false;
        }

        index_ = index;
        instance_id_ = instance_id;
        SDL_SetJoystickEventsEnabled(true);
        return true;
    }

    std::string VK_Joystick::name() const {
        if (stick_ != nullptr) {
            const char *device_name = SDL_GetJoystickName(stick_);
            if (device_name != nullptr) {
                return device_name;
            }
        }
        return "Joystick Not Opened.";
    }

    void VK_Joystick::close() {
        if (stick_ != nullptr) {
            SDL_CloseJoystick(stick_);
        }

        stick_ = nullptr;
        index_ = -1;
        instance_id_ = 0;
    }

    std::optional<SDL_Joystick *> VK_Joystick::handle() const {
        if (stick_ != nullptr) {
            return stick_;
        }
        return std::nullopt;
    }

    SDL_Joystick *VK_Joystick::unwrap() const {
        if (stick_ != nullptr) {
            return stick_;
        }
        throw mxvk::Exception("Invalid joystick handle");
    }

    int VK_Joystick::joystickIndex() const {
        return index_;
    }

    bool VK_Joystick::getButton(const int button) const {
        if (stick_ == nullptr) {
            return false;
        }
        return SDL_GetJoystickButton(stick_, button);
    }

    Uint8 VK_Joystick::getHat(const int hat) const {
        if (stick_ == nullptr) {
            return 0;
        }
        return SDL_GetJoystickHat(stick_, hat);
    }

    Sint16 VK_Joystick::getAxis(const int axis) const {
        if (stick_ == nullptr) {
            return 0;
        }
        return SDL_GetJoystickAxis(stick_, axis);
    }

    int VK_Joystick::numButtons() const {
        if (stick_ == nullptr) {
            return 0;
        }
        const int count = SDL_GetNumJoystickButtons(stick_);
        return count < 0 ? 0 : count;
    }

    int VK_Joystick::numHats() const {
        if (stick_ == nullptr) {
            return 0;
        }
        const int count = SDL_GetNumJoystickHats(stick_);
        return count < 0 ? 0 : count;
    }

    int VK_Joystick::numAxes() const {
        if (stick_ == nullptr) {
            return 0;
        }
        const int count = SDL_GetNumJoystickAxes(stick_);
        return count < 0 ? 0 : count;
    }

    VK_Controller::VK_Controller() = default;

    VK_Controller::~VK_Controller() noexcept {
        close();
    }

    int VK_Controller::joysticks() {
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        if (ids != nullptr) {
            SDL_free(ids);
        }
        return count;
    }

    bool VK_Controller::openByInstanceId(const SDL_JoystickID instance_id, const int index_hint) {
        close();

        stick_ = SDL_OpenGamepad(instance_id);
        if (stick_ == nullptr) {
            return false;
        }

        index_ = index_hint;
        instance_id_ = instance_id;
        SDL_SetGamepadEventsEnabled(true);
        return true;
    }

    bool VK_Controller::open(const int index) {
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        if (ids == nullptr || index < 0 || index >= count) {
            if (ids != nullptr) {
                SDL_free(ids);
            }
            return false;
        }

        const SDL_JoystickID instance_id = ids[index];
        SDL_free(ids);
        return openByInstanceId(instance_id, index);
    }

    std::string VK_Controller::name() const {
        if (stick_ != nullptr) {
            const char *device_name = SDL_GetGamepadName(stick_);
            if (device_name != nullptr) {
                return device_name;
            }
        }
        return "Controller Not Opened.";
    }

    void VK_Controller::close() {
        if (stick_ != nullptr) {
            SDL_CloseGamepad(stick_);
        }

        stick_ = nullptr;
        index_ = -1;
        instance_id_ = 0;
    }

    std::optional<SDL_Gamepad *> VK_Controller::handle() const {
        if (stick_ != nullptr) {
            return stick_;
        }
        return std::nullopt;
    }

    SDL_Gamepad *VK_Controller::unwrap() const {
        if (stick_ != nullptr) {
            return stick_;
        }
        throw mxvk::Exception("Invalid controller handle");
    }

    int VK_Controller::controllerIndex() const {
        return index_;
    }

    bool VK_Controller::getButton(const SDL_GamepadButton button) const {
        if (stick_ == nullptr) {
            return false;
        }
        return SDL_GetGamepadButton(stick_, button);
    }

    Uint8 VK_Controller::getHat(const int hat) const {
        if (stick_ == nullptr) {
            return 0;
        }

        SDL_Joystick *joystick = SDL_GetGamepadJoystick(stick_);
        if (joystick == nullptr) {
            return 0;
        }
        return SDL_GetJoystickHat(joystick, hat);
    }

    Sint16 VK_Controller::getAxis(const SDL_GamepadAxis axis) const {
        if (stick_ == nullptr) {
            return 0;
        }
        return SDL_GetGamepadAxis(stick_, axis);
    }

    bool VK_Controller::active() const {
        return stick_ != nullptr && SDL_GamepadConnected(stick_);
    }

    bool VK_Controller::connectEvent(SDL_Event &e) {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
            return openByInstanceId(e.gdevice.which);
        }

        if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
            if (stick_ != nullptr && instance_id_ == e.gdevice.which) {
                close();
            }
            return true;
        }

        return false;
    }

} // namespace mxvk