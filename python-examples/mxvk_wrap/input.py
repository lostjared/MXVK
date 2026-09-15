## @file input.py
## @brief Joystick and game-controller convenience wrappers.

from ._native import mxvk


class Joystick:
    ## @brief An opened SDL joystick.

    def __init__(self, index: int = 0) -> None:
        ## @brief Open joystick @p index.
        self.native = mxvk.Joystick()
        self.native.open(index)

    @classmethod
    def count(cls) -> int:
        ## @brief Return the number of connected joysticks.
        return mxvk.Joystick.count()

    def button(self, index: int) -> bool:
        ## @brief Return whether button @p index is pressed.
        return self.native.button(index)

    def axis(self, index: int) -> int:
        ## @brief Return the signed raw value for axis @p index.
        return self.native.axis(index)

    def close(self) -> None:
        ## @brief Close the joystick handle.
        if self.native is not None:
            self.native.close()
            self.native = None


class Controller:
    ## @brief An opened SDL game controller.

    def __init__(self, index: int = 0) -> None:
        ## @brief Open controller @p index.
        self.native = mxvk.Controller()
        self.native.open(index)

    @classmethod
    def count(cls) -> int:
        ## @brief Return the number of discoverable controllers.
        return mxvk.Controller.count()

    def button(self, button: int) -> bool:
        ## @brief Return whether SDL gamepad @p button is pressed.
        return self.native.button(button)

    def axis(self, axis: int) -> int:
        ## @brief Return the signed raw value for gamepad @p axis.
        return self.native.axis(axis)

    def close(self) -> None:
        ## @brief Close the controller handle.
        if self.native is not None:
            self.native.close()
            self.native = None
