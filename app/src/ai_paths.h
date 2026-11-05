#pragma once

// Where AI Train keeps its files. A header of its own so that the health check
// and the trainer both use it without including each other.

#include <QString>

#include "qt_paths.h"
#include "samn/settings.h"

namespace samn::ui {

// config_dir()/ai: the app's own Python, caches, datasets and runs.
inline QString AiDir() { return FromFs(samn::config_dir() / "ai"); }

}  // namespace samn::ui
