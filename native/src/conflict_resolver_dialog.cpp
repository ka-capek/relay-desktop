#include "relay/conflict_resolver_dialog.hpp"
#include "relay/theme.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace relay {
namespace {

constexpr int rowHeight = 20;
constexpr int numberWidth = 44;
constexpr int boxSize = 10;
constexpr int textInset = 24;

QString withoutEnding(QString line) {
  if (line.endsWith(u'\n')) line.chop(1);
  if (line.endsWith(u'\r')) line.chop(1);
  return line;
}

bool isTextColumn(const int column) {
  return column == ConflictLinesModel::currentTextColumn || column == ConflictLinesModel::incomingTextColumn;
}

bool confirm(QWidget* parent, const QString& title, const QString& text) {
  QMessageBox dialog(QMessageBox::Question, title, text, QMessageBox::Ok | QMessageBox::Cancel, parent);
  dialog.setTextFormat(Qt::PlainText);
  dialog.setDefaultButton(QMessageBox::Cancel);
  return dialog.exec() == QMessageBox::Ok;
}

// Flat rows: conflict lines are tinted until their block is decided, then
// chosen lines are green and the others are muted. A square box in front of
// each conflict line shows whether it goes into the result; a click anywhere
// on the line toggles it.
class ConflictLineDelegate final : public QStyledItemDelegate {
 public:
  explicit ConflictLineDelegate(QObject* parent) : QStyledItemDelegate(parent), font_(theme::codeFont()) {}
  void setCodeFont(QFont font) { font_ = std::move(font); }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    painter->save();
    painter->setClipRect(option.rect);
    painter->setFont(font_);
    const auto& colors = theme::colors();
    const bool conflict = index.data(ConflictLinesModel::blockRole).toInt() >= 0;
    const bool text = isTextColumn(index.column());
    const bool line = index.data(ConflictLinesModel::lineRole).toBool();
    const bool decided = index.data(ConflictLinesModel::decidedRole).toBool();
    const bool chosen = index.data(ConflictLinesModel::chosenRole).toBool();
    QColor background = colors.panel;
    if (conflict && !line) background = colors.soft;
    else if (conflict && !decided) background = theme::tint(colors.orange, text ? 0.10 : 0.18);
    else if (conflict && chosen) background = theme::tint(colors.added, text ? 0.14 : 0.22);
    painter->fillRect(option.rect, background);
    painter->setPen(colors.lineSoft);
    if (!text) painter->drawLine(option.rect.topRight(), option.rect.bottomRight());
    if (index.column() == ConflictLinesModel::incomingNumberColumn)
      painter->drawLine(option.rect.topLeft(), option.rect.bottomLeft());

    QRect textRect = text ? option.rect.adjusted(conflict ? textInset : 8, 0, -8, 0) : option.rect.adjusted(2, 0, -8, 0);
    if (text && conflict && line) {
      const QRect box(option.rect.left() + 8, option.rect.center().y() - boxSize / 2, boxSize, boxSize);
      if (chosen) painter->fillRect(box, colors.green);
      painter->setPen(chosen ? colors.green : colors.muted);
      painter->drawRect(box.adjusted(0, 0, -1, -1));
    }
    painter->setPen(!text || (conflict && decided && !chosen) ? colors.muted : colors.ink);
    painter->drawText(textRect, static_cast<int>(((text ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter).toInt()),
                      index.data(Qt::DisplayRole).toString());

    if (option.state.testFlag(QStyle::State_Selected) && text) {
      QColor selection = colors.green;
      selection.setAlpha(28);
      painter->fillRect(option.rect, selection);
    }
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const override {
    const QFontMetrics metrics(font_);
    const int width = isTextColumn(index.column())
        ? textInset + 8 + metrics.horizontalAdvance(index.data(Qt::DisplayRole).toString())
        : numberWidth;
    return {width, std::max(rowHeight, metrics.height() + 4)};
  }

  bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem&,
                   const QModelIndex& index) override {
    if (!(index.flags() & Qt::ItemIsUserCheckable)) return false;
    const auto type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease && type != QEvent::MouseButtonDblClick) return false;
    // Shift and Ctrl clicks extend the selection instead of toggling a line.
    const auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->modifiers() != Qt::NoModifier || mouse->button() != Qt::LeftButton) return false;
    // The press still selects the line and moves the current block. A drag
    // that ends on another line selects lines without toggling any.
    if (type == QEvent::MouseButtonPress) pressed_ = index;
    if (type != QEvent::MouseButtonRelease) return type == QEvent::MouseButtonDblClick;
    if (std::exchange(pressed_, {}) != index) return false;
    const bool checked = index.data(Qt::CheckStateRole).value<Qt::CheckState>() == Qt::Checked;
    return model->setData(index, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
  }

 private:
  QFont font_;
  QPersistentModelIndex pressed_;
};

// Space toggles every selected line on the side of the current cell.
class ConflictTable final : public QTableView {
 public:
  using QTableView::QTableView;

 protected:
  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() != Qt::Key_Space || event->modifiers() != Qt::NoModifier) {
      QTableView::keyPressEvent(event);
      return;
    }
    const auto current = currentIndex();
    if (!(current.flags() & Qt::ItemIsUserCheckable)) return;
    const auto state = current.data(Qt::CheckStateRole).value<Qt::CheckState>() == Qt::Checked ? Qt::Unchecked : Qt::Checked;
    auto indexes = selectionModel()->selectedIndexes();
    if (!indexes.contains(current)) indexes = {current};
    for (const auto& index : indexes)
      if (index.column() == current.column() && (index.flags() & Qt::ItemIsUserCheckable))
        model()->setData(index, state, Qt::CheckStateRole);
  }
};

}  // namespace

ConflictSides conflictSides(const Repository& repository) {
  const auto current = repository.branch.isEmpty() || repository.branch == QStringLiteral("detached HEAD")
      ? ConflictResolverDialog::tr("Current branch") : repository.branch;
  const auto& operation = repository.pendingOperation;
  if (operation == QStringLiteral("rebase"))
    return {ConflictResolverDialog::tr("Branch rebased onto"), ConflictResolverDialog::tr("Reapplied commit")};
  if (operation == QStringLiteral("cherry-pick")) return {current, ConflictResolverDialog::tr("Cherry-picked commit")};
  if (operation == QStringLiteral("revert")) return {current, ConflictResolverDialog::tr("Reverted version")};
  return {current, ConflictResolverDialog::tr("Merged branch")};
}

ConflictLinesModel::ConflictLinesModel(QObject* parent) : QAbstractTableModel(parent) {}

int ConflictLinesModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int ConflictLinesModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : columnCountValue;
}

const QString* ConflictLinesModel::lineText(const Row& row, const bool incoming) const {
  const auto& segment = file_.segments.at(row.segment);
  const auto line = incoming ? row.incoming : row.current;
  if (line < 0) return nullptr;
  if (!segment.conflict) return &segment.lines.at(line);
  return &(incoming ? segment.incoming : segment.current).at(line);
}

QVariant ConflictLinesModel::data(const QModelIndex& index, const int role) const {
  if (!index.isValid() || index.row() >= rows_.size()) return {};
  const auto& row = rows_.at(index.row());
  const bool incoming = index.column() >= incomingNumberColumn;
  const auto* text = lineText(row, incoming);
  const bool chosen = row.block >= 0 && text &&
      (incoming ? choices_.at(row.block).incoming : choices_.at(row.block).current).value(incoming ? row.incoming : row.current);
  switch (role) {
    case Qt::DisplayRole:
      if (isTextColumn(index.column())) return text ? withoutEnding(*text) : QString{};
      if (const auto number = incoming ? row.incomingNumber : row.currentNumber; number > 0) return number;
      return {};
    case Qt::ToolTipRole:
      return isTextColumn(index.column()) && text ? withoutEnding(*text) : QVariant{};
    case Qt::CheckStateRole:
      if (row.block < 0 || !text || !isTextColumn(index.column())) return {};
      return chosen ? Qt::Checked : Qt::Unchecked;
    case Qt::AccessibleTextRole: {
      if (!isTextColumn(index.column()) || !text) return {};
      const auto side = incoming ? sides_.incoming : sides_.current;
      const auto number = incoming ? row.incomingNumber : row.currentNumber;
      auto description = tr("%1, line %2: %3").arg(side).arg(number).arg(withoutEnding(*text));
      if (row.block >= 0) description += chosen ? tr(", in the result") : tr(", not in the result");
      return description;
    }
    case blockRole: return row.block;
    case lineRole: return text != nullptr;
    case chosenRole: return chosen;
    case decidedRole: return row.block >= 0 && choices_.at(row.block).decided;
    default: return {};
  }
}

QVariant ConflictLinesModel::headerData(const int section, const Qt::Orientation orientation, const int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
  if (section == currentTextColumn) return sides_.current;
  if (section == incomingTextColumn) return sides_.incoming;
  return {};
}

Qt::ItemFlags ConflictLinesModel::flags(const QModelIndex& index) const {
  if (!index.isValid()) return Qt::NoItemFlags;
  const auto& row = rows_.at(index.row());
  Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  if (row.block >= 0 && isTextColumn(index.column()) && lineText(row, index.column() == incomingTextColumn))
    result |= Qt::ItemIsUserCheckable;
  return result;
}

bool ConflictLinesModel::setData(const QModelIndex& index, const QVariant& value, const int role) {
  if (role != Qt::CheckStateRole || !(flags(index) & Qt::ItemIsUserCheckable)) return false;
  const auto& row = rows_.at(index.row());
  auto& choice = choices_[row.block];
  const bool incoming = index.column() == incomingTextColumn;
  (incoming ? choice.incoming : choice.current)[incoming ? row.incoming : row.current] =
      value.value<Qt::CheckState>() == Qt::Checked;
  choice.decided = true;
  blockChanged(row.block);
  return true;
}

void ConflictLinesModel::setFile(const ConflictFile& file) {
  beginResetModel();
  file_ = file;
  rows_.clear();
  blockRows_.clear();
  blockSegments_.clear();
  choices_.clear();
  qint64 current = 1;
  qint64 incoming = 1;
  for (int segmentIndex = 0; segmentIndex < file_.segments.size(); ++segmentIndex) {
    const auto& segment = file_.segments.at(segmentIndex);
    if (!segment.conflict) {
      for (int line = 0; line < segment.lines.size(); ++line)
        rows_.append({segmentIndex, -1, line, line, current++, incoming++});
      continue;
    }
    const auto block = static_cast<int>(choices_.size());
    blockRows_.append(static_cast<int>(rows_.size()));
    blockSegments_.append(segmentIndex);
    choices_.append({QList<bool>(segment.current.size(), false), QList<bool>(segment.incoming.size(), false)});
    // A block where both sides are empty still gets one row to show it.
    const auto height = std::max<qsizetype>({segment.current.size(), segment.incoming.size(), 1});
    for (int line = 0; line < height; ++line) {
      const bool hasCurrent = line < segment.current.size();
      const bool hasIncoming = line < segment.incoming.size();
      rows_.append({segmentIndex, block, hasCurrent ? line : -1, hasIncoming ? line : -1,
                    hasCurrent ? current++ : 0, hasIncoming ? incoming++ : 0});
    }
  }
  endResetModel();
  emit choicesChanged();
}

void ConflictLinesModel::setSides(const ConflictSides& sides) {
  sides_ = sides;
  emit headerDataChanged(Qt::Horizontal, 0, columnCountValue - 1);
}

const ConflictFile& ConflictLinesModel::file() const noexcept { return file_; }
const QList<ConflictChoice>& ConflictLinesModel::choices() const noexcept { return choices_; }
int ConflictLinesModel::blockCount() const noexcept { return static_cast<int>(choices_.size()); }

int ConflictLinesModel::decidedCount() const {
  return static_cast<int>(std::count_if(choices_.cbegin(), choices_.cend(), [](const auto& choice) { return choice.decided; }));
}

int ConflictLinesModel::firstRow(const int block) const { return blockRows_.value(block, -1); }

int ConflictLinesModel::blockAt(const int row) const {
  return row >= 0 && row < rows_.size() ? rows_.at(row).block : -1;
}

QString ConflictLinesModel::result() const { return composeConflictResolution(file_.segments, choices_); }

void ConflictLinesModel::chooseBlock(const int block, const bool current, const bool incoming) {
  if (block < 0 || block >= choices_.size()) return;
  auto& choice = choices_[block];
  std::fill(choice.current.begin(), choice.current.end(), current);
  std::fill(choice.incoming.begin(), choice.incoming.end(), incoming);
  choice.decided = true;
  blockChanged(block);
}

void ConflictLinesModel::setIncomingFirst(const int block, const bool incomingFirst) {
  if (block < 0 || block >= choices_.size() || choices_.at(block).incomingFirst == incomingFirst) return;
  choices_[block].incomingFirst = incomingFirst;
  blockChanged(block);
}

void ConflictLinesModel::resetBlock(const int block) {
  if (block < 0 || block >= choices_.size()) return;
  auto& choice = choices_[block];
  std::fill(choice.current.begin(), choice.current.end(), false);
  std::fill(choice.incoming.begin(), choice.incoming.end(), false);
  choice.decided = false;
  choice.incomingFirst = false;
  blockChanged(block);
}

void ConflictLinesModel::blockChanged(const int block) {
  const auto& segment = file_.segments.at(blockSegments_.at(block));
  const auto height = std::max<qsizetype>({segment.current.size(), segment.incoming.size(), 1});
  const auto first = blockRows_.at(block);
  emit dataChanged(index(first, 0), index(static_cast<int>(first + height - 1), columnCountValue - 1));
  emit choicesChanged();
}

ConflictResolverDialog::ConflictResolverDialog(const QString& filePath, QWidget* parent)
    : QDialog(parent), filePath_(filePath), model_(new ConflictLinesModel(this)) {
  setObjectName(QStringLiteral("conflictResolverDialog"));
  setWindowTitle(tr("Resolve %1").arg(filePath));
  resize(1100, 720);
  auto* layout = new QVBoxLayout(this);

  auto* explanation = new QLabel(tr("Choose the lines that go into the result: click a line, or select several and press Space. "
                                    "Lines keep the order shown, left side first unless the block is set to right side first."), this);
  explanation->setWordWrap(true);
  layout->addWidget(explanation);

  auto* tools = new QHBoxLayout;
  tools->setSpacing(4);
  position_ = new QLabel(this);
  position_->setObjectName(QStringLiteral("conflictPosition"));
  previous_ = new QPushButton(tr("Previous conflict"), this);
  next_ = new QPushButton(tr("Next conflict"), this);
  useCurrent_ = new QPushButton(tr("Use left"), this);
  useCurrent_->setObjectName(QStringLiteral("useCurrentBlock"));
  useIncoming_ = new QPushButton(tr("Use right"), this);
  useIncoming_->setObjectName(QStringLiteral("useIncomingBlock"));
  useBoth_ = new QPushButton(tr("Use both"), this);
  useBoth_->setObjectName(QStringLiteral("useBothBlocks"));
  useNeither_ = new QPushButton(tr("Use neither"), this);
  incomingFirst_ = new QCheckBox(tr("Right side first"), this);
  incomingFirst_->setObjectName(QStringLiteral("incomingFirst"));
  reset_ = new QPushButton(tr("Undecide"), this);
  reset_->setToolTip(tr("Return this block to undecided; it keeps its conflict markers."));
  tools->addWidget(position_);
  tools->addWidget(previous_);
  tools->addWidget(next_);
  tools->addSpacing(12);
  for (auto* button : {useCurrent_, useIncoming_, useBoth_, useNeither_}) tools->addWidget(button);
  tools->addWidget(incomingFirst_);
  tools->addWidget(reset_);
  tools->addStretch();
  layout->addLayout(tools);

  auto* splitter = new QSplitter(Qt::Vertical, this);
  table_ = new ConflictTable(splitter);
  table_->setObjectName(QStringLiteral("conflictLines"));
  table_->setAccessibleName(tr("Conflicting lines"));
  table_->setModel(model_);
  table_->setItemDelegate(new ConflictLineDelegate(table_));
  table_->setShowGrid(false);
  table_->setWordWrap(false);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setSelectionBehavior(QAbstractItemView::SelectItems);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setMouseTracking(true);
  table_->verticalHeader()->hide();
  table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  table_->verticalHeader()->setDefaultSectionSize(std::max(rowHeight, QFontMetrics(theme::codeFont()).height() + 4));
  table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  auto* header = table_->horizontalHeader();
  header->setSectionResizeMode(QHeaderView::Fixed);
  header->setSectionResizeMode(ConflictLinesModel::currentTextColumn, QHeaderView::Stretch);
  header->setSectionResizeMode(ConflictLinesModel::incomingTextColumn, QHeaderView::Stretch);
  header->resizeSection(ConflictLinesModel::currentNumberColumn, numberWidth);
  header->resizeSection(ConflictLinesModel::incomingNumberColumn, numberWidth);
  header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  header->setHighlightSections(false);

  auto* resultPanel = new QWidget(splitter);
  auto* resultLayout = new QVBoxLayout(resultPanel);
  resultLayout->setContentsMargins(0, 0, 0, 0);
  auto* resultHeader = new QHBoxLayout;
  resultHeader->addWidget(new QLabel(tr("Result"), resultPanel));
  resultHeader->addStretch();
  editResult_ = new QPushButton(tr("Edit result"), resultPanel);
  editResult_->setObjectName(QStringLiteral("editConflictResult"));
  editResult_->setCheckable(true);
  editResult_->setToolTip(tr("Edit the result as text. Line choices are locked while you edit."));
  resultHeader->addWidget(editResult_);
  resultLayout->addLayout(resultHeader);
  result_ = new QPlainTextEdit(resultPanel);
  result_->setObjectName(QStringLiteral("conflictResult"));
  result_->setAccessibleName(tr("Result"));
  result_->setReadOnly(true);
  result_->setLineWrapMode(QPlainTextEdit::NoWrap);
  result_->setFont(theme::codeFont());
  resultLayout->addWidget(result_);
  splitter->addWidget(table_);
  splitter->addWidget(resultPanel);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 2);
  layout->addWidget(splitter, 1);

  status_ = new QLabel(tr("Reading the file…"), this);
  status_->setObjectName(QStringLiteral("conflictStatus"));
  layout->addWidget(status_);
  error_ = new QLabel(this);
  error_->setObjectName(QStringLiteral("conflictResolverError"));
  error_->setTextFormat(Qt::PlainText);
  error_->setWordWrap(true);
  error_->setProperty("role", QStringLiteral("error"));
  error_->hide();
  layout->addWidget(error_);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  save_ = buttons->addButton(tr("Save and mark resolved"), QDialogButtonBox::AcceptRole);
  save_->setObjectName(QStringLiteral("saveConflictResolution"));
  layout->addWidget(buttons);

  connect(model_, &ConflictLinesModel::choicesChanged, this, [this] {
    refreshResult();
    updateState();
  });
  connect(table_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
    if (const auto block = model_->blockAt(current.row()); block >= 0 && block != block_) {
      block_ = block;
      updateState();
    }
  });
  connect(previous_, &QPushButton::clicked, this, [this] { showBlock(block_ - 1); });
  connect(next_, &QPushButton::clicked, this, [this] { showBlock(block_ + 1); });
  connect(useCurrent_, &QPushButton::clicked, this, [this] { model_->chooseBlock(block_, true, false); });
  connect(useIncoming_, &QPushButton::clicked, this, [this] { model_->chooseBlock(block_, false, true); });
  connect(useBoth_, &QPushButton::clicked, this, [this] { model_->chooseBlock(block_, true, true); });
  connect(useNeither_, &QPushButton::clicked, this, [this] { model_->chooseBlock(block_, false, false); });
  connect(reset_, &QPushButton::clicked, this, [this] { model_->resetBlock(block_); });
  connect(incomingFirst_, &QCheckBox::toggled, this, [this](const bool checked) { model_->setIncomingFirst(block_, checked); });
  connect(editResult_, &QPushButton::toggled, this, [this](const bool checked) { setManualEditing(checked); });
  connect(save_, &QPushButton::clicked, this, [this] {
    error_->hide();
    auto content = resultText();
    emit saveRequested(filePath_, model_->file().contentHash, content);
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &ConflictResolverDialog::reject);
  updateState();
}

QString ConflictResolverDialog::filePath() const { return filePath_; }

void ConflictResolverDialog::setCodeFontSize(const int pixels) {
  auto font = theme::codeFont();
  font.setPixelSize(pixels);
  static_cast<ConflictLineDelegate*>(table_->itemDelegate())->setCodeFont(font);
  table_->verticalHeader()->setDefaultSectionSize(std::max(rowHeight, QFontMetrics(font).height() + 4));
  result_->setFont(font);
  table_->viewport()->update();
}
ConflictLinesModel* ConflictResolverDialog::linesModel() const noexcept { return model_; }

void ConflictResolverDialog::setRepository(const Repository& repository) {
  model_->setSides(conflictSides(repository));
  if (!repository.conflictedFiles.contains(filePath_)) accept();
}

void ConflictResolverDialog::setFile(const ConflictFile& file) {
  if (file.path != filePath_) return;
  loaded_ = true;
  error_->hide();
  // Text edited by hand gets the file's line endings, judged by its first line.
  lineEndingsCrLf_ = [&file] {
    for (const auto& segment : file.segments)
      for (const auto* lines : {&segment.lines, &segment.current, &segment.incoming})
        if (!lines->isEmpty()) return lines->first().endsWith(QStringLiteral("\r\n"));
    return false;
  }();
  model_->setFile(file);
  showBlock(model_->blockCount() > 0 ? 0 : -1);
}

void ConflictResolverDialog::setBusy(const bool busy) {
  busy_ = busy;
  updateState();
}

void ConflictResolverDialog::showError(const QString& message) {
  error_->setText(message);
  error_->show();
  updateState();
}

QString ConflictResolverDialog::resultText() const {
  if (!editResult_->isChecked()) return model_->result();
  auto text = result_->toPlainText();
  if (lineEndingsCrLf_) text.replace(u'\n', QStringLiteral("\r\n"));
  return text;
}

void ConflictResolverDialog::reject() {
  const bool changed = editResult_->isChecked() || model_->decidedCount() > 0;
  if (changed && !confirm(this, tr("Close without saving"), tr("Close and discard your choices for this file?"))) return;
  QDialog::reject();
}

void ConflictResolverDialog::showBlock(const int block) {
  if (block < 0 || block >= model_->blockCount()) {
    updateState();
    return;
  }
  block_ = block;
  const auto index = model_->index(model_->firstRow(block), ConflictLinesModel::currentTextColumn);
  table_->setCurrentIndex(index);
  table_->scrollTo(index, QAbstractItemView::PositionAtCenter);
  updateState();
}

void ConflictResolverDialog::updateState() {
  const auto blocks = model_->blockCount();
  const bool hasBlock = block_ >= 0 && block_ < blocks;
  const bool manual = editResult_->isChecked();
  const bool choosing = loaded_ && hasBlock && !manual;
  position_->setText(hasBlock ? tr("Conflict %1 of %2").arg(block_ + 1).arg(blocks) : QString{});
  previous_->setEnabled(choosing && block_ > 0);
  next_->setEnabled(choosing && block_ + 1 < blocks);
  for (auto* button : {useCurrent_, useIncoming_, useBoth_, useNeither_}) button->setEnabled(choosing);
  incomingFirst_->setEnabled(choosing);
  {
    const QSignalBlocker blocker(incomingFirst_);
    incomingFirst_->setChecked(hasBlock && model_->choices().at(block_).incomingFirst);
  }
  reset_->setEnabled(choosing);
  table_->setEnabled(loaded_ && !manual);
  editResult_->setEnabled(loaded_);
  const auto decided = model_->decidedCount();
  if (!loaded_) status_->setText(error_->isVisible() ? QString{} : tr("Reading the file…"));
  else if (manual) status_->setText(tr("Editing the result as text. It must not contain conflict markers."));
  else if (blocks == 0) status_->setText(tr("No conflict markers remain. Save to mark the file resolved."));
  else status_->setText(tr("%1 of %2 conflicts decided.").arg(decided).arg(blocks));
  save_->setEnabled(loaded_ && !busy_ && (manual || decided == blocks));
}

void ConflictResolverDialog::refreshResult() {
  if (editResult_->isChecked()) return;
  const auto scroll = result_->verticalScrollBar()->value();
  result_->setPlainText(model_->result());
  result_->verticalScrollBar()->setValue(scroll);
}

void ConflictResolverDialog::setManualEditing(const bool editing) {
  if (!editing && result_->document()->isModified() &&
      !confirm(this, tr("Discard text edits"), tr("Discard your edits to the result and return to choosing lines?"))) {
    const QSignalBlocker blocker(editResult_);
    editResult_->setChecked(true);
    return;
  }
  result_->setReadOnly(!editing);
  result_->document()->setModified(false);
  if (!editing) refreshResult();
  else result_->setFocus();
  updateState();
}

}  // namespace relay
