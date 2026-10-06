#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QList>

class QApplication;

namespace relay::theme {

struct Colors {
  QColor ink;
  QColor muted;
  QColor line;
  QColor lineSoft;
  QColor panel;
  QColor canvas;
  QColor soft;
  QColor green;
  QColor greenDeep;
  QColor greenWash;
  QColor orange;
  QColor outerBackground;
  QColor avatarCoral;
  QColor avatarViolet;
  QColor avatarBlue;
  QColor added;
  QColor removed;
  QColor onAccent;
  QList<QColor> branches;
};

struct Metrics {
  static constexpr int textBadge = 10;
  static constexpr int textMeta = 11;
  static constexpr int textSmall = 12;
  static constexpr int textBody = 13;
  static constexpr int textMedium = 15;
  static constexpr int textLarge = 17;
  static constexpr int textExtraLarge = 20;
  static constexpr int textCode = 12;

  static constexpr int titleRowHeight = 38;
  static constexpr int macTitleStripHeight = 34;
  static constexpr int repositoryActionRowHeight = 58;
  static constexpr int contentTabHeight = 45;
  static constexpr int statusRowHeight = 31;
  static constexpr int repositoryRowHeight = 54;
  static constexpr int fileRowHeight = 54;
  static constexpr int commitFileRowHeight = 46;
};

[[nodiscard]] const Colors& colors();
[[nodiscard]] QFont bodyFont();
[[nodiscard]] QFont codeFont();
[[nodiscard]] QPalette palette();
[[nodiscard]] QString styleSheet();

[[nodiscard]] QStringList presetIds();
[[nodiscard]] QJsonObject definition(const QString& id);
[[nodiscard]] QString validate(const QJsonObject& definition);
void configure(const QString& id, const QJsonObject& custom = {});
[[nodiscard]] QColor tint(const QColor& color, double amount = 0.15);
[[nodiscard]] QColor branchColor(int identity);
void apply(QApplication& application);

}  // namespace relay::theme
