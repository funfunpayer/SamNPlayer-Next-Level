# Third-party notices

SamNPlayer is freeware and not open source (see `EULA`). It includes or uses
the following third-party components. Each is licensed under its own terms,
reproduced or linked below. Nothing in the SamNPlayer EULA limits your rights
under these licences.

## Shipped in the download packages

| Component | Used for | Licence | Source / licence text |
|---|---|---|---|
| **FFmpeg** (`ffmpeg`, `ffprobe` beside the app; a separate program SamNPlayer runs) | video and audio decoding, clip cutting | **GPL v3** build (Windows: BtbN "gpl" build; Linux: the distribution's package) | https://ffmpeg.org/legal.html · source: https://ffmpeg.org/download.html and https://github.com/BtbN/FFmpeg-Builds · the Windows package also ships FFmpeg's own `LICENSE.txt` |
| **OpenCV** 4.x (Windows: DLLs beside the app) | tracking (CSRT), optical flow | Apache License 2.0 | https://opencv.org/license/ · https://github.com/opencv/opencv |
| OpenCV's bundled libraries on Windows (MinGW-w64 runtime, libstdc++/libgcc, zlib, libpng, libjpeg-turbo, libtiff, libwebp, OpenJPEG and similar) | image I/O for OpenCV | permissive licences (GCC Runtime Library Exception, zlib, libpng, BSD/IJG, MIT-style) | https://packages.msys2.org (package pages list each licence) |
| **Wails v2** | desktop app framework | MIT | https://github.com/wailsapp/wails |
| leaanthony/go-ansi-parser, slicer, u | Wails dependencies | MIT | https://github.com/leaanthony |
| rivo/uniseg | Wails dependency | MIT | https://github.com/rivo/uniseg |
| gorilla/websocket | Intiface connection | BSD 2-Clause | https://github.com/gorilla/websocket |
| godbus/dbus | Linux desktop integration (Bluetooth) | BSD 2-Clause | https://github.com/godbus/dbus |
| pkg/errors | dependency | BSD 2-Clause | https://github.com/pkg/errors |
| tinygo.org/x/bluetooth | Bluetooth LE devices | BSD 3-Clause | https://github.com/tinygo-org/bluetooth |
| Go standard library | runtime | BSD 3-Clause | https://go.dev/LICENSE |
| Sora and Figtree fonts | user interface | SIL Open Font License 1.1 | https://openfontlicense.org · https://github.com/sora-xor/sora-font · https://github.com/erikdkennedy/figtree |

The SamNPlayer program only *starts* FFmpeg as a separate program; it does
not link it. Where FFmpeg's licence requires the corresponding source code,
it is available from the links above for the version shipped; on request we
also provide the exact FFmpeg source archive for a SamNPlayer release.

## Not shipped — installed by you on request

These are downloaded only when you choose an optional feature (for example
"Install video tools", "Install dependencies" in AI Train, or the AI setup).
They come from their own publishers under their own licences:

| Component | Licence |
|---|---|
| Python | PSF License |
| PyTorch, torchvision | BSD-style |
| Ultralytics YOLO (optional AI Train path) | **AGPL-3.0** — used only on your computer; SamNPlayer does not ship YOLO weights |
| RF-DETR (contact detector training) | Apache 2.0 |
| NudeNet | MIT (code); see its model licence |
| onnxruntime | MIT |
| opencv-python, numpy and further pip packages | their own licences (mostly BSD / MIT / Apache 2.0) |
| Microsoft Edge WebView2 Runtime (Windows) | Microsoft software licence; part of Windows / installed by Microsoft |
| Intiface Central / Buttplug | their own licences; separate program |
