#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "stroke_filter.h"

namespace py = pybind11;

PYBIND11_MODULE(stroke_filter_native, m) {
    py::class_<samn::Action>(m, "Action")
        .def(py::init<>())
        .def(py::init([](int64_t at, int pos) { return samn::Action{at, pos}; }))
        .def_readwrite("at", &samn::Action::at)
        .def_readwrite("pos", &samn::Action::pos);

    py::class_<samn::StrokeFilter>(m, "StrokeFilter")
        .def(py::init<>())
        .def_readwrite("smooth", &samn::StrokeFilter::smooth)
        .def_readwrite("stepMs", &samn::StrokeFilter::stepMs)
        .def_readwrite("maxSpeed", &samn::StrokeFilter::maxSpeed)
        .def_readwrite("idleFillMs", &samn::StrokeFilter::idleFillMs);

    m.def("apply_stroke_filter", &samn::ApplyStrokeFilter);
    m.def("active", &samn::Active);
}
