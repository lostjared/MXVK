#ifndef _MXNET_EXCEPT
#define _MXNET_EXCEPT

#include <iostream>
#include <string>

namespace mxnetwork {
    class Exception {
      public:
        Exception(const std::string &s) : txt{s} {}
        std::string text() const;

      protected:
        std::string txt;
    };
} // namespace mxnetwork

#endif
