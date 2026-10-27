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
| FFmpeg n7.0.2 built for the Windows package (Qt's configure options plus `--enable-libdav1d`, `ci/windows/build_ffmpeg_dav1d.sh`; replaces Qt's FFmpeg DLLs, licence beside it as `FFMPEG_LICENSE.md`) | video playback, AV1 without a GPU decoder | LGPL-2.1+ |
| dav1d (vcpkg) | AV1 decoding inside that FFmpeg | BSD-2-Clause |
| nlohmann/json 3.11.3 | core | MIT |
| ONNX Runtime 1.22.0 (official NuGet build, `Microsoft.ML.OnnxRuntime`; licence and notices ship beside it in the package) | `vision/` detectors (D-155 step 4) | MIT |
| zlib (the one OpenCV brings) | unpacking the models fetched with one click | Zlib |

## Fetched by the user, not shipped

| Component | How | Licence |
|---|---|---|
| uv 0.13.4 (Astral) | *AI Train → Train → Set up* fetches the PyPI wheel (hash pinned); it installs Python 3.12, PyTorch and rfdetr into the AI folder | MIT / Apache-2.0 |
| ffmpeg 7 (from imageio-ffmpeg 0.6.0) | *AI Train → Benchmark → Cut a clip → Fetch ffmpeg* fetches the PyPI wheel and checks the wheel and the program against pinned SHA-256 hashes; kept in the config folder's `tools/`, run as a separate program | ffmpeg: GPL-3.0 build (with libx264); imageio-ffmpeg: BSD-2-Clause |
| rfdetr 1.11.2 (Roboflow) and its RF-DETR weights | installed by uv into that environment; trains the student | Apache-2.0 |
| NudeNet 3.4.2 `320n.onnx` (AI Train teacher, D-155) | *AI Train → Teachers → Download NudeNet* fetches the PyPI wheel and checks the wheel and the model against pinned SHA-256 hashes | the wheel carries an AGPL-3.0 licence file (PyPI lists MIT); the weights were trained with YOLOv8, so their licence is unclear and they are not bundled |
| pybind11 | Python bindings of the core | BSD-3-Clause |
