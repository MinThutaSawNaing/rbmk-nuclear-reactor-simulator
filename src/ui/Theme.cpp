#include "ui/Theme.h"

namespace theme {

QString styleSheet()
{
    return QStringLiteral(R"(
QWidget {
    background-color: #07090e;
    color: #cbd5e1;
    font-family: 'Consolas', 'Courier New', monospace;
    font-size: 12px;
}
QMainWindow, QDialog { background-color: #07090e; }
QMainWindow::separator {
    background-color: #0b101a;
    width: 5px;
    height: 5px;
}
QMainWindow::separator:hover { background-color: #1e293b; }

QFrame#HeaderBar {
    background-color: #121824;
    border-bottom: 1px solid #1e293b;
}
QFrame#Card {
    background-color: #121824;
    border: 1px solid #1e293b;
    border-radius: 10px;
}
QFrame#SubCard {
    background-color: #0b101a;
    border: 1px solid #1e293b;
    border-radius: 8px;
}
QLabel#AppTitle {
    color: #38bdf8;
    font-size: 17px;
    font-weight: bold;
    letter-spacing: 1px;
}
QLabel#AppSubtitle { color: #64748b; font-size: 11px; }
QLabel#SectionTitle {
    color: #e2e8f0;
    font-size: 11px;
    font-weight: bold;
    letter-spacing: 1px;
}
QLabel#Hint { color: #64748b; font-size: 11px; }
QLabel#Mono { font-family: 'Consolas', 'Courier New', monospace; }
QLabel#Clock { color: #e2e8f0; font-size: 13px; font-weight: bold; }
QLabel#StatusBadge {
    border-radius: 6px;
    padding: 5px 12px;
    font-size: 11px;
    font-weight: bold;
    letter-spacing: 1px;
}
QLabel#SelectedLabel { color: #67e8f9; font-weight: bold; }

QGroupBox {
    background-color: #121824;
    border: 1px solid #1e293b;
    border-radius: 10px;
    margin-top: 14px;
    padding: 10px 10px 8px 10px;
    font-size: 11px;
    font-weight: bold;
    color: #94a3b8;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 12px;
    padding: 0 4px;
    color: #38bdf8;
    letter-spacing: 1px;
}

QPushButton {
    background-color: #1a2333;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 6px 10px;
    color: #cbd5e1;
}
QPushButton:hover { background-color: #223047; border-color: #475569; }
QPushButton:pressed { background-color: #0f172a; }
QPushButton:disabled { color: #475569; border-color: #1e293b; background-color: #0f141d; }

QPushButton#PrimaryButton {
    background-color: #0e7490;
    border: 1px solid #22d3ee;
    color: #ecfeff;
    font-weight: bold;
}
QPushButton#PrimaryButton:hover { background-color: #0891b2; }

QPushButton#DangerButton {
    background-color: #7f1d1d;
    border: 2px solid #ef4444;
    color: #fee2e2;
    font-weight: bold;
    letter-spacing: 1px;
    padding: 9px;
}
QPushButton#DangerButton:hover { background-color: #991b1b; }

QPushButton#AutoHoldButton {
    background-color: #0b101a;
    border: 1px solid #334155;
    color: #94a3b8;
    padding: 4px 10px;
    font-weight: bold;
}

QPushButton#AutoHoldButton:hover { border-color: #475569; }

QPushButton#AutoHoldButton:checked {
    background-color: #052e2b;
    border: 1px solid #2dd4bf;
    color: #5eead4;
}

QPushButton#PumpButton {
    background-color: #0b101a;
    border: 1px solid #334155;
    padding: 5px 4px;
    font-size: 10px;
    color: #64748b;
}
QPushButton#PumpButton:checked {
    background-color: #064e3b;
    border: 1px solid #10b981;
    color: #6ee7b7;
    font-weight: bold;
}

QComboBox {
    background-color: #1a2333;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 4px 8px;
    color: #cbd5e1;
}
QComboBox:hover { border-color: #475569; }
QComboBox::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView {
    background-color: #121824;
    border: 1px solid #334155;
    selection-background-color: #0e7490;
    color: #cbd5e1;
}

QSlider::groove:horizontal {
    height: 6px;
    background: #1e293b;
    border-radius: 3px;
}
QSlider::sub-page:horizontal {
    background: #0891b2;
    border-radius: 3px;
}
QSlider::handle:horizontal {
    background: #67e8f9;
    border: 2px solid #0e7490;
    width: 14px;
    margin: -6px 0;
    border-radius: 8px;
}
QSlider::handle:horizontal:hover { background: #a5f3fc; }
QSlider::groove:horizontal:disabled { background: #141b27; }
QSlider::sub-page:horizontal:disabled { background: #1e293b; }
QSlider::handle:horizontal:disabled { background: #334155; border-color: #1e293b; }

QPlainTextEdit, QTextEdit, QTextBrowser {
    background-color: #090d16;
    border: 1px solid #1e293b;
    border-radius: 8px;
    color: #cbd5e1;
    selection-background-color: #0e7490;
}
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical {
    background: #0f172a;
    width: 8px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #334155;
    border-radius: 4px;
    min-height: 24px;
}
QScrollBar::handle:vertical:hover { background: #475569; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: #0f172a; height: 8px; }
QScrollBar::handle:horizontal { background: #334155; border-radius: 4px; min-width: 24px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

QMenuBar { background-color: #0b101a; border-bottom: 1px solid #1e293b; }
QMenuBar::item { padding: 5px 10px; background: transparent; color: #94a3b8; }
QMenuBar::item:selected { background: #1a2333; color: #67e8f9; }
QMenu {
    background-color: #121824;
    border: 1px solid #334155;
    padding: 4px;
}
QMenu::item { padding: 6px 24px 6px 20px; border-radius: 4px; }
QMenu::item:selected { background-color: #0e7490; color: #ecfeff; }
QMenu::item:disabled { color: #475569; }
QMenu::separator { height: 1px; background: #1e293b; margin: 4px 8px; }

QDockWidget {
    color: #94a3b8;
    font-weight: bold;
    titlebar-close-icon: none;
}
QDockWidget::title {
    background-color: #121824;
    border: 1px solid #1e293b;
    padding: 5px;
}
QStatusBar { background-color: #0b101a; color: #64748b; border-top: 1px solid #1e293b; }
QToolTip {
    background-color: #121824;
    color: #e2e8f0;
    border: 1px solid #38bdf8;
    padding: 5px;
}
QSplitter::handle { background-color: #0b101a; }
QSplitter::handle:hover { background-color: #1e293b; }
)");
}

} // namespace theme
