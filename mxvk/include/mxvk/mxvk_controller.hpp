/**
 * @file mxvk_controller.hpp
 * @brief SDL3 joystick and gamepad RAII wrappers.
 */
#ifndef _MXVK_CONTROLLER_H_
#define _MXVK_CONTROLLER_H_

#include <SDL3/SDL.h>

#include <optional>
#include <string>

namespace mxvk {

    /**
     * @class VK_Joystick
     * @brief RAII wrapper for a raw SDL_Joystick device.
     *
     * Opens an SDL joystick by index and provides axis, hat, and button
     * readback. The handle is automatically closed on destruction.
     */
    class VK_Joystick {
      public:
        /** @brief Default constructor — device not yet opened. */
        VK_Joystick();
        /** @brief Destructor — closes the joystick if open. */
        ~VK_Joystick() noexcept;

        VK_Joystick(const VK_Joystick &) = delete;
        VK_Joystick &operator=(const VK_Joystick &) = delete;
        VK_Joystick(VK_Joystick &&) = delete;
        VK_Joystick &operator=(VK_Joystick &&) = delete;

        /**
         * @brief Open the joystick at the given device index.
         * @param index SDL joystick index (0-based) within the current device list.
         * @return @c true on success.
         */
        bool open(int index);

        /**
         * @brief Return the joystick's name as reported by SDL.
         * @return Device name string.
         */
        std::string name() const;

        /** @brief Close the joystick handle if open. */
        void close();

        /**
         * @brief Return the underlying SDL_Joystick handle as an optional.
         * @return std::optional containing the handle, or std::nullopt.
         */
        [[nodiscard]] std::optional<SDL_Joystick *> handle() const;

        /**
         * @brief Unwrap the SDL_Joystick pointer, asserting it is open.
         * @return Raw SDL_Joystick pointer.
         */
        SDL_Joystick *unwrap() const;

        /**
         * @brief Return the device index this joystick was opened with.
         * @return Device index.
         */
        int joystickIndex() const;

        /**
         * @brief Return the number of connected joysticks.
         * @return SDL_NumJoysticks() result.
         */
        static int joysticks();

        /** @return Number of buttons on this joystick. */
        [[nodiscard]] int numButtons() const;
        /** @return Number of hats on this joystick. */
        [[nodiscard]] int numHats() const;
        /** @return Number of axes on this joystick. */
        [[nodiscard]] int numAxes() const;

        /**
         * @brief Test whether a button is currently pressed.
         * @param button Button index.
         * @return @c true if pressed.
         */
        [[nodiscard]] bool getButton(int button) const;

        /**
         * @brief Read the position of a POV hat.
         * @param hat Hat index.
         * @return SDL hat position bitmask.
         */
        [[nodiscard]] Uint8 getHat(int hat) const;

        /**
         * @brief Read the current axis value.
         * @param axis Axis index.
         * @return Axis value in the range [-32768, 32767].
         */
        [[nodiscard]] Sint16 getAxis(int axis) const;

      protected:
        SDL_Joystick *stick = nullptr;  ///< Underlying SDL joystick handle.
        int deviceIndex = -1;        ///< Open index in the current joystick list.
        SDL_JoystickID instanceId = 0; ///< Stable SDL joystick instance identifier.
    };

    /**
     * @class VK_Controller
     * @brief RAII wrapper for SDL_Gamepad (standard layout mapping).
     *
     * Provides button, hat, and axis queries using SDL's gamepad API,
     * which normalizes button layouts across different physical devices.
     */
    class VK_Controller {
      public:
        /** @brief Default constructor — device not yet opened. */
        VK_Controller();
        /** @brief Destructor — closes the controller if open. */
        ~VK_Controller() noexcept;

        VK_Controller(const VK_Controller &) = delete;
        VK_Controller &operator=(const VK_Controller &) = delete;
        VK_Controller(VK_Controller &&) = delete;
        VK_Controller &operator=(VK_Controller &&) = delete;

        /**
         * @brief Open a gamepad by device index.
         * @param index SDL gamepad index (0-based) within the current device list.
         * @return @c true on success.
         */
        bool open(int index);

        /**
         * @brief Handle a device-added/removed event to maintain connection state.
         * @param e SDL event to inspect.
         * @return @c true if the event was a controller connect/disconnect.
         */
        bool connectEvent(SDL_Event &e);

        /**
         * @brief Return the controller's name as reported by SDL.
         * @return Device name string.
         */
        std::string name() const;

        /** @brief Close the controller handle if open. */
        void close();

        /**
         * @brief Return the underlying SDL_Gamepad handle as an optional.
         * @return std::optional containing the handle, or std::nullopt.
         */
        [[nodiscard]] std::optional<SDL_Gamepad *> handle() const;

        /**
         * @brief Unwrap the SDL_Gamepad pointer, asserting it is open.
         * @return Raw SDL_Gamepad pointer.
         */
        [[nodiscard]] SDL_Gamepad *unwrap() const;

        /**
         * @brief Return the device index this controller was opened with.
         * @return Device index.
         */
        int controllerIndex() const;

        /**
         * @brief Return the number of connected joysticks/controllers.
         * @return SDL_NumJoysticks() result.
         */
        static int joysticks();

        /**
         * @brief Test whether a mapped button is currently pressed.
         * @param button SDL_GamepadButton constant.
         * @return @c true if pressed.
         */
        [[nodiscard]] bool getButton(SDL_GamepadButton button) const;

        /**
         * @brief Read the position of a POV hat.
         * @param hat Hat index.
         * @return SDL hat position bitmask.
         */
        [[nodiscard]] Uint8 getHat(int hat) const;

        /**
         * @brief Read the current axis value.
         * @param axis SDL_GamepadAxis constant.
         * @return Axis value in the range [-32768, 32767].
         */
        [[nodiscard]] Sint16 getAxis(SDL_GamepadAxis axis) const;

        /**
         * @brief Check whether the controller is currently usable.
         * @return @c true if the controller is open and has a valid index.
         */
        [[nodiscard]] bool active() const;

      protected:
        bool openByInstanceId(SDL_JoystickID instanceId, int index_hint = -1);

        SDL_Gamepad *stick = nullptr; ///< Underlying SDL gamepad handle.
        int deviceIndex = -1;        ///< Open index in the current gamepad list.
        SDL_JoystickID instanceId = 0; ///< Stable SDL gamepad instance identifier.
    };

    // Backward-compatible aliases.
    using Controller = VK_Controller;
    using Joystick = VK_Joystick;

} // namespace mxvk

namespace mx {
    using VK_Controller = mxvk::VK_Controller;
    using VK_Joystick = mxvk::VK_Joystick;
    using Controller = mxvk::VK_Controller;
    using Joystick = mxvk::VK_Joystick;
} // namespace mx

#endif
