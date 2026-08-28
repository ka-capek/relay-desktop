#include "relay/theme.hpp"

#include <QApplication>
#include <QFontDatabase>

namespace relay::theme {

const Colors& colors() {
  static const Colors value{
      .ink = QColor{0x19, 0x20, 0x1e},
      .muted = QColor{0x69, 0x73, 0x6f},
      .line = QColor{0xdf, 0xe3, 0xdf},
      .lineSoft = QColor{0xe9, 0xec, 0xe9},
      .panel = QColor{0xff, 0xff, 0xff},
      .canvas = QColor{0xf6, 0xf7, 0xf4},
      .soft = QColor{0xf0, 0xf2, 0xee},
      .green = QColor{0x17, 0x6b, 0x4b},
      .greenDeep = QColor{0x0f, 0x52, 0x38},
      .greenWash = QColor{0xe5, 0xf1, 0xeb},
      .orange = QColor{0xd2, 0x76, 0x3c},
      .outerBackground = QColor{0xe9, 0xec, 0xe7},
      .avatarCoral = QColor{0xbb, 0x62, 0x48},
      .avatarViolet = QColor{0x72, 0x5a, 0x9f},
      .avatarBlue = QColor{0x46, 0x77, 0x9e},
      .added = QColor{0x2d, 0x87, 0x59},
      .removed = QColor{0xc6, 0x5f, 0x49},
  };
  return value;
}

QFont bodyFont() {
  QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
  font.setPixelSize(Metrics::textBody);
  return font;
}

QFont codeFont() {
  QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  font.setPixelSize(Metrics::textCode);
  return font;
}

QPalette palette() {
  const Colors& token = colors();
  QPalette result;
  result.setColor(QPalette::Window, token.panel);
  result.setColor(QPalette::WindowText, token.ink);
  result.setColor(QPalette::Base, token.panel);
  result.setColor(QPalette::AlternateBase, token.canvas);
  result.setColor(QPalette::Text, token.ink);
  result.setColor(QPalette::Button, token.panel);
  result.setColor(QPalette::ButtonText, token.ink);
  result.setColor(QPalette::BrightText, token.panel);
  result.setColor(QPalette::Highlight, token.greenWash);
  result.setColor(QPalette::HighlightedText, token.ink);
  result.setColor(QPalette::Link, token.green);
  result.setColor(QPalette::LinkVisited, token.greenDeep);
  result.setColor(QPalette::ToolTipBase, token.panel);
  result.setColor(QPalette::ToolTipText, token.ink);
  result.setColor(QPalette::PlaceholderText, QColor{0x8a, 0x93, 0x8f});
  result.setColor(QPalette::Disabled, QPalette::Text, QColor{0x84, 0x90, 0x8a});
  result.setColor(QPalette::Disabled, QPalette::WindowText, QColor{0x84, 0x90, 0x8a});
  result.setColor(QPalette::Disabled, QPalette::ButtonText, QColor{0x84, 0x90, 0x8a});
  result.setColor(QPalette::Disabled, QPalette::Highlight, token.soft);
  result.setColor(QPalette::Disabled, QPalette::HighlightedText, token.muted);
  return result;
}

QString styleSheet() {
  // QSS has no variables. Keep these literals synchronized with Colors and
  // Metrics; tst_theme.cpp guards the complete token set.
  return QStringLiteral(R"QSS(
    QWidget {
      color: #19201e;
      font-size: 13px;
      selection-background-color: #e5f1eb;
      selection-color: #19201e;
    }
    QMainWindow, #relayRoot { background: #ffffff; }
    #titleRow, #actionRow {
      background: #fbfcfa;
      border-bottom: 1px solid #dfe3df;
    }
    #titleRow { min-height: 38px; max-height: 38px; }
    #macTitleStrip { min-height: 34px; max-height: 34px; background: #fbfcfa; }
    #actionRow { min-height: 58px; max-height: 58px; }
    #sidebar {
      background: #f7f8f5;
      border-right: 1px solid #dfe3df;
    }
    #contentTabs {
      min-height: 45px;
      max-height: 45px;
      background: #ffffff;
      border-bottom: 1px solid #dfe3df;
    }
    #statusBar, QStatusBar {
      min-height: 31px;
      max-height: 31px;
      background: #f5f7f4;
      border-top: 1px solid #dfe3df;
      color: #76817b;
      font-size: 11px;
    }
    #sectionHeading {
      color: #65706b;
      font-size: 12px;
      font-weight: 700;
    }
    QLabel[role="badge"] { font-size: 10px; }
    QLabel[role="meta"], QStatusBar QLabel { color: #69736f; font-size: 11px; }
    QLabel[role="small"] { font-size: 12px; }
    QLabel[role="body"] { font-size: 13px; }
    QLabel[role="medium"] { font-size: 15px; }
    QLabel[role="large"] { font-size: 17px; font-weight: 650; }
    QLabel[role="title"] { font-size: 20px; font-weight: 700; }
    QLabel[role="code"] { font-family: "Geist Mono", "SFMono-Regular", Consolas, monospace; font-size: 12px; }

    QPushButton, QToolButton {
      min-height: 28px;
      padding: 3px 10px;
      border: 1px solid #d7dcd7;
      border-radius: 7px;
      background: #ffffff;
      color: #19201e;
      font-size: 13px;
    }
    QPushButton:hover, QToolButton:hover { background: #f7f8f6; }
    QPushButton:pressed, QToolButton:pressed { background: #f0f2ee; }
    QPushButton:disabled, QToolButton:disabled { color: #84908a; background: #f6f7f4; }
    QPushButton:focus, QToolButton:focus { border: 1px solid #78a28e; }
    QPushButton[kind="primary"], QToolButton[kind="primary"] {
      border: 0;
      background: #176b4b;
      color: #ffffff;
      font-weight: 650;
    }
    QPushButton[kind="primary"]:hover, QToolButton[kind="primary"]:hover { background: #0f5238; }
    QPushButton[kind="primary"]:disabled, QToolButton[kind="primary"]:disabled { background: #aeb8b2; }
    QPushButton[kind="flat"], QToolButton[kind="flat"],
    QPushButton[kind="icon"], QToolButton[kind="icon"] {
      border: 0;
      background: transparent;
    }
    QPushButton[kind="flat"]:hover, QToolButton[kind="flat"]:hover,
    QPushButton[kind="icon"]:hover, QToolButton[kind="icon"]:hover { background: #f0f2ee; }
    QPushButton[kind="danger"], QToolButton[kind="danger"] { color: #a54e43; }

    QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox {
      min-height: 36px;
      border: 1px solid #d8ddd8;
      border-radius: 7px;
      padding: 0 9px;
      background: #ffffff;
      color: #19201e;
      font-size: 12px;
    }
    QTextEdit, QPlainTextEdit { padding: 8px 9px; }
    QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus {
      border: 1px solid #78a28e;
      background: #ffffff;
    }
    QComboBox::drop-down { width: 24px; border: 0; }
    QCheckBox, QRadioButton { spacing: 7px; font-size: 12px; }
    QCheckBox::indicator, QRadioButton::indicator { width: 13px; height: 13px; }

    QMenuBar { background: #fbfcfa; color: #46534d; font-size: 12px; spacing: 1px; }
    QMenuBar::item { padding: 4px 9px; border-radius: 5px; background: transparent; }
    QMenuBar::item:selected, QMenuBar::item:pressed { background: #e8ebe7; }
    QMenu {
      min-width: 250px;
      padding: 5px;
      border: 1px solid #d8ddd8;
      border-radius: 8px;
      background: #ffffff;
      color: #19201e;
      font-size: 12px;
    }
    QMenu::item { padding: 7px 28px 7px 9px; border-radius: 6px; }
    QMenu::item:selected { background: #f0f4f1; }
    QMenu::separator { height: 1px; margin: 6px 3px; background: #e9ece9; }

    QListView, QTreeView, QTableView {
      border: 0;
      outline: 0;
      background: #ffffff;
      alternate-background-color: #f6f7f4;
      selection-background-color: #eef3ef;
      selection-color: #19201e;
    }
    #repositoryList { background: #f7f8f5; }
    #repositoryList::item { min-height: 54px; border: 0; border-radius: 8px; }
    #repositoryList::item:hover { background: #ecefeb; }
    #repositoryList::item:selected { background: #e1e9e3; border: 0; }
    #changedFileList::item { min-height: 54px; border-bottom: 1px solid #f0f2ef; }
    #commitFileList::item { min-height: 46px; border-bottom: 1px solid #f0f2ef; }
    #historyList::item { border: 0; }

    QTabBar { background: #ffffff; }
    QTabBar::tab {
      min-width: 100px;
      min-height: 43px;
      padding: 0 14px;
      border: 0;
      border-bottom: 2px solid transparent;
      background: transparent;
      color: #707a75;
      font-size: 13px;
      font-weight: 600;
    }
    QTabBar::tab:selected { color: #19201e; border-bottom: 2px solid #176b4b; }

    QDialog, #modalCard {
      border: 1px solid #dfe3df;
      border-radius: 15px;
      background: #ffffff;
    }
    #modalBackdrop { background: rgba(19, 26, 22, 98); }
    #modalNote {
      padding: 9px 10px;
      border: 0;
      border-radius: 7px;
      background: #f4f6f3;
      color: #6d7772;
      font-size: 11px;
    }
    #toast {
      padding: 10px 15px;
      border: 1px solid #304d3e;
      border-radius: 8px;
      background: #21372d;
      color: #ffffff;
      font-size: 13px;
    }
    #toast[error="true"] { border-color: #673b34; background: #492d28; }

    QScrollBar:vertical { width: 10px; margin: 0; border: 0; background: transparent; }
    QScrollBar:horizontal { height: 10px; margin: 0; border: 0; background: transparent; }
    QScrollBar::handle { min-width: 28px; min-height: 28px; border-radius: 4px; background: #c8cfca; }
    QScrollBar::handle:hover { background: #aeb8b2; }
    QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
    QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

    QToolTip {
      padding: 5px 7px;
      border: 1px solid #d8ddd8;
      border-radius: 5px;
      background: #ffffff;
      color: #19201e;
      font-size: 11px;
    }
  )QSS");
}

void apply(QApplication& application) {
  application.setPalette(palette());
  application.setFont(bodyFont());
  application.setStyleSheet(styleSheet());
}

}  // namespace relay::theme
