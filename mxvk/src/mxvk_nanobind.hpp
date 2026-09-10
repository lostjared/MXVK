#ifndef MXVK_NANOBIND_HPP
#define MXVK_NANOBIND_HPP

#include <nanobind/nanobind.h>

namespace mxvk {
    /** Register MXVK's Python-facing types and functions on a nanobind module. */
    void bind_nanobind_module(nanobind::module_ &module);
} // namespace mxvk

#endif
