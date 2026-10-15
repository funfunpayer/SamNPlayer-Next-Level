"""Play mode: video, script curve with playhead, heatmap, offset, loop.

Video first (as in SamNPlayer 0.7): "Open…" takes a video or a script. A
video finds its script (beside it, in the script folders, tolerant names);
a script finds its video beside it.
"""

from __future__ import annotations

import os
from pathlib import Path

from PySide6.QtCore import Qt, QUrl
from PySide6.QtGui import QColor, QKeySequence, QPainter, QShortcut
from PySide6.QtMultimedia import QAudioOutput, QMediaPlayer
from PySide6.QtMultimediaWidgets import QVideoWidget
from PySide6.QtWidgets import (
    QComboBox,
    QFileDialog,
    QFrame,
    QHBoxLayout,
    QLabel,
    QMessageBox,
    QPushButton,
    QSlider,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from ..formats import funscript, marks
from ..formats.mapper import default_map_options, to_intensity_curve
from ..library.script_finder import (
    VIDEO_EXTS,
    ScriptVariant,
    find_script_for_video,
    split_folders,
)
from ..player.session import LoopRange, Session
from ..settings import Settings
from .curve import CurveWidget

SCRIPT_FOLDERS_KEY = "library.script_folders"  # newline list, as in 0.7
SEEK_MS, FINE_SEEK_MS, OFFSET_STEP_MS = 5000, 1000, 50


def heatmap(actions: list[tuple[int, int]], buckets: int) -> list[tuple[int, float]]:
    """(time, intensity 0-1) per bucket; GetHeatmap in 0.7: max(suction, vibration)."""
    if len(actions) < 2:
        return []
    duration = actions[-1][0]
    if duration <= 0:
        return []
    opts = default_map_options()
    opts.tick_ms = max(10, duration // max(10, buckets))
    frames = to_intensity_curve(actions, opts) or []
    return [(f.at, max(f.suction, f.vibration)) for f in frames]


class HeatmapStrip(QWidget):
    def __init__(self) -> None:
        super().__init__()
        self.points: list[tuple[int, float]] = []
        self.setFixedHeight(14)

    def set_points(self, points: list[tuple[int, float]]) -> None:
        self.points = points
        self.update()

    def paintEvent(self, _e) -> None:  # noqa: N802
        p = QPainter(self)
        p.fillRect(self.rect(), QColor("#16181d"))
        if not self.points:
            return
        w, n = self.width(), len(self.points)
        for i, (_, v) in enumerate(self.points):
            x0, x1 = i * w // n, (i + 1) * w // n
            c = QColor.fromHsvF(max(0.0, 0.66 * (1 - v)), 0.85, 0.35 + 0.6 * v)
            p.fillRect(x0, 0, max(1, x1 - x0), self.height(), c)


class PlayPage(QWidget):
    def __init__(self, settings: Settings | None = None) -> None:
        super().__init__()
        self.settings = settings or Settings()
        self.session = Session(self.settings)
        self.script: funscript.Funscript | None = None

        self.player = QMediaPlayer(self)
        self.audio = QAudioOutput(self)
        self.player.setAudioOutput(self.audio)
        self.video = QVideoWidget()
        self.player.setVideoOutput(self.video)
        self.video.hide()

        self.open_button = QPushButton("Open…")
        self.play_button = QPushButton("Play")
        self.info = QLabel("Open a video or a funscript.")
        self.offset = QSpinBox()
        self.offset.setRange(-10_000, 10_000)
        self.offset.setSingleStep(OFFSET_STEP_MS)
        self.offset.setSuffix(" ms")
        self.offset.setToolTip("Script offset (saved per script)")
        self.loop_start = QPushButton("Loop from here")
        self.loop_end = QPushButton("Loop to here")
        self.loop_clear = QPushButton("No loop")
        self.variant = QComboBox()
        self.variant.setToolTip("This video has several scripts — choose one")
        self.variant.hide()
        self._variants: list[ScriptVariant] = []
        self.chapter = QComboBox()
        self.chapter.setToolTip("Jump to a chapter")
        self.chapter.hide()
        self._chapters: list[marks.Chapter] = []
        self.noscript = QFrame()
        self.noscript.setFrameShape(QFrame.Shape.StyledPanel)
        self.noscript_create = QPushButton("Create script")
        self.noscript_create.setEnabled(False)
        self.noscript_create.setToolTip("The Generator comes in SamNPlayer 2 phase P3")
        self.noscript_choose = QPushButton("Choose script…")
        self.noscript_folder = QPushButton("Add a script folder…")
        nl = QHBoxLayout(self.noscript)
        nl.addWidget(QLabel("No script for this video yet."))
        for wdg in (self.noscript_create, self.noscript_choose, self.noscript_folder):
            nl.addWidget(wdg)
        nl.addStretch(1)
        self.noscript.hide()
        self.slider = QSlider(Qt.Orientation.Horizontal)
        self.curve = CurveWidget()
        self.heat = HeatmapStrip()

        top = QHBoxLayout()
        for wdg in (self.open_button, self.play_button):
            top.addWidget(wdg)
        top.addWidget(self.info, 1)
        top.addWidget(self.variant)
        top.addWidget(self.chapter)
        top.addWidget(QLabel("Offset"))
        top.addWidget(self.offset)
        loops = QHBoxLayout()
        for wdg in (self.loop_start, self.loop_end, self.loop_clear):
            loops.addWidget(wdg)
        loops.addStretch(1)
        lay = QVBoxLayout(self)
        lay.addLayout(top)
        lay.addWidget(self.noscript)
        lay.addWidget(self.video, 3)
        lay.addWidget(self.slider)
        lay.addWidget(self.heat)
        lay.addWidget(self.curve, 1)
        lay.addLayout(loops)

        self.open_button.clicked.connect(self.pick)
        self.play_button.clicked.connect(self.toggle_play)
        self.offset.valueChanged.connect(self._offset_changed)
        self.slider.sliderMoved.connect(self.player.setPosition)
        self.player.positionChanged.connect(self.on_position)
        self.player.durationChanged.connect(lambda d: self.slider.setRange(0, int(d)))
        self.player.playbackStateChanged.connect(self._state_changed)
        self.loop_start.clicked.connect(lambda: self._set_loop(start=self.player.position()))
        self.loop_end.clicked.connect(lambda: self._set_loop(end=self.player.position()))
        self.loop_clear.clicked.connect(self._clear_loop)
        self.variant.activated.connect(self._variant_chosen)
        self.chapter.activated.connect(self._chapter_chosen)
        self.noscript_choose.clicked.connect(self._choose_for_video)
        self.noscript_folder.clicked.connect(self._add_folder)
        self.setAcceptDrops(True)

        for key, fn in (
            ("Space", self.toggle_play),
            ("Left", lambda: self.seek_by(-SEEK_MS)),
            ("Right", lambda: self.seek_by(SEEK_MS)),
            ("Shift+Left", lambda: self.seek_by(-FINE_SEEK_MS)),
            ("Shift+Right", lambda: self.seek_by(FINE_SEEK_MS)),
            ("+", lambda: self.offset.setValue(self.offset.value() + OFFSET_STEP_MS)),
            ("-", lambda: self.offset.setValue(self.offset.value() - OFFSET_STEP_MS)),
        ):
            QShortcut(QKeySequence(key), self, activated=fn)

    # ---- opening -------------------------------------------------------
    def pick(self) -> None:
        exts = " ".join("*" + e for e in VIDEO_EXTS)
        path, _ = QFileDialog.getOpenFileName(
            self, "Open video or script", "",
            f"Video or script ({exts} *.funscript);;All files (*)",
        )
        if path:
            self.open_path(path)

    def open_path(self, path: str | Path) -> bool:
        path = str(path)
        if os.path.splitext(path)[1].lower() in VIDEO_EXTS:
            return self.open_video(path)
        return self.open_script(path)

    def open_video(self, video: str) -> bool:
        folders = split_folders(self.settings.get(SCRIPT_FOLDERS_KEY, "") or "")
        match = find_script_for_video(video, folders)
        if match.found and match.script_path:
            ok = self.open_script(match.script_path, video=video)
            self._show_variants(match.variants, match.script_path)
            return ok
        self._set_video(video)
        self.script = None
        self.session.video_path = video
        self.curve.set_actions([])
        self.heat.set_points([])
        self._show_variants([], "")
        self.noscript.show()
        self.info.setText(f"{Path(video).name}: no script found for this video.")
        return True

    def open_pair(self, video: str, script: str) -> bool:
        """A script with a given video (dropped together or chosen)."""
        return self.open_script(script, video=video)

    def _show_variants(self, variants: list[ScriptVariant], current: str) -> None:
        self._variants = list(variants)
        self.variant.clear()
        if len(variants) < 2:
            self.variant.hide()
            return
        for v in variants:
            name = v.label.capitalize() if v.label else "Main script"
            if not v.beside:
                name += " (script folder)"
            self.variant.addItem(name)
        idx = next((i for i, v in enumerate(variants) if v.path == current), 0)
        self.variant.setCurrentIndex(idx)
        self.variant.show()

    def _variant_chosen(self, idx: int) -> None:
        if 0 <= idx < len(self._variants):
            self.open_script(self._variants[idx].path, video=self.session.video_path)

    def _choose_for_video(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "Choose script", "", "Funscript (*.funscript)")
        if path and self.session.video_path:
            self.open_pair(self.session.video_path, path)

    def _add_folder(self) -> None:
        d = QFileDialog.getExistingDirectory(self, "Add a script folder")
        if d:
            self.add_script_folder(d)

    def add_script_folder(self, folder: str) -> None:
        """Add a folder to the search and look for the open video's script there."""
        folders = split_folders(self.settings.get(SCRIPT_FOLDERS_KEY, "") or "")
        if folder not in folders:
            folders.append(folder)
            self.settings.set(SCRIPT_FOLDERS_KEY, "\n".join(folders))
        if self.script is None and self.session.video_path:
            video = self.session.video_path
            match = find_script_for_video(video, folders)
            if match.found and match.script_path:
                self.open_video(video)
            else:
                self.info.setText(f"{Path(video).name}: no script in that folder either.")

    # ---- drag and drop -------------------------------------------------
    def dragEnterEvent(self, e) -> None:  # noqa: N802
        if e.mimeData().hasUrls():
            e.acceptProposedAction()

    def dropEvent(self, e) -> None:  # noqa: N802
        paths = [u.toLocalFile() for u in e.mimeData().urls() if u.isLocalFile()]
        self.open_dropped(paths)

    def open_dropped(self, paths: list[str]) -> bool:
        videos = [p for p in paths if os.path.splitext(p)[1].lower() in VIDEO_EXTS]
        scripts = [p for p in paths if p.lower().endswith(".funscript")]
        if videos and scripts:
            return self.open_pair(videos[0], scripts[0])
        if videos or scripts:
            return self.open_path((videos or scripts)[0])
        return False

    def open_script(self, path: str, video: str | None = None) -> bool:
        try:
            s = funscript.load(path)
        except Exception as e:  # shown to the user, not swallowed
            QMessageBox.warning(self, "Open script", f"Could not open {Path(path).name}:\n{e}")
            return False
        self.script = s
        self.noscript.hide()
        if video is None:
            self._show_variants([], "")
        self.session.open_script(str(s.path), s.actions, video=video)
        self.curve.set_actions(s.actions)
        self.heat.set_points(heatmap(s.actions, 200))
        self._load_marks(str(s.path))
        self.offset.blockSignals(True)
        self.offset.setValue(self.session.offset_ms)
        self.offset.blockSignals(False)
        self._set_video(self.session.video_path)
        secs = s.duration_ms / 1000
        note = f", {s.dropped} broken points skipped" if s.dropped else ""
        vid = f" + {Path(self.session.video_path).name}" if self.session.video_path else " (no video)"
        self.info.setText(f"{s.path.name}{vid}: {len(s.actions)} points, {secs:.0f} s{note}")
        return True

    def _load_marks(self, path: str) -> None:
        try:
            bms, chs = marks.load_bookmarks(path), marks.load_chapters(path)
        except marks.MarksError:
            bms, chs = [], []  # broken metadata must not block playing
        self._chapters = chs
        self.curve.set_marks([b.time_ms for b in bms], [(c.start_ms, c.end_ms) for c in chs])
        self.chapter.clear()
        for c in chs:
            self.chapter.addItem(c.name or f"Chapter at {c.start_ms // 1000} s")
        self.chapter.setVisible(bool(chs))

    def _chapter_chosen(self, idx: int) -> None:
        if 0 <= idx < len(self._chapters):
            # Script time → video time: the offset moves the script later.
            self.player.setPosition(self._chapters[idx].start_ms + self.session.offset_ms)

    def _set_video(self, video: str) -> None:
        if video:
            self.player.setSource(QUrl.fromLocalFile(video))
            self.video.show()
        else:
            self.player.setSource(QUrl())
            self.video.hide()

    def release_media(self) -> None:
        """Stop and drop the media before the page goes away. Qt's decoder
        thread may still be opening the file; destroying the player under it
        crashes (seen with a broken file), so the source is cleared first."""
        self.player.stop()
        self.player.setSource(QUrl())

    def closeEvent(self, e) -> None:  # noqa: N802
        self.release_media()
        super().closeEvent(e)

    # ---- playback ------------------------------------------------------
    def toggle_play(self) -> None:
        if self.player.playbackState() == QMediaPlayer.PlaybackState.PlayingState:
            self.player.pause()
        else:
            self.player.play()

    def seek_by(self, delta_ms: int) -> None:
        self.player.setPosition(max(0, self.player.position() + delta_ms))

    def on_position(self, video_ms: int) -> None:
        if self.session.loop and (to := self.session.loop.jump(video_ms)) is not None:
            self.player.setPosition(to)
            return
        if not self.slider.isSliderDown():
            self.slider.setValue(int(video_ms))
        self.curve.set_playhead(self.session.script_ms(video_ms) if self.script else None)

    def _offset_changed(self, ms: int) -> None:
        self.session.set_offset(ms)
        self.on_position(self.player.position())

    def _state_changed(self, state) -> None:
        playing = state == QMediaPlayer.PlaybackState.PlayingState
        self.play_button.setText("Pause" if playing else "Play")

    def _set_loop(self, start: int | None = None, end: int | None = None) -> None:
        cur = self.session.loop or LoopRange(0, 0)
        self.session.loop = LoopRange(start if start is not None else cur.start_ms,
                                      end if end is not None else cur.end_ms)

    def _clear_loop(self) -> None:
        self.session.loop = None
