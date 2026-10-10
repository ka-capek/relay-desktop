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
  return {QStringLiteral("light"), QStringLiteral("dark"), QStringLiteral("catppuccin-latte"), QStringLiteral("catppuccin-mocha"),
          QStringLiteral("mono-light"), QStringLiteral("mono-dark")};
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
  result.setColor(QPalette::Light, token.soft);
  result.setColor(QPalette::Midlight, token.soft);
  result.setColor(QPalette::Mid, token.line);
  result.setColor(QPalette::Dark, token.line);
  result.setColor(QPalette::Shadow, token.outerBackground);
  result.setColor(QPalette::Accent, token.green);
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
  // Square, hairline, dense: no rounded corners anywhere (0.5.5 design).
  QString sheet = QStringLiteral(R"QSS(
    QWidget {
      color: @ink@;
      font-size: 12px;
      selection-background-color: @greenWash@;
      selection-color: @ink@;
    }
    QMainWindow, #relayRoot { background: @panel@; }
    #titleRow, #actionRow {
      background: @canvas@;
      border-bottom: 1px solid @line@;
    }
    #titleRow { min-height: 30px; max-height: 30px; }
    #macTitleStrip { min-height: 30px; max-height: 30px; background: @canvas@; }
    #actionRow { min-height: 38px; max-height: 38px; }
    #sidebar {
      background: @canvas@;
      border-right: 1px solid @line@;
    }
    /* The pane's top edge is the line under the tabs; tabs overlap it by one
       pixel so the selected tab's underline sits on it, full width. */
    QTabWidget::pane { border: 0; border-top: 1px solid @line@; top: -1px; background: @panel@; }
    QTabWidget::tab-bar { left: 0; }
    #contentTabs > QTabBar { background: @panel@; }
    #statusBar, QStatusBar {
      min-height: 22px;
      max-height: 22px;
      background: @canvas@;
      border-top: 1px solid @line@;
      color: @muted@;
      font-size: 11px;
    }
    QStatusBar::item { border: 0; }
    /* Status bar buttons read as status text: same size and centre line. */
    QStatusBar QPushButton, QStatusBar QToolButton {
      min-height: 18px; max-height: 18px; padding: 0 6px; border: 0;
      background: transparent; color: @muted@; font-size: 11px;
    }
    QStatusBar QPushButton:hover, QStatusBar QToolButton:hover { background: @soft@; color: @ink@; }
    #sectionHeading {
      color: @muted@;
      font-size: 10px;
      font-weight: 700;
    }
    QLabel[role="error"] { color: @removed@; }
    QStatusBar[error="true"], #statusBar[error="true"] { color: @removed@; }
    QLabel[role="badge"] { font-size: 10px; }
    QLabel[role="meta"], QStatusBar QLabel { color: @muted@; font-size: 11px; }
    QLabel[role="small"] { font-size: 11px; }
    QLabel[role="body"] { font-size: 12px; }
    QLabel[role="medium"] { font-size: 14px; }
    QLabel[role="large"] { font-size: 15px; font-weight: 650; }
    QLabel[role="title"] { font-size: 17px; font-weight: 700; }
    QLabel[role="code"] { font-family: "Geist Mono", "SFMono-Regular", Consolas, monospace; font-size: 11px; }

    /* One control height (22px content + 1px borders = 24px) and no vertical
       padding, so every button, field and combo shares a centre line. */
    QPushButton, QToolButton {
      min-height: 22px;
      max-height: 22px;
      padding: 0 8px;
      border: 1px solid @line@;
      border-radius: 0;
      background: @panel@;
      color: @ink@;
      font-size: 12px;
    }
    QPushButton:hover, QToolButton:hover { background: @soft@; }
    QPushButton:pressed, QToolButton:pressed, QToolButton:checked { background: @greenWash@; }
    QPushButton:disabled, QToolButton:disabled { color: @muted@; background: @canvas@; }
    QPushButton:focus, QToolButton:focus { border: 1px solid @green@; }
    QPushButton[kind="primary"], QToolButton[kind="primary"] {
      border: 1px solid @green@;
      background: @green@;
      color: @onAccent@;
      font-weight: 650;
    }
    QPushButton[kind="primary"]:hover, QToolButton[kind="primary"]:hover { background: @greenDeep@; border-color: @greenDeep@; }
    QPushButton[kind="primary"]:disabled, QToolButton[kind="primary"]:disabled { background: @line@; border-color: @line@; }
    QPushButton[kind="flat"], QToolButton[kind="flat"],
    QPushButton[kind="icon"], QToolButton[kind="icon"] {
      border: 0;
      background: transparent;
    }
    QPushButton[kind="flat"]:hover, QToolButton[kind="flat"]:hover,
    QPushButton[kind="icon"]:hover, QToolButton[kind="icon"]:hover { background: @soft@; }
    QPushButton[kind="danger"], QToolButton[kind="danger"] { color: @removed@; }

    QLineEdit, QComboBox, QSpinBox {
      min-height: 22px;
      max-height: 22px;
      border: 1px solid @line@;
      border-radius: 0;
      padding: 0 6px;
      background: @panel@;
      color: @ink@;
      font-size: 12px;
    }
    QTextEdit, QPlainTextEdit {
      border: 1px solid @line@;
      border-radius: 0;
      padding: 4px 6px;
      background: @panel@;
      color: @ink@;
      font-size: 12px;
    }
    QToolButton[kind="square"] { min-width: 22px; max-width: 22px; padding: 0; }
    QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus {
      border: 1px solid @green@;
      background: @panel@;
    }
    QSpinBox { padding-right: 26px; }
    QSpinBox::up-button { subcontrol-origin: padding; subcontrol-position: top right; width: 22px; height: 12px; border: 0; }
    QSpinBox::down-button { subcontrol-origin: padding; subcontrol-position: bottom right; width: 22px; height: 12px; border: 0; }
    QSpinBox::up-arrow { image: url(@upArrow@); width: 12px; height: 12px; }
    QSpinBox::down-arrow { image: url(@arrow@); width: 12px; height: 12px; }
    QComboBox { padding-right: 24px; }
    QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 22px; border: 0; }
    QComboBox::down-arrow { image: url(@arrow@); width: 12px; height: 12px; }
    QPushButton::menu-indicator, QToolButton::menu-indicator { subcontrol-origin: padding; subcontrol-position: center right; right: 6px; image: url(@arrow@); width: 12px; height: 12px; }
    QPushButton[hasMenu="true"], QToolButton[hasMenu="true"] { padding-right: 22px; }
    QToolButton#syncButton::menu-button { width: 20px; border-left: 1px solid @line@; }
    QToolButton#syncButton::menu-arrow { image: url(@arrow@); width: 12px; height: 12px; }
    QComboBox QAbstractItemView { padding: 0; border: 1px solid @line@; background: @panel@; selection-background-color: @greenWash@; }
    QCheckBox, QRadioButton { spacing: 6px; font-size: 12px; }
    QCheckBox::indicator, QRadioButton::indicator { width: 12px; height: 12px; }

    QMenuBar { background: @canvas@; color: @ink@; font-size: 12px; spacing: 0; }
    QMenuBar::item { padding: 3px 8px; border-radius: 0; background: transparent; }
    QMenuBar::item:selected, QMenuBar::item:pressed { background: @soft@; }
    QMenu {
      min-width: 220px;
      padding: 2px 0;
      border: 1px solid @line@;
      border-radius: 0;
      background: @panel@;
      color: @ink@;
      font-size: 12px;
    }
    QMenu::item { padding: 4px 24px 4px 10px; border-radius: 0; }
    QMenu::item:selected { background: @greenWash@; }
    QMenu::item:disabled { color: @muted@; }
    QMenu::separator { height: 1px; margin: 2px 0; background: @lineSoft@; }

    QListView, QTreeView, QTableView {
      border: 0;
      outline: 0;
      background: @panel@;
      alternate-background-color: @canvas@;
      selection-background-color: @greenWash@;
      selection-color: @ink@;
    }
    #repositoryList { background: @canvas@; }
    #repositoryList::item { border: 0; border-radius: 0; }
    #repositoryList::item:hover { background: @soft@; }
    #repositoryList::item:selected { background: @greenWash@; border: 0; }
    #changedFileList::item, #commitFileList::item, #historyList::item { border: 0; }

    QTabBar { background: @panel@; }
    /* The tab bar and the sidebar heading are both 30px with one hairline
       under them, so the line runs straight across the window. */
    #sidebarHeading { border-bottom: 1px solid @line@; background: @canvas@; }
    QTabBar::tab {
      min-width: 0;
      min-height: 28px;
      max-height: 28px;
      padding: 0 14px;
      border: 0;
      border-bottom: 2px solid transparent;
      background: transparent;
      color: @muted@;
      font-size: 12px;
      font-weight: 600;
    }
    QTabBar::tab:hover { color: @ink@; }
    QTabBar::tab:selected { color: @ink@; border-bottom: 2px solid @green@; }

    /* A 1px hairline inside a wider grab area keeps panes easy to resize. */
    QSplitter::handle { background: @panel@; }
    QSplitter::handle:horizontal { border-left: 1px solid @line@; }
    QSplitter::handle:vertical { border-top: 1px solid @line@; }
    QSplitter::handle:hover { background: @soft@; }

    QDialog, #modalCard {
      border: 1px solid @line@;
      border-radius: 0;
      background: @panel@;
    }
    #modalBackdrop { background: rgba(19, 26, 22, 98); }
    #modalNote {
      padding: 6px 8px;
      border: 0;
      border-radius: 0;
      background: @canvas@;
      color: @muted@;
      font-size: 11px;
    }
    #toast {
      padding: 6px 10px;
      border: 1px solid @line@;
      border-radius: 0;
      background: @soft@;
      color: @ink@;
      font-size: 12px;
    }
    #toast[error="true"] { border-color: @removed@; background: @soft@; }

    QScrollBar:vertical { width: 8px; margin: 0; border: 0; background: transparent; }
    QScrollBar:horizontal { height: 8px; margin: 0; border: 0; background: transparent; }
    QScrollBar::handle { min-width: 24px; min-height: 24px; border-radius: 0; background: @line@; }
    QScrollBar::handle:hover { background: @muted@; }
    QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
    QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

    QToolTip {
      padding: 3px 6px;
      border: 1px solid @line@;
      border-radius: 0;
      background: @panel@;
      color: @ink@;
      font-size: 11px;
    }
  )QSS");
  sheet.replace(QStringLiteral("@upArrow@"), colors().panel.lightness() < 128 ? QStringLiteral(":/relay/chevron-up-dark.svg") : QStringLiteral(":/relay/chevron-up-light.svg"));
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
