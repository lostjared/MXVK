#ifndef _MXVK_EXCEPTION_
#define _MXVK_EXCEPTION_

#include <iostream>
#include <string>

namespace mxvk {
    class Exception {
      public:
        Exception(const std::string &text) : txt{text} {}
        std::string text() const { return txt; }

      private:
        std::string txt;
    };
} // namespace mxvk

#endif