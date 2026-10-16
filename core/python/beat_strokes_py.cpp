#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "samn/beat_strokes.h"

namespace py = pybind11;

// Part of the samn_native module (samn_native_py.cpp).
void BindBeatStrokes(py::module_& m) {
    py::class_<samn::BeatStrokeOptions>(m, "BeatStrokeOptions")
        .def(py::init<>())
        .def_readwrite("top", &samn::BeatStrokeOptions::top)
        .def_readwrite("bottom", &samn::BeatStrokeOptions::bottom)
        .def_readwrite("maxSpeed", &samn::BeatStrokeOptions::maxSpeed)
        .def_readwrite("holdAfterMs", &samn::BeatStrokeOptions::holdAfterMs);

    m.def("beat_strokes", &samn::BeatStrokes);
}
