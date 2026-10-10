#include "relay/compare_dialog.hpp"

#include "relay/diff_view.hpp"
#include "relay/item_delegates.hpp"
#include "relay/list_models.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

namespace relay {
namespace {
QListView* commitList(HistoryCommitListModel* model, const QString& name, const QString& accessibleName, QWidget* parent) {
  auto* list = new QListView(parent);
  list->setObjectName(name);
  list->setAccessibleName(accessibleName);
  list->setModel(model);
  list->setItemDelegate(new HistoryCommitItemDelegate(list));
  list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  list->setMouseTracking(true);
  return list;
}

QString shortName(const QString& ref) {
  if (ref.startsWith(QStringLiteral("refs/heads/"))) return ref.mid(11);
  if (ref.startsWith(QStringLiteral("refs/remotes/"))) return ref.mid(13);
  return ref;
}
}  // namespace

CompareDialog::CompareDialog(QWidget* parent) : QDialog(parent) {
  setObjectName(QStringLiteral("compareDialog"));
  setWindowTitle(tr("Compare branches"));
  resize(980, 720);
  auto* layout = new QVBoxLayout(this);
  auto* choose = new QHBoxLayout;
  base_ = new QComboBox(this);
  base_->setObjectName(QStringLiteral("compareBase"));
  base_->setAccessibleName(tr("Base branch"));
  compare_ = new QComboBox(this);
  compare_->setObjectName(QStringLiteral("compareHead"));
  compare_->setAccessibleName(tr("Compared branch"));
  auto* swap = new QPushButton(tr("Swap"), this);
  swap->setObjectName(QStringLiteral("compareSwap"));
  choose->addWidget(new QLabel(tr("Base"), this));
  choose->addWidget(base_, 1);
  choose->addWidget(new QLabel(tr("Compare"), this));
  choose->addWidget(compare_, 1);
  choose->addWidget(swap);
  layout->addLayout(choose);
  summary_ = new QLabel(this);
  summary_->setObjectName(QStringLiteral("compareSummary"));
  summary_->setWordWrap(true);
  layout->addWidget(summary_);

  tabs_ = new QTabWidget(this);
  ahead_ = new HistoryCommitListModel(this);
  behind_ = new HistoryCommitListModel(this);
  tabs_->addTab(commitList(ahead_, QStringLiteral("compareAhead"), tr("Commits only in the compared branch"), tabs_), {});
  tabs_->addTab(commitList(behind_, QStringLiteral("compareBehind"), tr("Commits only in the base branch"), tabs_), {});
  auto* split = new QSplitter(Qt::Horizontal, tabs_);
  files_ = new CommitFileListModel(this);
  fileList_ = new QListView(split);
  fileList_->setObjectName(QStringLiteral("compareFiles"));
  fileList_->setAccessibleName(tr("Changed files"));
  fileList_->setModel(files_);
  fileList_->setItemDelegate(new CommitFileItemDelegate(fileList_));
  fileList_->setMouseTracking(true);
  diff_ = new DiffView(split);
  diff_->setObjectName(QStringLiteral("compareDiff"));
  split->addWidget(fileList_);
  split->addWidget(diff_);
  split->setStretchFactor(1, 3);
  tabs_->addTab(split, {});
  layout->addWidget(tabs_, 1);

  auto* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
  merge_ = box->addButton(tr("Merge into current branch"), QDialogButtonBox::ActionRole);
  merge_->setObjectName(QStringLiteral("compareMerge"));
  merge_->setVisible(false);
  layout->addWidget(box);
  connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(merge_, &QPushButton::clicked, this, [this] { emit mergeRequested(compare_->currentData().toString()); });
  connect(base_, &QComboBox::currentIndexChanged, this, &CompareDialog::request);
  connect(compare_, &QComboBox::currentIndexChanged, this, &CompareDialog::request);
  connect(swap, &QPushButton::clicked, this, [this] {
    selectBranches(compare_->currentData().toString(), base_->currentData().toString());
  });
  connect(fileList_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
    diff_->clearDiff();
    if (const auto* file = files_->fileAt(index.row())) emit fileDiffRequested(diffFrom_, diffTo_, file->path);
  });
}

void CompareDialog::setRepository(const Repository& repository) {
  currentBranch_ = repository.branch;
  const auto previousBase = base_->currentData().toString();
  const auto previousCompare = compare_->currentData().toString();
  {
    const QSignalBlocker baseBlocker(base_);
    const QSignalBlocker compareBlocker(compare_);
    base_->clear();
    compare_->clear();
    for (auto* box : {base_, compare_}) {
      for (const auto& branch : repository.branches) box->addItem(branch, QStringLiteral("refs/heads/") + branch);
      for (const auto& branch : repository.remoteBranches) box->addItem(tr("Remote · %1").arg(branch), QStringLiteral("refs/remotes/") + branch);
    }
  }
  selectBranches(previousBase.isEmpty() ? QStringLiteral("refs/heads/") + repository.branch : previousBase, previousCompare);
}

void CompareDialog::selectBranches(const QString& base, const QString& compare) {
  {
    const QSignalBlocker baseBlocker(base_);
    const QSignalBlocker compareBlocker(compare_);
    base_->setCurrentIndex(std::max(0, base_->findData(base)));
    auto index = compare_->findData(compare);
    // Default to another branch than the base, preferring its origin copy.
    if (index < 0) index = compare_->findData(QStringLiteral("refs/remotes/origin/") + shortName(base_->currentData().toString()));
    for (int candidate = 0; index < 0 && candidate < compare_->count(); ++candidate)
      if (compare_->itemData(candidate) != base_->currentData()) index = candidate;
    compare_->setCurrentIndex(std::max(0, index));
  }
  request();
}

void CompareDialog::request() {
  const auto base = base_->currentData().toString();
  const auto compare = compare_->currentData().toString();
  ahead_->clear();
  behind_->clear();
  files_->setFiles({});
  diff_->clearDiff();
  merge_->setVisible(false);
  diffFrom_.clear();
  diffTo_.clear();
  if (base.isEmpty() || compare.isEmpty() || base == compare) {
    summary_->setText(tr("Choose two different branches."));
    return;
  }
  summary_->setText(tr("Comparing…"));
  emit comparisonRequested(base, compare);
}

void CompareDialog::showComparison(const BranchComparison& comparison) {
  if (comparison.base != base_->currentData().toString() || comparison.compare != compare_->currentData().toString()) return;
  const auto base = shortName(comparison.base);
  const auto compare = shortName(comparison.compare);
  ahead_->resetHistory(comparison.ahead, {}, true);
  behind_->resetHistory(comparison.behind, {}, true);
  files_->setFiles(comparison.files);
  diffFrom_ = comparison.mergeBase.isEmpty() ? comparison.baseHash : comparison.mergeBase;
  diffTo_ = comparison.compareHash;
  const auto more = comparison.truncated ? QStringLiteral("+") : QString{};
  tabs_->setTabText(0, tr("%1%2 only in %3").arg(comparison.ahead.size()).arg(more, compare));
  tabs_->setTabText(1, tr("%1%2 only in %3").arg(comparison.behind.size()).arg(more, base));
  tabs_->setTabText(2, tr("%n changed file(s)", nullptr, int(comparison.files.size())));
  if (comparison.ahead.isEmpty() && comparison.behind.isEmpty()) summary_->setText(tr("%1 and %2 point to the same history.").arg(compare, base));
  else if (comparison.mergeBase.isEmpty()) summary_->setText(tr("%1 and %2 have no common history.").arg(compare, base));
  else summary_->setText(tr("%1 compared with %2: %3%5 ahead, %4%5 behind. Changes since %1 branched off: +%6 −%7.")
                             .arg(compare, base).arg(comparison.ahead.size()).arg(comparison.behind.size())
                             .arg(more).arg(comparison.added).arg(comparison.removed));
  merge_->setText(tr("Merge %1 into %2").arg(compare, currentBranch_));
  merge_->setVisible(comparison.base == QStringLiteral("refs/heads/") + currentBranch_ && !comparison.ahead.isEmpty());
  if (!comparison.files.isEmpty()) fileList_->setCurrentIndex(files_->index(0));
}

void CompareDialog::showFileDiff(const QString& from, const QString& to, const QString& filePath, const QString& diff) {
  const auto* file = files_->fileAt(fileList_->currentIndex().row());
  if (from != diffFrom_ || to != diffTo_ || !file || file->path != filePath) return;
  diff_->setDiff(diff);
}

void CompareDialog::setDiffFontSize(const int pixels) { diff_->setCodeFontSize(pixels); }

}  // namespace relay
