#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "samn/funscript.h"

namespace py = pybind11;

// Part of the samn_native module (samn_native_py.cpp).
void BindFunscript(py::module_& m) {
    py::class_<samn::Script>(m, "Script")
        .def(py::init<>())
        .def_readwrite("actions", &samn::Script::actions)
        .def_readwrite("inverted", &samn::Script::inverted)
        .def_readwrite("dropped", &samn::Script::dropped)
        .def_readwrite("duration_meta", &samn::Script::duration_meta);

    m.def("parse", &samn::Parse);
    m.def("write", &samn::Write);
    m.def("shift_actions", &samn::ShiftActions, "Shift actions by deltaMs; returns (actions, dropped)");
}
