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

constexpr int rowHorizontalPadding = 11;

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

void fillRounded(QPainter& painter, const QRect& rect, const QColor& color,
                 qreal radius = 0.0) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, radius > 0.0);
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  if (radius > 0.0) painter.drawRoundedRect(rect, radius, radius);
  else painter.drawRect(rect);
  painter.restore();
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
  painter.drawRoundedRect(book, 1.5, 1.5);
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
  fillRounded(painter, rect, colors.background, 5.0);
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

  int left = row.left() + rowHorizontalPadding;
  if (checkable) {
    QStyleOptionButton checkbox;
    checkbox.rect = QRect{left, row.center().y() - 7, 14, 14};
    checkbox.state = QStyle::State_Enabled;
    if (index.data(Qt::CheckStateRole).toInt() == Qt::Checked) checkbox.state |= QStyle::State_On;
    else checkbox.state |= QStyle::State_Off;
    const QStyle* style = option.widget != nullptr ? option.widget->style() : QApplication::style();
    style->drawPrimitive(QStyle::PE_IndicatorCheckBox, &checkbox, &painter, option.widget);
    left += 22;
  }

  const QString status = index.data(ChangedFileListModel::statusCodeRole).toString();
  const QString tone = index.data(ChangedFileListModel::statusToneRole).toString();
  const qlonglong added = index.data(ChangedFileListModel::addedCountRole).toLongLong();
  const qlonglong removed = index.data(ChangedFileListModel::removedCountRole).toLongLong();
  const bool binary = index.data(ChangedFileListModel::binaryRole).toBool();
  const QRect badgeRect{row.right() - rowHorizontalPadding - 21, row.center().y() - 10, 21, 21};
  const QFont badgeFont = rowFont(option, theme::Metrics::textBadge, QFont::Bold);
  drawStatusBadge(painter, badgeRect, status, tone, badgeFont);

  const QFont metaFont = rowFont(option, theme::Metrics::textMeta);
  const int deltaLeft = drawDelta(painter, badgeRect.left() - 9, row.top() + 7,
                                  added, removed, binary, metaFont);
  const int textRight = std::max(left, deltaLeft - 8);
  const QFont nameFont = rowFont(option, theme::Metrics::textSmall, QFont::DemiBold);
  const QString name = index.data(ChangedFileListModel::nameRole).toString();
  const QString directory = index.data(ChangedFileListModel::directoryRole).toString();

  painter.save();
  painter.setPen(token.ink);
  painter.setFont(nameFont);
  painter.drawText(QRect{left, row.top() + 7, textRight - left, 18},
                   textFlags(Qt::AlignLeft | Qt::AlignVCenter),
                   elided(name, nameFont, textRight - left));
  painter.setPen(theme::colors().muted);
  painter.setFont(metaFont);
  painter.drawText(QRect{left, row.top() + 27, textRight - left, 16},
                   textFlags(Qt::AlignLeft | Qt::AlignVCenter),
                   elided(directory, metaFont, textRight - left));
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

QString initials(const QString& author) {
  const QStringList parts = author.split(QChar{u' '}, Qt::SkipEmptyParts);
  QString result;
  for (qsizetype i = 0; i < std::min<qsizetype>(2, parts.size()); ++i) {
    if (!parts.at(i).isEmpty()) result.append(parts.at(i).front().toUpper());
  }
  return result.isEmpty() ? QStringLiteral("G") : result;
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
  QColor background = Qt::transparent;
  if (option.state.testFlag(QStyle::State_Selected)) background = token.greenWash;
  else if (option.state.testFlag(QStyle::State_MouseOver)) background = theme::colors().soft;
  if (background != Qt::transparent) fillRounded(*painter, option.rect.adjusted(1, 1, -1, -1), background, 8.0);

  const QRect iconRect{option.rect.left() + 10, option.rect.center().y() - 9, 18, 18};
  drawRepositoryGlyph(*painter, iconRect, identityColor(index.data(RepositoryListModel::nameRole).toString()));
  const int left = iconRect.right() + 10;
  int right = option.rect.right() - 10;

  const qlonglong changes = index.data(RepositoryListModel::changeCountRole).toLongLong();
  if (changes > 0) {
    const QString label = QString::number(changes);
    const QFont countFont = rowFont(option, theme::Metrics::textMeta, QFont::Bold);
    const int width = std::max(21, QFontMetrics{countFont}.horizontalAdvance(label) + 10);
    const QRect countRect{right - width + 1, option.rect.center().y() - 10, width, 21};
    fillRounded(*painter, countRect, theme::colors().soft, 9.0);
    painter->setPen(theme::colors().added);
    painter->setFont(countFont);
    painter->drawText(countRect, textFlags(Qt::AlignCenter), label);
    right = countRect.left() - 9;
  }

  const QFont nameFont = rowFont(option, theme::Metrics::textBody, QFont::DemiBold);
  const QFont ownerFont = rowFont(option, theme::Metrics::textMeta);
  const QString name = index.data(RepositoryListModel::nameRole).toString();
  const QString owner = index.data(RepositoryListModel::ownerRole).toString();
  painter->setPen(token.ink);
  painter->setFont(nameFont);
  painter->drawText(QRect{left, option.rect.top() + 8, right - left, 19},
                    textFlags(Qt::AlignLeft | Qt::AlignVCenter),
                    elided(name, nameFont, right - left));
  painter->setPen(theme::colors().muted);
  painter->setFont(ownerFont);
  painter->drawText(QRect{left, option.rect.top() + 29, right - left, 16},
                    textFlags(Qt::AlignLeft | Qt::AlignVCenter),
                    elided(owner, ownerFont, right - left));

  // Repository selection deliberately has no left-edge marker. The selected
  // background above is the complete selection treatment.
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
  const int dayHeight = startsDay ? 30 : 0;
  QRect commitRect = option.rect.adjusted(0, dayHeight, 0, 0);

  if (startsDay) {
    const QRect dayRect{option.rect.left(), option.rect.top(), option.rect.width(), dayHeight};
    painter->fillRect(dayRect, theme::colors().soft);
    painter->setPen(token.lineSoft);
    painter->drawLine(dayRect.bottomLeft(), dayRect.bottomRight());
    const QFont dayFont = rowFont(option, theme::Metrics::textMeta, QFont::DemiBold);
    painter->setFont(dayFont);
    painter->setPen(theme::colors().muted);
    painter->drawText(dayRect.adjusted(13, 0, -10, 0),
                      textFlags(Qt::AlignLeft | Qt::AlignVCenter),
                      index.data(HistoryCommitListModel::dayLabelRole).toString());
  }

  QColor background = token.panel;
  if (option.state.testFlag(QStyle::State_Selected)) background = token.greenWash;
  else if (option.state.testFlag(QStyle::State_MouseOver)) background = theme::colors().soft;
  painter->fillRect(commitRect, background);
  drawBottomLine(*painter, commitRect);
  if (option.state.testFlag(QStyle::State_Selected)) {
    painter->fillRect(QRect{commitRect.left(), commitRect.top(), 3, commitRect.height()}, token.green);
  }

  int graphWidth = 0;
  if (graph) {
    graphWidth = graph->width * 22 + 16;
    const auto x = [&commitRect](int lane) { return commitRect.left() + 16 + lane * 22; };
    const auto y = commitRect.center().y();
    painter->setRenderHint(QPainter::Antialiasing);
    const auto line = [painter](QPointF start, QPointF end, int lane) {
      painter->setPen(QPen(theme::branchColor(lane), 2.2));
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
    painter->setPen(QPen(theme::branchColor(graph->color), 2.2));
    painter->setBrush(graph->parents.size() > 1 ? background : theme::branchColor(graph->color));
    painter->drawEllipse(QPointF(x(graph->lane), y), 4.5, 4.5);
    painter->setBrush(Qt::NoBrush);
  }
  const int left = commitRect.left() + 13 + graphWidth;
  const QRect authorRect{commitRect.right() - 39, commitRect.top() + 12, 26, 26};
  const int right = authorRect.left() - 10;
  const QFont titleFont = rowFont(option, theme::Metrics::textBody, QFont::DemiBold);
  const QFont metaFont = rowFont(option, theme::Metrics::textMeta);
  const QString title = index.data(HistoryCommitListModel::titleRole).toString();
  const QString hash = index.data(HistoryCommitListModel::shortHashRole).toString();
  const QString author = index.data(HistoryCommitListModel::authorRole).toString();
  const QDateTime date = index.data(HistoryCommitListModel::dateRole).toDateTime();

  painter->setPen(token.ink);
  painter->setFont(titleFont);
  painter->drawText(QRect{left, commitRect.top() + 7, right - left, 18},
                    textFlags(Qt::AlignLeft | Qt::AlignVCenter),
                    elided(title, titleFont, right - left));
  const QString metadata = QStringLiteral("%1 · %2 · %3").arg(hash, relativeTime(date), author);
  painter->setPen(theme::colors().muted);
  painter->setFont(metaFont);
  painter->drawText(QRect{left, commitRect.top() + 28, right - left, 16},
                    textFlags(Qt::AlignLeft | Qt::AlignVCenter),
                    elided(metadata, metaFont, right - left));

  const auto authorColor = identityColor(author);
  fillRounded(*painter, authorRect, theme::tint(authorColor), 13.0);
  painter->setPen(authorColor);
  painter->setFont(rowFont(option, theme::Metrics::textBadge, QFont::Bold));
  painter->drawText(authorRect, textFlags(Qt::AlignCenter), initials(author));

  if (!refs.isEmpty()) {
    int tagLeft = left;
    const int tagTop = commitRect.top() + 48;
    const QFont tagFont = rowFont(option, theme::Metrics::textBadge);
    const QFontMetrics metrics{tagFont};
    for (const QString& originalRef : refs) {
      QString ref = originalRef;
      if (ref.startsWith(QStringLiteral("tag: "))) ref.remove(0, 5);
      const int width = metrics.horizontalAdvance(ref) + 12;
      if (tagLeft + width > right) break;
      const QRect tagRect{tagLeft, tagTop, width, 18};
      const QColor refColor = graph ? theme::branchColor(graph->color) : token.green;
      fillRounded(*painter, tagRect, theme::tint(refColor), 5.0);
      painter->setPen(refColor);
      painter->setFont(tagFont);
      painter->drawText(tagRect, textFlags(Qt::AlignCenter), ref);
      tagLeft += width + 4;
    }
  }

  drawFocus(*painter, option, commitRect);
  painter->restore();
}

QSize HistoryCommitItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                          const QModelIndex& index) const {
  const bool startsDay = index.data(HistoryCommitListModel::startsDayGroupRole).toBool();
  const int refsHeight = index.data(HistoryCommitListModel::refsRole).toStringList().isEmpty() ? 0 : 22;
  const auto* model = dynamic_cast<const HistoryCommitListModel*>(index.model());
  const auto* graph = model ? model->graphRowAt(index.row()) : nullptr;
  return {graph ? std::max(option.rect.width(), graph->width * 22 + 300) : option.rect.width(), (!graph && startsDay ? 30 : 0) + 52 + refsHeight};
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
