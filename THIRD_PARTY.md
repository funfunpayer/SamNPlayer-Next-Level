# Third-party code and components

Every piece of code copied or adapted from another project is listed here with its source, version and licence; the file itself keeps its original copyright and licence header (`LICENSING.md` rule 2). Libraries we only link to are listed with their licence.

## Copied or adapted code

| Our file | Source (project, file, version / commit) | Licence |
|---|---|---|
| `third_party/doctest/doctest.h` | doctest 2.4.11 | MIT |

## Linked libraries

| Library | Use | Licence |
|---|---|---|
| Qt 6 (Core, Gui, Quick, Qml, Multimedia, Svg, Network, WebSockets, SerialPort, Bluetooth) | app | LGPL-3.0 / GPL-3.0 |
| OpenCV 4 (core, imgproc, videoio, video, tracking, dnn) | `vision/` | Apache-2.0 |
| FFmpeg (through Qt Multimedia and OpenCV videoio) | decoding | LGPL-2.1+ (GPL builds where noted) |
| nlohmann/json 3.11.3 | core | MIT |
| ONNX Runtime 1.22.0 (official NuGet build, `Microsoft.ML.OnnxRuntime`; licence and notices ship beside it in the package) | `vision/` detectors (D-155 step 4) | MIT |
| pybind11 | Python bindings of the core | BSD-3-Clause |
