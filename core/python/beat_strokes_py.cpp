#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "samn/beat_strokes.h"

namespace py = pybind11;

PYBIND11_MODULE(beat_strokes_native, m) {
    py::class_<samn::Action>(m, "Action")
        .def(py::init<>())
        .def(py::init([](int64_t at, int pos) { return samn::Action{at, pos}; }))
        .def_readwrite("at", &samn::Action::at)
        .def_readwrite("pos", &samn::Action::pos);

    py::class_<samn::BeatStrokeOptions>(m, "BeatStrokeOptions")
        .def(py::init<>())
        .def_readwrite("top", &samn::BeatStrokeOptions::top)
        .def_readwrite("bottom", &samn::BeatStrokeOptions::bottom)
        .def_readwrite("maxSpeed", &samn::BeatStrokeOptions::maxSpeed)
        .def_readwrite("holdAfterMs", &samn::BeatStrokeOptions::holdAfterMs);

    m.def("beat_strokes", &samn::BeatStrokes);
}
