"""The SamNPlayer look (colours from the 0.7 interface, style.css)."""

from __future__ import annotations

from PySide6.QtWidgets import QApplication

COLORS = {
    "bg": "#0c0d12",
    "surface": "#181a22",
    "border": "#2e3240",
    "text": "#f3f0f7",
    "text_dim": "#a8a3b5",
    "accent": "#b896e8",
    "accent_hover": "#cbb0f2",
    "teal": "#5ad4c4",
    "danger": "#e87171",
}

STYLE = """
QWidget {{ background: {bg}; color: {text}; font-size: 13px; }}
QPushButton {{
  background: {surface}; border: 1px solid {border}; border-radius: 14px;
  padding: 6px 14px;
}}
QPushButton:hover {{ border-color: {accent}; }}
QPushButton:checked {{ background: {accent}; color: {bg}; border-color: {accent}; }}
QPushButton:disabled {{ color: {text_dim}; border-color: {surface}; }}
QComboBox, QSpinBox {{
  background: {surface}; border: 1px solid {border}; border-radius: 10px; padding: 4px 8px;
}}
QFrame[frameShape="6"] {{ border: 1px solid {border}; border-radius: 14px; }}
QSlider::groove:horizontal {{ height: 4px; background: {border}; border-radius: 2px; }}
QSlider::sub-page:horizontal {{ background: {accent}; border-radius: 2px; }}
QSlider::handle:horizontal {{
  width: 14px; margin: -6px 0; border-radius: 7px; background: {text};
}}
QLabel {{ background: transparent; }}
""".format(**COLORS)


def apply(app: QApplication) -> None:
    app.setStyleSheet(STYLE)
