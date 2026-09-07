#include "relay/theme.hpp"

#include <QApplication>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QStyle>
#include <QPushButton>
#include <QToolButton>

static void initializeThemeResources() {
  static const bool ready = [] { Q_INIT_RESOURCE(resources); return true; }();
  Q_UNUSED(ready);
}

namespace relay::theme {

namespace {
Colors active;
bool initialized = false;
const QList<QPair<QString, QColor Colors::*>>& roles() {
  static const QList<QPair<QString, QColor Colors::*>> value{
    {QStringLiteral("text"), &Colors::ink}, {QStringLiteral("muted"), &Colors::muted},
    {QStringLiteral("border"), &Colors::line}, {QStringLiteral("borderSoft"), &Colors::lineSoft},
    {QStringLiteral("panel"), &Colors::panel}, {QStringLiteral("canvas"), &Colors::canvas},
    {QStringLiteral("surface"), &Colors::soft}, {QStringLiteral("accent"), &Colors::green},
    {QStringLiteral("accentHover"), &Colors::greenDeep}, {QStringLiteral("selection"), &Colors::greenWash},
    {QStringLiteral("warning"), &Colors::orange}, {QStringLiteral("background"), &Colors::outerBackground},
    {QStringLiteral("coral"), &Colors::avatarCoral}, {QStringLiteral("violet"), &Colors::avatarViolet},
    {QStringLiteral("blue"), &Colors::avatarBlue}, {QStringLiteral("added"), &Colors::added},
    {QStringLiteral("removed"), &Colors::removed}, {QStringLiteral("onAccent"), &Colors::onAccent}
  };
  return value;
}
}

QStringList presetIds() {
  return {QStringLiteral("light"), QStringLiteral("dark"), QStringLiteral("catppuccin-latte"), QStringLiteral("catppuccin-mocha")};
}

QJsonObject definition(const QString& id) {
  initializeThemeResources();
  const auto name = presetIds().contains(id) ? id : QStringLiteral("light");
  QFile file(QStringLiteral(":/relay/themes/%1.json").arg(name));
  if (!file.open(QIODevice::ReadOnly)) qFatal("Bundled theme is missing");
  return QJsonDocument::fromJson(file.readAll()).object();
}

QString validate(const QJsonObject& object) {
  static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
  for (auto it = object.begin(); it != object.end(); ++it) {
    if (it.key() == QStringLiteral("branches")) {
      const auto array = it.value().toArray();
      if (!it.value().isArray() || array.size() < 2 || array.size() > 32)
        return QStringLiteral("branches must contain 2–32 hex colors.");
      for (const auto color : array) if (!hex.match(color.toString()).hasMatch())
        return QStringLiteral("Branch colors must use #RRGGBB.");
    } else {
      bool known = false;
      for (const auto& role : roles()) if (role.first == it.key()) known = true;
      if (!known) return QStringLiteral("Unknown theme color: %1").arg(it.key());
      if (!hex.match(it.value().toString()).hasMatch()) return QStringLiteral("%1 must use #RRGGBB.").arg(it.key());
    }
  }
  return {};
}

void configure(const QString& id, const QJsonObject& custom) {
  auto object = definition(id);
  if (validate(custom).isEmpty()) for (auto it = custom.begin(); it != custom.end(); ++it) object.insert(it.key(), it.value());
  for (const auto& role : roles()) active.*(role.second) = QColor(object.value(role.first).toString());
  active.branches.clear();
  for (const auto color : object.value(QStringLiteral("branches")).toArray()) active.branches.append(QColor(color.toString()));
  initialized = true;
}

const Colors& colors() {
  if (!initialized) configure(QStringLiteral("light"));
  return active;
}

QColor tint(const QColor& color, double amount) {
  const auto base = colors().panel;
  return QColor::fromRgbF(static_cast<float>(base.redF() * (1 - amount) + color.redF() * amount),
                         static_cast<float>(base.greenF() * (1 - amount) + color.greenF() * amount),
                         static_cast<float>(base.blueF() * (1 - amount) + color.blueF() * amount));
}
QColor branchColor(int identity) {
  const auto& list = colors().branches;
  return list.at(qMax(0, identity) % list.size());
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
  result.setColor(QPalette::PlaceholderText, token.muted);
  result.setColor(QPalette::Disabled, QPalette::Text, token.muted);
  result.setColor(QPalette::Disabled, QPalette::WindowText, token.muted);
  result.setColor(QPalette::Disabled, QPalette::ButtonText, token.muted);
  result.setColor(QPalette::Disabled, QPalette::Highlight, token.soft);
  result.setColor(QPalette::Disabled, QPalette::HighlightedText, token.muted);
  return result;
}

QString styleSheet() {
  QString sheet = QStringLiteral(R"QSS(
    QWidget {
      color: @ink@;
      font-size: 13px;
      selection-background-color: @greenWash@;
      selection-color: @ink@;
    }
    QSplitter::handle { background: @outerBackground@; }
    QMainWindow, #relayRoot { background: @panel@; }
    #titleRow, #actionRow {
      background: @canvas@;
      border-bottom: 1px solid @line@;
    }
    #titleRow { min-height: 38px; max-height: 38px; }
    #macTitleStrip { min-height: 34px; max-height: 34px; background: @canvas@; }
    #actionRow { min-height: 58px; max-height: 58px; }
    #sidebar {
      background: @canvas@;
      border-right: 1px solid @line@;
    }
    #contentTabs > QTabBar {
      min-height: 45px;
      max-height: 45px;
      background: @panel@;
      border-bottom: 1px solid @line@;
    }
    #statusBar, QStatusBar {
      min-height: 31px;
      max-height: 31px;
      background: @canvas@;
      border-top: 1px solid @line@;
      color: @muted@;
      font-size: 11px;
    }
    #sectionHeading {
      color: @muted@;
      font-size: 12px;
      font-weight: 700;
    }
    QLabel[role="error"] { color: @removed@; }
    QStatusBar[error="true"], #statusBar[error="true"] { color: @removed@; }
    QLabel[role="badge"] { font-size: 10px; }
    QLabel[role="meta"], QStatusBar QLabel { color: @muted@; font-size: 11px; }
    QLabel[role="small"] { font-size: 12px; }
    QLabel[role="body"] { font-size: 13px; }
    QLabel[role="medium"] { font-size: 15px; }
    QLabel[role="large"] { font-size: 17px; font-weight: 650; }
    QLabel[role="title"] { font-size: 20px; font-weight: 700; }
    QLabel[role="code"] { font-family: "Geist Mono", "SFMono-Regular", Consolas, monospace; font-size: 12px; }

    QPushButton, QToolButton {
      min-height: 28px;
      padding: 3px 10px;
      border: 1px solid @line@;
      border-radius: 7px;
      background: @panel@;
      color: @ink@;
      font-size: 13px;
    }
    QPushButton:hover, QToolButton:hover { background: @canvas@; }
    QPushButton:pressed, QToolButton:pressed { background: @soft@; }
    QPushButton:disabled, QToolButton:disabled { color: @muted@; background: @canvas@; }
    QPushButton:focus, QToolButton:focus { border: 1px solid @green@; }
    QPushButton[kind="primary"], QToolButton[kind="primary"] {
      border: 0;
      background: @green@;
      color: @onAccent@;
      font-weight: 650;
    }
    QPushButton[kind="primary"]:hover, QToolButton[kind="primary"]:hover { background: @greenDeep@; }
    QPushButton[kind="primary"]:disabled, QToolButton[kind="primary"]:disabled { background: @line@; }
    QPushButton[kind="flat"], QToolButton[kind="flat"],
    QPushButton[kind="icon"], QToolButton[kind="icon"] {
      border: 0;
      background: transparent;
    }
    QPushButton[kind="flat"]:hover, QToolButton[kind="flat"]:hover,
    QPushButton[kind="icon"]:hover, QToolButton[kind="icon"]:hover { background: @soft@; }
    QPushButton[kind="danger"], QToolButton[kind="danger"] { color: @removed@; }

    QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox {
      min-height: 36px;
      border: 1px solid @line@;
      border-radius: 7px;
      padding: 0 9px;
      background: @panel@;
      color: @ink@;
      font-size: 12px;
    }
    QTextEdit, QPlainTextEdit { padding: 8px 9px; }
    QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus {
      border: 1px solid @green@;
      background: @panel@;
    }
    QComboBox { padding-right: 32px; }
    QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 28px; border: 0; }
    QComboBox::down-arrow { image: url(@arrow@); width: 14px; height: 14px; }
    QPushButton::menu-indicator, QToolButton::menu-indicator { subcontrol-origin: padding; subcontrol-position: center right; right: 8px; image: url(@arrow@); width: 14px; height: 14px; }
    QPushButton[hasMenu="true"], QToolButton[hasMenu="true"] { padding-right: 25px; }
    QComboBox QAbstractItemView { padding: 4px; border: 1px solid @line@; background: @panel@; selection-background-color: @greenWash@; }
    QCheckBox, QRadioButton { spacing: 7px; font-size: 12px; }
    QCheckBox::indicator, QRadioButton::indicator { width: 13px; height: 13px; }

    QMenuBar { background: @canvas@; color: @ink@; font-size: 12px; spacing: 1px; }
    QMenuBar::item { padding: 4px 9px; border-radius: 5px; background: transparent; }
    QMenuBar::item:selected, QMenuBar::item:pressed { background: @soft@; }
    QMenu {
      min-width: 250px;
      padding: 5px;
      border: 1px solid @line@;
      border-radius: 8px;
      background: @panel@;
      color: @ink@;
      font-size: 12px;
    }
    QMenu::item { padding: 7px 28px 7px 9px; border-radius: 6px; }
    QMenu::item:selected { background: @soft@; }
    QMenu::separator { height: 1px; margin: 6px 3px; background: @lineSoft@; }

    QListView, QTreeView, QTableView {
      border: 0;
      outline: 0;
      background: @panel@;
      alternate-background-color: @canvas@;
      selection-background-color: @greenWash@;
      selection-color: @ink@;
    }
    #repositoryList { background: @canvas@; }
    #repositoryList::item { min-height: 54px; border: 0; border-radius: 8px; }
    #repositoryList::item:hover { background: @soft@; }
    #repositoryList::item:selected { background: @greenWash@; border: 0; }
    #changedFileList::item { min-height: 54px; border-bottom: 1px solid @lineSoft@; }
    #commitFileList::item { min-height: 46px; border-bottom: 1px solid @lineSoft@; }
    #historyList::item { border: 0; }

    QTabBar { background: @panel@; }
    QTabBar::tab {
      min-width: 100px;
      min-height: 43px;
      padding: 0 14px;
      border: 0;
      border-bottom: 2px solid transparent;
      background: transparent;
      color: @muted@;
      font-size: 13px;
      font-weight: 600;
    }
    QTabBar::tab:selected { color: @ink@; border-bottom: 2px solid @green@; }

    QDialog, #modalCard {
      border: 1px solid @line@;
      border-radius: 15px;
      background: @panel@;
    }
    #modalBackdrop { background: rgba(19, 26, 22, 98); }
    #modalNote {
      padding: 9px 10px;
      border: 0;
      border-radius: 7px;
      background: @canvas@;
      color: @muted@;
      font-size: 11px;
    }
    #toast {
      padding: 10px 15px;
      border: 1px solid @soft@;
      border-radius: 8px;
      background: @soft@;
      color: @ink@;
      font-size: 13px;
    }
    #toast[error="true"] { border-color: @removed@; background: @soft@; }

    QScrollBar:vertical { width: 10px; margin: 0; border: 0; background: transparent; }
    QScrollBar:horizontal { height: 10px; margin: 0; border: 0; background: transparent; }
    QScrollBar::handle { min-width: 28px; min-height: 28px; border-radius: 4px; background: @line@; }
    QScrollBar::handle:hover { background: @line@; }
    QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
    QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

    QToolTip {
      padding: 5px 7px;
      border: 1px solid @line@;
      border-radius: 5px;
      background: @panel@;
      color: @ink@;
      font-size: 11px;
    }
  )QSS");
  sheet.replace(QStringLiteral("@arrow@"), colors().panel.lightness() < 128 ? QStringLiteral(":/relay/chevron-dark.svg") : QStringLiteral(":/relay/chevron-light.svg"));
  sheet.replace(QStringLiteral("@canvas@"), colors().canvas.name());
  sheet.replace(QStringLiteral("@green@"), colors().green.name());
  sheet.replace(QStringLiteral("@greenDeep@"), colors().greenDeep.name());
  sheet.replace(QStringLiteral("@greenWash@"), colors().greenWash.name());
  sheet.replace(QStringLiteral("@ink@"), colors().ink.name());
  sheet.replace(QStringLiteral("@line@"), colors().line.name());
  sheet.replace(QStringLiteral("@lineSoft@"), colors().lineSoft.name());
  sheet.replace(QStringLiteral("@muted@"), colors().muted.name());
  sheet.replace(QStringLiteral("@onAccent@"), colors().onAccent.name());
  sheet.replace(QStringLiteral("@outerBackground@"), colors().outerBackground.name());
  sheet.replace(QStringLiteral("@panel@"), colors().panel.name());
  sheet.replace(QStringLiteral("@removed@"), colors().removed.name());
  sheet.replace(QStringLiteral("@soft@"), colors().soft.name());
  return sheet;
}

void apply(QApplication& application) {
  initializeThemeResources();
  if (application.style()->objectName() != QStringLiteral("fusion")) application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  application.setPalette(palette());
  application.setFont(bodyFont());
  for (auto* widget : QApplication::allWidgets()) {
    if (auto* button = qobject_cast<QPushButton*>(widget)) button->setProperty("hasMenu", button->menu() != nullptr);
    if (auto* button = qobject_cast<QToolButton*>(widget)) button->setProperty("hasMenu", button->menu() != nullptr);
  }
  application.setStyleSheet(styleSheet());
  for (auto* widget : QApplication::allWidgets()) widget->update();
}

}  // namespace relay::theme
