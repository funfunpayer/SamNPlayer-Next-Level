// One Python module for the whole core: samn_native, with a submodule per
// part. samn::Action is registered once here (four modules each registering
// it could not be imported into one process).
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "samn/funscript.h"

namespace py = pybind11;

void BindBeatbar(py::module_& m);
void BindStrokeFilter(py::module_& m);
void BindBeatStrokes(py::module_& m);
void BindFunscript(py::module_& m);

PYBIND11_MODULE(samn_native, m) {
    m.doc() = "SamNPlayer 2 core (C++) for AI tools and parity tests";
    py::class_<samn::Action>(m, "Action")
        .def(py::init<>())
        .def(py::init([](int64_t at, int pos) { return samn::Action{at, pos}; }))
        .def_readwrite("at", &samn::Action::at)
        .def_readwrite("pos", &samn::Action::pos)
        .def("__repr__", [](const samn::Action& a) {
            return "Action(" + std::to_string(a.at) + ", " + std::to_string(a.pos) + ")";
        });
    const auto sub = [&m](const char* name, void (*bind)(py::module_&)) {
        py::module_ s = m.def_submodule(name);
        s.attr("Action") = m.attr("Action");
        bind(s);
    };
    sub("beatbar", BindBeatbar);
    sub("stroke_filter", BindStrokeFilter);
    sub("beat_strokes", BindBeatStrokes);
    sub("funscript", BindFunscript);
}
