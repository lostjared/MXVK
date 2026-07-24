#pragma once

#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_console.hpp"

#include <SDL3/SDL.h>
#include <iosfwd>
#include <vector>

namespace mxvk {

    class VK_IOWindow : public VK_Window {
      public:
        VK_IOWindow(const std::string &path, const std::string &title, const int width, const int height, const bool fullscreen, const bool enableVsync = false);
        void event(SDL_Event &e) override;
        void print(const std::string &text, SDL_Color col = {255, 255, 255, 255});
        void proc() override;
        virtual void console_proc() = 0;
        virtual void console_event(SDL_Event &e) = 0;
        bool visible() const { return console.isVisible(); }

      protected:
        /**
         * @brief Handle app-specific console commands.
         *
         * Called after shared commands such as `echo`/`quit`/`about`.
         *
         * @param args Tokenized command arguments.
         * @param out Console output stream.
         * @return true when the command is handled.
         */
        virtual bool handleConsoleCommand(const std::vector<std::string> &args, std::ostream &out);

        /**
         * @brief Append app-specific help lines to the console help output.
         *
         * @param out Console output stream.
         */
        virtual void appendConsoleHelp(std::ostream &out) const;

      private:
        VK_Console console;
    };
} // namespace mxvk
