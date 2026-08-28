#include "relay/diff_view.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QSet>
#include <QStyle>
#include <QStyleOptionFocusRect>
#include <QStyledItemDelegate>

#include <algorithm>
#include <limits>

namespace relay {
namespace {

constexpr int lineHeight = 26;
constexpr int gutterWidth = 40;
constexpr int minimumTextWidth = 610;

class DiffDelegate final : public QStyledItemDelegate {
 public:
  explicit DiffDelegate(QFont font, QObject* parent)
      : QStyledItemDelegate(parent), font_(std::move(font)) {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    painter->save();
    painter->setClipRect(option.rect);
    painter->setFont(font_);

    const auto kind = index.data(DiffModel::kindRole).value<DiffLineKind>();
    if (kind == DiffLineKind::hunk) {
      painter->fillRect(option.rect, QColor{0xe9, 0xf1, 0xec});
      painter->setPen(QColor{0xd8, 0xe4, 0xdc});
      painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
      painter->setPen(QColor{0x4e, 0x70, 0x60});
      painter->drawText(option.rect.adjusted(14, 0, -8, 0),
                        Qt::AlignLeft | Qt::AlignVCenter,
                        index.data(Qt::DisplayRole).toString());
    } else {
      QColor rowColor{0xfc, 0xfc, 0xfb};
      QColor gutterColor = rowColor;
      QColor gutterBorder{0xec, 0xee, 0xec};
      if (kind == DiffLineKind::addition) {
        rowColor = QColor{0xe8, 0xf4, 0xec};
        gutterColor = QColor{0xda, 0xec, 0xdf};
        gutterBorder = QColor{0xc9, 0xe0, 0xd0};
      } else if (kind == DiffLineKind::removal) {
        rowColor = QColor{0xfa, 0xe9, 0xe6};
        gutterColor = QColor{0xf4, 0xdc, 0xd7};
        gutterBorder = QColor{0xea, 0xcd, 0xc7};
      }

      const bool gutter = index.column() != DiffModel::textColumn;
      painter->fillRect(option.rect, gutter ? gutterColor : rowColor);
      if (gutter) {
        painter->setPen(gutterBorder);
        painter->drawLine(option.rect.topRight(), option.rect.bottomRight());
      }

      painter->setPen(gutter ? QColor{0xa0, 0xa7, 0xa3} : QColor{0x19, 0x20, 0x1e});
      const QRect textRect = gutter ? option.rect.adjusted(2, 0, -10, 0)
                                    : option.rect.adjusted(12, 0, -8, 0);
      const Qt::Alignment alignment =
          (gutter ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter;
      painter->drawText(textRect, static_cast<int>(alignment.toInt()),
                        index.data(Qt::DisplayRole).toString());
    }

    if (option.state.testFlag(QStyle::State_Selected)) {
      painter->fillRect(option.rect, QColor{0x17, 0x6b, 0x4b, 0x1c});
    }
    if (option.state.testFlag(QStyle::State_HasFocus)) {
      QStyleOptionFocusRect focus;
      focus.QStyleOption::operator=(option);
      focus.rect = option.rect.adjusted(1, 1, -1, -1);
      focus.backgroundColor = QColor{0xe5, 0xf1, 0xeb};
      option.widget->style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, painter,
                                            option.widget);
    }
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
    return {minimumTextWidth, lineHeight};
  }

 private:
  QFont font_;
};

}  // namespace

DiffView::DiffView(QWidget* parent)
    : QTableView(parent),
      model_(new DiffModel(this)),
      copyAction_(new QAction(tr("Copy"), this)),
      codeFont_(QFontDatabase::systemFont(QFontDatabase::FixedFont)) {
  codeFont_.setPixelSize(12);
  setModel(model_);
  setItemDelegate(new DiffDelegate(codeFont_, this));

  horizontalHeader()->hide();
  horizontalHeader()->setMinimumSectionSize(0);
  horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  horizontalHeader()->setStretchLastSection(false);
  verticalHeader()->hide();
  verticalHeader()->setMinimumSectionSize(lineHeight);
  verticalHeader()->setDefaultSectionSize(lineHeight);
  verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

  setFrameShape(QFrame::NoFrame);
  setShowGrid(false);
  setWordWrap(false);
  setTextElideMode(Qt::ElideNone);
  setSelectionBehavior(QAbstractItemView::SelectRows);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  setTabKeyNavigation(false);
  setContextMenuPolicy(Qt::ActionsContextMenu);
  viewport()->setAutoFillBackground(true);
  viewport()->setBackgroundRole(QPalette::Base);

  setAccessibleName(tr("Diff"));
  setAccessibleDescription(
      tr("Unified Git diff with old and new line numbers and accessible line descriptions"));

  copyAction_->setShortcuts(QKeySequence::Copy);
  copyAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  copyAction_->setEnabled(false);
  addAction(copyAction_);
  connect(copyAction_, &QAction::triggered, this, &DiffView::copySelection);
  connect(selectionModel(), &QItemSelectionModel::selectionChanged, this,
          [this] { copyAction_->setEnabled(selectionModel()->hasSelection()); });

  rebuildSpansAndWidths();
}

void DiffView::setDiff(QString diff) {
  clearSpans();
  model_->setDiff(std::move(diff));
  rebuildSpansAndWidths();
  scrollToTop();
}

void DiffView::clearDiff() { setDiff({}); }

DiffModel* DiffView::diffModel() noexcept { return model_; }

const DiffModel* DiffView::diffModel() const noexcept { return model_; }

QAction* DiffView::copyAction() const noexcept { return copyAction_; }

void DiffView::copySelection() {
  const QModelIndexList indexes = selectionModel()->selectedRows(DiffModel::textColumn);
  if (indexes.isEmpty()) {
    return;
  }

  QList<int> rows;
  rows.reserve(indexes.size());
  for (const QModelIndex& index : indexes) {
    rows.append(index.row());
  }
  std::sort(rows.begin(), rows.end());
  rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

  QString text;
  for (const int row : rows) {
    if (!text.isEmpty()) {
      text.append(QChar{u'\n'});
    }
    text.append(model_->textAt(row));
  }
  QApplication::clipboard()->setText(text);
}

QSize DiffView::sizeHint() const { return {690, 360}; }

void DiffView::paintEvent(QPaintEvent* event) {
  QTableView::paintEvent(event);
  if (model_->rowCount() != 0) {
    return;
  }
  QPainter painter{viewport()};
  painter.setPen(QColor{0x8a, 0x93, 0x8f});
  painter.setFont(font());
  painter.drawText(viewport()->rect().adjusted(18, 18, -18, -18),
                   Qt::AlignLeft | Qt::AlignTop,
                   tr("No textual diff available for this file."));
}

void DiffView::rebuildSpansAndWidths() {
  clearSpans();
  for (int row = 0; row < model_->rowCount(); ++row) {
    if (model_->isHunk(row)) {
      setSpan(row, DiffModel::oldLineColumn, 1, DiffModel::columnCountValue);
    }
  }

  int widest = 0;
  const QFontMetrics metrics{codeFont_};
  for (int row = 0; row < model_->rowCount(); ++row) {
    widest = std::max(widest, metrics.horizontalAdvance(model_->textAt(row).toString()));
  }
  const qint64 requestedWidth = static_cast<qint64>(widest) + 24;
  const int contentWidth = static_cast<int>(std::clamp<qint64>(
      requestedWidth, minimumTextWidth, static_cast<qint64>(QWIDGETSIZE_MAX)));

  setColumnWidth(DiffModel::oldLineColumn, gutterWidth);
  setColumnWidth(DiffModel::newLineColumn, gutterWidth);
  setColumnWidth(DiffModel::textColumn, contentWidth);
  copyAction_->setEnabled(selectionModel()->hasSelection());
  viewport()->update();
}

}  // namespace relay
