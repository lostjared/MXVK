#ifndef _MXNET_EXCEPT
#define _MXNET_EXCEPT

#include <iostream>
#include <string>

namespace mxnetwork {
    /**
     * @brief Lightweight exception wrapper for MXNetwork failures.
     */
    class Exception {
      public:
        /**
         * @brief Construct an exception with a message.
         * @param s Error text.
         */
        Exception(const std::string &s) : txt{s} {}
        /**
         * @brief Return the stored error text.
         * @return Error text.
         */
        std::string text() const;

      protected:
        std::string txt;
    };
} // namespace mxnetwork

#endif
