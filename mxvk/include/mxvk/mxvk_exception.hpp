#ifndef MXVK_EXCEPTION_HPP
#define MXVK_EXCEPTION_HPP

#include <stdexcept>
#include <string>

namespace mxvk {
    class Exception : public std::runtime_error {
      public:
        explicit Exception(const std::string &text) : std::runtime_error(text) {}
        std::string text() const { return what(); }
    };
} // namespace mxvk

#endif
