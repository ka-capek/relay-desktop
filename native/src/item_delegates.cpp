#include "relay/item_delegates.hpp"

#include "relay/list_models.hpp"
#include "relay/theme.hpp"

#include <QApplication>
#include <QDateTime>
#include <QFontMetrics>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionFocusRect>

#include <algorithm>

namespace relay {
namespace {

constexpr int historyRowHeight = 24;
constexpr int historyLaneWidth = 14;
constexpr int dayHeaderHeight = 20;

QFont rowFont(const QStyleOptionViewItem& option, int pixelSize,
              QFont::Weight weight = QFont::Normal) {
  QFont font = option.font;
  font.setPixelSize(pixelSize);
  font.setWeight(weight);
  return font;
}

int textFlags(Qt::Alignment alignment) {
  return static_cast<int>(alignment.toInt());
}

// The design has no rounded corners; every filled shape is a plain box.
void fillBox(QPainter& painter, const QRect& rect, const QColor& color) {
  painter.fillRect(rect, color);
}

void drawFocus(QPainter& painter, const QStyleOptionViewItem& option,
               const QRect& rect) {
  if (!option.state.testFlag(QStyle::State_HasFocus)) return;
  QStyleOptionFocusRect focus;
  focus.rect = rect.adjusted(1, 1, -1, -1);
  focus.state = option.state;
  focus.direction = option.direction;
  focus.fontMetrics = option.fontMetrics;
  focus.palette = option.palette;
  focus.styleObject = option.styleObject;
  focus.backgroundColor = theme::colors().greenWash;
  const QStyle* style = option.widget != nullptr ? option.widget->style() : QApplication::style();
  style->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, option.widget);
}

QString elided(const QString& text, const QFont& font, int width) {
  return QFontMetrics{font}.elidedText(text, Qt::ElideRight, std::max(0, width));
}

QColor identityColor(const QString& value) {
  quint32 hash = 2166136261U;
  for (const auto character : value) hash = (hash ^ character.unicode()) * 16777619U;
  const auto& token = theme::colors();
  const QColor accents[]{token.avatarBlue, token.avatarViolet, token.avatarCoral};
  return accents[hash % 3];
}

void drawRepositoryGlyph(QPainter& painter, const QRect& rect, const QColor& color) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen{color};
  pen.setWidthF(1.5);
  pen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  const QRectF book = QRectF{rect}.adjusted(2.5, 1.5, -2.5, -1.5);
  painter.drawRect(book);
  painter.drawLine(QPointF{book.left() + 3.0, book.top()},
                   QPointF{book.left() + 3.0, book.bottom()});
  painter.drawLine(QPointF{book.left(), book.bottom() - 3.0},
                   QPointF{book.right(), book.bottom() - 3.0});
  painter.restore();
}

void drawBottomLine(QPainter& painter, const QRect& rect) {
  painter.save();
  painter.setPen(theme::colors().soft);
  painter.drawLine(rect.bottomLeft(), rect.bottomRight());
  painter.restore();
}

struct BadgeColors {
  QColor foreground;
  QColor background;
};

BadgeColors statusColors(const QString& tone) {
  if (tone == QStringLiteral("added")) {
    return {theme::colors().added, theme::tint(theme::colors().added)};
  }
  if (tone == QStringLiteral("deleted")) {
    return {theme::colors().removed, theme::tint(theme::colors().removed)};
  }
  return {theme::colors().orange, theme::tint(theme::colors().orange)};
}

void drawStatusBadge(QPainter& painter, const QRect& rect, const QString& code,
                     const QString& tone, const QFont& font) {
  const BadgeColors colors = statusColors(tone);
  fillBox(painter, rect, colors.background);
  painter.save();
  painter.setFont(font);
  painter.setPen(colors.foreground);
  painter.drawText(rect, textFlags(Qt::AlignCenter), code);
  painter.restore();
}

int drawDelta(QPainter& painter, int right, int top, qlonglong added,
              qlonglong removed, bool binary, const QFont& font) {
  painter.save();
  painter.setFont(font);
  const QFontMetrics metrics{font};
  if (binary) {
    const QString label = QObject::tr("binary");
    const int width = metrics.horizontalAdvance(label);
    painter.setPen(theme::colors().muted);
    painter.drawText(QRect{right - width, top, width, 18},
                     textFlags(Qt::AlignRight | Qt::AlignVCenter), label);
    painter.restore();
    return right - width;
  }

  if (removed > 0) {
    const QString label = QStringLiteral("−%1").arg(removed);
    const int width = metrics.horizontalAdvance(label);
    painter.setPen(theme::colors().removed);
    painter.drawText(QRect{right - width, top, width, 18},
                     textFlags(Qt::AlignRight | Qt::AlignVCenter), label);
    right -= width + 5;
  }
  if (added > 0) {
    const QString label = QStringLiteral("+%1").arg(added);
    const int width = metrics.horizontalAdvance(label);
    painter.setPen(theme::colors().added);
    painter.drawText(QRect{right - width, top, width, 18},
                     textFlags(Qt::AlignRight | Qt::AlignVCenter), label);
    right -= width + 5;
  }
  painter.restore();
  return right;
}

void paintFileRow(QPainter& painter, const QStyleOptionViewItem& option,
                  const QModelIndex& index, bool checkable) {
  const theme::Colors& token = theme::colors();
  const QRect row = option.rect;
  QColor background = token.panel;
  if (option.state.testFlag(QStyle::State_Selected)) background = theme::colors().greenWash;
  else if (option.state.testFlag(QStyle::State_MouseOver)) background = theme::colors().soft;
  painter.fillRect(row, background);
  drawBottomLine(painter, row);

  int left = row.left() + 8;
  if (checkable) {
    QStyleOptionButton checkbox;
    checkbox.rect = QRect{left, row.center().y() - 6, 12, 12};
    checkbox.state = QStyle::State_Enabled;
    if (index.data(Qt::CheckStateRole).toInt() == Qt::Checked) checkbox.state |= QStyle::State_On;
    else checkbox.state |= QStyle::State_Off;
    const QStyle* style = option.widget != nullptr ? option.widget->style() : QApplication::style();
    style->drawPrimitive(QStyle::PE_IndicatorCheckBox, &checkbox, &painter, option.widget);
    left += 19;
  }

  const QString status = index.data(ChangedFileListModel::statusCodeRole).toString();
  const QString tone = index.data(ChangedFileListModel::statusToneRole).toString();
  const qlonglong added = index.data(ChangedFileListModel::addedCountRole).toLongLong();
  const qlonglong removed = index.data(ChangedFileListModel::removedCountRole).toLongLong();
  const bool binary = index.data(ChangedFileListModel::binaryRole).toBool();
  // Status letter first, like `git status --short`.
  const QRect badgeRect{left, row.center().y() - 8, 16, 16};
  drawStatusBadge(painter, badgeRect, status, tone, rowFont(option, theme::Metrics::textBadge, QFont::Bold));
  left = badgeRect.right() + 7;

  const QFont metaFont = rowFont(option, theme::Metrics::textMeta);
  const int deltaLeft = drawDelta(painter, row.right() - 8, row.center().y() - 9, added, removed, binary, metaFont);
  const int textRight = std::max(left, deltaLeft - 8);
  const QFont nameFont = rowFont(option, theme::Metrics::textSmall, QFont::DemiBold);
  const QString name = index.data(ChangedFileListModel::nameRole).toString();
  const QString directory = index.data(ChangedFileListModel::directoryRole).toString();
  const QRect textRect{left, row.top(), textRight - left, row.height()};

  painter.save();
  painter.setPen(token.ink);
  painter.setFont(nameFont);
  const auto shownName = elided(name, nameFont, textRect.width());
  painter.drawText(textRect, textFlags(Qt::AlignLeft | Qt::AlignVCenter), shownName);
  // The folder follows the name on the same line, muted.
  const int nameWidth = QFontMetrics{nameFont}.horizontalAdvance(shownName) + 8;
  if (nameWidth < textRect.width()) {
    painter.setPen(theme::colors().muted);
    painter.setFont(metaFont);
    painter.drawText(textRect.adjusted(nameWidth, 0, 0, 0), textFlags(Qt::AlignLeft | Qt::AlignVCenter),
                     elided(directory, metaFont, textRect.width() - nameWidth));
  }
  painter.restore();
  drawFocus(painter, option, row);
}

QString relativeTime(const QDateTime& date) {
  if (!date.isValid()) return QObject::tr("Unknown time");
  const qint64 seconds = std::max<qint64>(0, date.secsTo(QDateTime::currentDateTime()));
  if (seconds < 60) return QObject::tr("Just now");
  if (seconds < 3600) return QObject::tr("%1m ago").arg(seconds / 60);
  if (seconds < 86400) return QObject::tr("%1h ago").arg(seconds / 3600);
  if (seconds < 604800) return QObject::tr("%1d ago").arg(seconds / 86400);
  return QLocale{}.toString(date.toLocalTime().date(), QStringLiteral("MMM d"));
}

}  // namespace

RepositoryItemDelegate::RepositoryItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

void RepositoryItemDelegate::paint(QPainter* painter,
                                    const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const {
  painter->save();
  painter->setClipRect(option.rect);
  const theme::Colors& token = theme::colors();
  const bool selected = option.state.testFlag(QStyle::State_Selected);
  if (selected) painter->fillRect(option.rect, token.greenWash);
  else if (option.state.testFlag(QStyle::State_MouseOver)) painter->fillRect(option.rect, token.soft);
  // Repository selection deliberately has no left-edge marker; the tint is
  // the complete selection treatment.

  const QRect iconRect{option.rect.left() + 8, option.rect.center().y() - 7, 14, 14};
  drawRepositoryGlyph(*painter, iconRect, identityColor(index.data(RepositoryListModel::nameRole).toString()));
  const int left = iconRect.right() + 7;
  int right = option.rect.right() - 8;

  const qlonglong changes = index.data(RepositoryListModel::changeCountRole).toLongLong();
  if (changes > 0) {
    const QString label = QString::number(changes);
    const QFont countFont = rowFont(option, theme::Metrics::textMeta, QFont::Bold);
    const int width = QFontMetrics{countFont}.horizontalAdvance(label);
    painter->setPen(token.added);
    painter->setFont(countFont);
    painter->drawText(QRect{right - width, option.rect.top(), width, option.rect.height()},
                      textFlags(Qt::AlignRight | Qt::AlignVCenter), label);
    right -= width + 8;
  }

  const QFont nameFont = rowFont(option, theme::Metrics::textSmall, QFont::DemiBold);
  const QFont ownerFont = rowFont(option, theme::Metrics::textMeta);
  const QString name = index.data(RepositoryListModel::nameRole).toString();
  const QString owner = index.data(RepositoryListModel::ownerRole).toString();
  const QRect textRect{left, option.rect.top(), right - left, option.rect.height()};
  const auto shownName = elided(name, nameFont, textRect.width());
  painter->setPen(token.ink);
  painter->setFont(nameFont);
  painter->drawText(textRect, textFlags(Qt::AlignLeft | Qt::AlignVCenter), shownName);
  const int nameWidth = QFontMetrics{nameFont}.horizontalAdvance(shownName) + 6;
  if (nameWidth < textRect.width()) {
    painter->setPen(token.muted);
    painter->setFont(ownerFont);
    painter->drawText(textRect.adjusted(nameWidth, 0, 0, 0), textFlags(Qt::AlignLeft | Qt::AlignVCenter),
                      elided(owner, ownerFont, textRect.width() - nameWidth));
  }
  drawFocus(*painter, option, option.rect);
  painter->restore();
}

QSize RepositoryItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                       const QModelIndex&) const {
  return {option.rect.width(), theme::Metrics::repositoryRowHeight};
}

ChangedFileItemDelegate::ChangedFileItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

void ChangedFileItemDelegate::paint(QPainter* painter,
                                     const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const {
  painter->save();
  painter->setClipRect(option.rect);
  paintFileRow(*painter, option, index, true);
  painter->restore();
}

QSize ChangedFileItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                        const QModelIndex&) const {
  return {option.rect.width(), theme::Metrics::fileRowHeight};
}

HistoryCommitItemDelegate::HistoryCommitItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

void HistoryCommitItemDelegate::paint(QPainter* painter,
                                      const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const {
  painter->save();
  painter->setClipRect(option.rect);
  const theme::Colors& token = theme::colors();
  const auto* model = dynamic_cast<const HistoryCommitListModel*>(index.model());
  const auto* graph = model ? model->graphRowAt(index.row()) : nullptr;
  const bool startsDay = !graph && index.data(HistoryCommitListModel::startsDayGroupRole).toBool();
  const QStringList refs = index.data(HistoryCommitListModel::refsRole).toStringList();
  const int highlight = index.data(HistoryCommitListModel::highlightRole).toInt();
  const int dayHeight = startsDay ? dayHeaderHeight : 0;
  const QRect commitRect = option.rect.adjusted(0, dayHeight, 0, 0);

  if (startsDay) {
    const QRect dayRect{option.rect.left(), option.rect.top(), option.rect.width(), dayHeight};
    painter->fillRect(dayRect, token.canvas);
    painter->setPen(token.lineSoft);
    painter->drawLine(dayRect.bottomLeft(), dayRect.bottomRight());
    painter->setFont(rowFont(option, theme::Metrics::textBadge, QFont::Bold));
    painter->setPen(token.muted);
    painter->drawText(dayRect.adjusted(10, 0, -10, 0), textFlags(Qt::AlignLeft | Qt::AlignVCenter),
                      index.data(HistoryCommitListModel::dayLabelRole).toString().toUpper());
  }

  const bool selected = option.state.testFlag(QStyle::State_Selected);
  QColor background = token.panel;
  if (selected) background = token.greenWash;
  else if (highlight == HistoryCommitListModel::matchHighlight) background = theme::tint(token.orange);
  else if (option.state.testFlag(QStyle::State_MouseOver)) background = token.soft;
  painter->fillRect(commitRect, background);
  if (selected) painter->fillRect(QRect{commitRect.left(), commitRect.top(), 2, commitRect.height()}, token.green);
  else if (highlight == HistoryCommitListModel::matchHighlight)
    painter->fillRect(QRect{commitRect.left(), commitRect.top(), 2, commitRect.height()}, token.orange);
  // Search results stay bright; everything else recedes but keeps its place.
  if (highlight == HistoryCommitListModel::dimmedHighlight && !selected) painter->setOpacity(0.3);

  int graphWidth = 0;
  if (graph) {
    // One graph column for all rows, so every message starts at the same x.
    graphWidth = std::max(model->graphLanes(), graph->width) * historyLaneWidth + 12;
    const auto x = [&commitRect](int lane) { return commitRect.left() + 12 + lane * historyLaneWidth; };
    const auto y = commitRect.center().y();
    painter->setRenderHint(QPainter::Antialiasing);
    const auto line = [painter](QPointF start, QPointF end, int lane) {
      painter->setPen(QPen(theme::branchColor(lane), 1.6));
      QPainterPath path(start);
      const qreal middle = (start.y() + end.y()) / 2.0;
      path.cubicTo(QPointF(start.x(), middle), QPointF(end.x(), middle), end);
      painter->drawPath(path);
    };
    for (qsizetype i = 0; i < graph->passing.size(); ++i) {
      const auto edge = graph->passing.at(i);
      line(QPointF(x(edge.first), commitRect.top()), QPointF(x(edge.second), commitRect.bottom() + 1), graph->passingColors.at(i));
    }
    if (graph->incoming) line(QPointF(x(graph->lane), commitRect.top()), QPointF(x(graph->lane), y), graph->color);
    for (qsizetype i = 0; i < graph->parents.size(); ++i)
      line(QPointF(x(graph->lane), y), QPointF(x(graph->parents.at(i)), commitRect.bottom() + 1), graph->parentColors.at(i));
    // Square commit marks; a merge is hollow.
    painter->setRenderHint(QPainter::Antialiasing, false);
    const QRect dot{x(graph->lane) - 3, y - 3, 7, 7};
    painter->fillRect(dot, theme::branchColor(graph->color));
    if (graph->parents.size() > 1) painter->fillRect(dot.adjusted(2, 2, -2, -2), background);
  }

  // Columns from the right: date, hash, author. Narrow views drop author,
  // then hash, before the message gets squeezed.
  const QFont titleFont = rowFont(option, theme::Metrics::textSmall);
  const QFont metaFont = rowFont(option, theme::Metrics::textMeta);
  QFont hashFont = theme::codeFont();
  hashFont.setPixelSize(theme::Metrics::textMeta);
  const QString title = index.data(HistoryCommitListModel::titleRole).toString();
  const QString hash = index.data(HistoryCommitListModel::shortHashRole).toString();
  const QString author = index.data(HistoryCommitListModel::authorRole).toString();
  const QDateTime date = index.data(HistoryCommitListModel::dateRole).toDateTime();
  // Column widths follow the view, not the graph, so columns line up.
  const int width = option.widget ? option.widget->width() : commitRect.width();
  int right = commitRect.right() - 8;
  const auto column = [&](int columnWidth, const QString& text, const QFont& font, const QColor& color, Qt::Alignment alignment) {
    const QRect rect{right - columnWidth, commitRect.top(), columnWidth, commitRect.height()};
    painter->setFont(font);
    painter->setPen(color);
    painter->drawText(rect, textFlags(alignment | Qt::AlignVCenter), elided(text, font, columnWidth));
    right = rect.left() - 10;
  };
  column(58, relativeTime(date), metaFont, token.muted, Qt::AlignRight);
  if (width > 380) column(56, hash, hashFont, token.muted, Qt::AlignLeft);
  if (width > 480) column(std::clamp(width / 6, 80, 140), author, metaFont, token.muted, Qt::AlignLeft);

  int left = commitRect.left() + 10 + graphWidth;
  if (!refs.isEmpty()) {
    // Branch and tag chips precede the message, as in `git log --oneline --decorate`.
    const QFont tagFont = rowFont(option, theme::Metrics::textBadge, QFont::DemiBold);
    const QFontMetrics metrics{tagFont};
    const int limit = left + std::max(80, (right - left) / 2);
    const QColor refColor = graph ? theme::branchColor(graph->color) : token.green;
    for (qsizetype i = 0; i < refs.size(); ++i) {
      QString ref = refs.at(i);
      if (ref.startsWith(QStringLiteral("tag: "))) ref.remove(0, 5);
      const int chipWidth = metrics.horizontalAdvance(ref) + 10;
      if (left + chipWidth > limit) {
        const auto more = QStringLiteral("+%1").arg(refs.size() - i);
        painter->setFont(tagFont);
        painter->setPen(token.muted);
        painter->drawText(QRect{left, commitRect.top(), metrics.horizontalAdvance(more) + 2, commitRect.height()},
                          textFlags(Qt::AlignLeft | Qt::AlignVCenter), more);
        left += metrics.horizontalAdvance(more) + 8;
        break;
      }
      const QRect chip{left, commitRect.center().y() - 8, chipWidth, 16};
      fillBox(*painter, chip, theme::tint(refColor));
      painter->setPen(refColor);
      painter->setFont(tagFont);
      painter->drawText(chip, textFlags(Qt::AlignCenter), ref);
      left += chipWidth + 4;
    }
    left += 4;
  }
  painter->setPen(token.ink);
  painter->setFont(titleFont);
  painter->drawText(QRect{left, commitRect.top(), std::max(0, right - left), commitRect.height()},
                    textFlags(Qt::AlignLeft | Qt::AlignVCenter), elided(title, titleFont, right - left));
  painter->setOpacity(1.0);
  drawBottomLine(*painter, commitRect);
  drawFocus(*painter, option, commitRect);
  painter->restore();
}

QSize HistoryCommitItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                          const QModelIndex& index) const {
  const auto* model = dynamic_cast<const HistoryCommitListModel*>(index.model());
  const auto* graph = model ? model->graphRowAt(index.row()) : nullptr;
  const bool startsDay = !graph && index.data(HistoryCommitListModel::startsDayGroupRole).toBool();
  return {graph ? std::max(option.rect.width(), model->graphLanes() * historyLaneWidth + 320) : option.rect.width(),
          (startsDay ? dayHeaderHeight : 0) + historyRowHeight};
}

CommitFileItemDelegate::CommitFileItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

void CommitFileItemDelegate::paint(QPainter* painter,
                                    const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const {
  painter->save();
  painter->setClipRect(option.rect);
  paintFileRow(*painter, option, index, false);
  painter->restore();
}

QSize CommitFileItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                       const QModelIndex&) const {
  return {option.rect.width(), theme::Metrics::commitFileRowHeight};
}

}  // namespace relay
