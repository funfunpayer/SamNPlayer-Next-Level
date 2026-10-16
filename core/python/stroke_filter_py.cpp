#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "samn/stroke_filter.h"

namespace py = pybind11;

// Part of the samn_native module (samn_native_py.cpp).
void BindStrokeFilter(py::module_& m) {
    py::class_<samn::StrokeFilter>(m, "StrokeFilter")
        .def(py::init<>())
        .def_readwrite("smooth", &samn::StrokeFilter::smooth)
        .def_readwrite("stepMs", &samn::StrokeFilter::stepMs)
        .def_readwrite("maxSpeed", &samn::StrokeFilter::maxSpeed)
        .def_readwrite("idleFillMs", &samn::StrokeFilter::idleFillMs);

    m.def("apply_stroke_filter", &samn::ApplyStrokeFilter);
    m.def("active", &samn::Active);
}
