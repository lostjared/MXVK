#ifndef _MXVK_EXCEPTION_
#define _MXVK_EXCEPTION_

#include<string>
#include<iostream>

namespace mxvk {
    class Exception {
    public:
        Exception(const std::string &text) : txt{text} {}
        std::string text() const { return txt; }
    private:
        std::string txt;
    };
}

#endif