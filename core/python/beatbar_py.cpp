#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "samn/beatbar.h"

namespace py = pybind11;

PYBIND11_MODULE(beatbar_native, m) {
    py::class_<samn::GrayFrame>(m, "GrayFrame")
        .def(py::init<>())
        .def_readwrite("w", &samn::GrayFrame::w)
        .def_readwrite("h", &samn::GrayFrame::h)
        .def_readwrite("pix", &samn::GrayFrame::pix);

    py::class_<samn::BeatBar>(m, "BeatBar")
        .def(py::init<>())
        .def_readwrite("y0", &samn::BeatBar::y0)
        .def_readwrite("y1", &samn::BeatBar::y1)
        .def_readwrite("velocity", &samn::BeatBar::velocity)
        .def_readwrite("target_x", &samn::BeatBar::target_x)
        .def_readwrite("score", &samn::BeatBar::score);

    m.def("find_beat_bar", &samn::FindBeatBar);
    m.def("beat_times", &samn::BeatTimes);
}
