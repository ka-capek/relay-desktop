#include "relay/list_models.hpp"

#include <QCollator>
#include <QCoreApplication>
#include <QLocale>
#include <QMimeData>
#include <QVariant>

#include <algorithm>
#include <limits>
#include <utility>

namespace relay {
namespace {

int boundedSize(qsizetype size) {
  return static_cast<int>(
      std::min(size, static_cast<qsizetype>(std::numeric_limits<int>::max())));
}

template <typename T>
const T* itemAt(const QList<T>& items, int row) noexcept {
  if (row < 0 || static_cast<qsizetype>(row) >= items.size()) {
    return nullptr;
  }
  return &items.at(row);
}

QVariant optionalDate(const std::optional<QDateTime>& value) {
  return value && value->isValid() ? QVariant::fromValue(*value) : QVariant{};
}

QString statusName(FileStatus status) {
  switch (status) {
    case FileStatus::added:
      return QCoreApplication::translate("ListModels", "Added");
    case FileStatus::modified:
      return QCoreApplication::translate("ListModels", "Modified");
    case FileStatus::deleted:
      return QCoreApplication::translate("ListModels", "Deleted");
  }
  return QCoreApplication::translate("ListModels", "Modified");
}

QHash<int, QByteArray> fileRoleNames(int pathRole, int nameRole, int directoryRole,
                                    int statusCodeRole, int statusToneRole,
                                    int addedCountRole, int removedCountRole,
                                    int binaryRole) {
  return {
      {pathRole, QByteArrayLiteral("path")},
      {nameRole, QByteArrayLiteral("name")},
      {directoryRole, QByteArrayLiteral("directory")},
      {statusCodeRole, QByteArrayLiteral("statusCode")},
      {statusToneRole, QByteArrayLiteral("statusTone")},
      {addedCountRole, QByteArrayLiteral("addedCount")},
      {removedCountRole, QByteArrayLiteral("removedCount")},
      {binaryRole, QByteArrayLiteral("binary")},
  };
}

QVariant fileData(const ChangedFile& file, int role, bool includeCheckState,
                  bool checked) {
  switch (role) {
    case Qt::DisplayRole:
      return file.name;
    case Qt::ToolTipRole:
      return file.path;
    case Qt::AccessibleTextRole: {
      QString description = QCoreApplication::translate(
                                "ListModels", "%1, %2, %3 additions, %4 removals")
                                .arg(file.path, statusName(file.status))
                                .arg(file.added)
                                .arg(file.removed);
      if (file.binary) {
        description.append(QCoreApplication::translate("ListModels", ", binary file"));
      }
      if (includeCheckState) {
        description.append(checked
                               ? QCoreApplication::translate("ListModels", ", included in commit")
                               : QCoreApplication::translate("ListModels", ", excluded from commit"));
      }
      return description;
    }
    case Qt::CheckStateRole:
      return includeCheckState ? QVariant::fromValue(checked ? Qt::Checked : Qt::Unchecked)
                               : QVariant{};
    default:
      return {};
  }
}

QVariant fileCustomData(const ChangedFile& file, int role, int pathRole, int nameRole,
                        int directoryRole, int statusCodeRole, int statusToneRole,
                        int addedCountRole, int removedCountRole, int binaryRole) {
  if (role == pathRole) return file.path;
  if (role == nameRole) return file.name;
  if (role == directoryRole) return file.directory;
  if (role == statusCodeRole) return fileStatusCode(file.status);
  if (role == statusToneRole) return fileStatusTone(file.status);
  if (role == addedCountRole) return QVariant::fromValue(static_cast<qlonglong>(file.added));
  if (role == removedCountRole) return QVariant::fromValue(static_cast<qlonglong>(file.removed));
  if (role == binaryRole) return file.binary;
  return {};
}

}  // namespace

RepositoryListModel::RepositoryListModel(QObject* parent) : QAbstractListModel(parent) {
  collator_.setCaseSensitivity(Qt::CaseInsensitive);
  collator_.setNumericMode(true);
}

int RepositoryListModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : boundedSize(visibleIndices_.size());
}

QVariant RepositoryListModel::data(const QModelIndex& index, int role) const {
  const RepositorySummary* repository = repositoryAt(index.row());
  if (!index.isValid() || index.column() != 0 || repository == nullptr) {
    return {};
  }

  switch (role) {
    case Qt::DisplayRole:
      return repository->name;
    case Qt::ToolTipRole:
      return repository->path;
    case Qt::AccessibleTextRole:
      return tr("%1/%2, branch %3, %4 changed files")
          .arg(repository->owner, repository->name, repository->branch)
          .arg(repository->changes);
    case pathRole:
      return repository->path;
    case nameRole:
      return repository->name;
    case ownerRole:
      return repository->owner;
    case branchRole:
      return repository->branch;
    case changeCountRole:
      return QVariant::fromValue(static_cast<qlonglong>(repository->changes));
    case lastOpenedRole:
      return optionalDate(repository->lastOpened);
    case addedAtRole:
      return optionalDate(repository->addedAt);
    case latestCommitRole:
      return optionalDate(repository->latestCommit);
    case firstCommitRole:
      return optionalDate(repository->firstCommit);
    case pinnedAccountIdRole:
      return pinnedAccountIds_.value(repository->path);
    case reorderPositionRole:
      return index.row() + 1;
    default:
      return {};
  }
}

Qt::ItemFlags RepositoryListModel::flags(const QModelIndex& index) const {
  if (!index.isValid()) {
    return canManuallyReorder() ? Qt::ItemIsDropEnabled : Qt::NoItemFlags;
  }
  Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  if (canManuallyReorder()) {
    result |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
  }
  return result;
}

Qt::DropActions RepositoryListModel::supportedDropActions() const {
  return Qt::MoveAction;
}

QStringList RepositoryListModel::mimeTypes() const {
  return {QStringLiteral("application/x-relay-repository-path")};
}

QMimeData* RepositoryListModel::mimeData(const QModelIndexList& indexes) const {
  auto* data = new QMimeData;
  if (!canManuallyReorder() || indexes.isEmpty()) return data;
  if (const auto* repository = repositoryAt(indexes.constFirst().row())) {
    data->setData(mimeTypes().constFirst(), repository->path.toUtf8());
  }
  return data;
}

bool RepositoryListModel::dropMimeData(const QMimeData* data, const Qt::DropAction action,
                                       int row, const int column,
                                       const QModelIndex& parent) {
  if (!canManuallyReorder() || data == nullptr || action != Qt::MoveAction ||
      column > 0 || !data->hasFormat(mimeTypes().constFirst()) || rowCount() < 2) {
    return false;
  }
  const QString repositoryPath = QString::fromUtf8(data->data(mimeTypes().constFirst()));
  if (row < 0 && parent.isValid()) row = parent.row();
  if (row < 0) row = rowCount();
  const bool placeAfter = row >= rowCount();
  const int targetRow = placeAfter ? rowCount() - 1 : row;
  const auto* target = repositoryAt(targetRow);
  return target != nullptr && moveRepository(repositoryPath, target->path, placeAfter);
}

QHash<int, QByteArray> RepositoryListModel::roleNames() const {
  auto roles = QAbstractListModel::roleNames();
  roles.insert(pathRole, QByteArrayLiteral("path"));
  roles.insert(nameRole, QByteArrayLiteral("name"));
  roles.insert(ownerRole, QByteArrayLiteral("owner"));
  roles.insert(branchRole, QByteArrayLiteral("branch"));
  roles.insert(changeCountRole, QByteArrayLiteral("changeCount"));
  roles.insert(lastOpenedRole, QByteArrayLiteral("lastOpened"));
  roles.insert(addedAtRole, QByteArrayLiteral("addedAt"));
  roles.insert(latestCommitRole, QByteArrayLiteral("latestCommit"));
  roles.insert(firstCommitRole, QByteArrayLiteral("firstCommit"));
  roles.insert(pinnedAccountIdRole, QByteArrayLiteral("pinnedAccountId"));
  roles.insert(reorderPositionRole, QByteArrayLiteral("reorderPosition"));
  return roles;
}

void RepositoryListModel::setRepositories(QList<RepositorySummary> repositories) {
  beginResetModel();
  repositories_ = std::move(repositories);
  rebuildVisible();
  endResetModel();
}

void RepositoryListModel::setFilter(QString filter) {
  filter = filter.trimmed();
  if (filter_ == filter) {
    return;
  }
  beginResetModel();
  filter_ = std::move(filter);
  rebuildVisible();
  endResetModel();
}

void RepositoryListModel::setOrder(RepositoryOrder order, QStringList manualOrder) {
  beginResetModel();
  order_ = order;
  manualOrder_ = std::move(manualOrder);
  rebuildVisible();
  endResetModel();
}

void RepositoryListModel::setOrder(RepositoryOrder order) {
  setOrder(order, manualOrder_);
}

void RepositoryListModel::setManualOrder(QStringList manualOrder) {
  setOrder(order_, std::move(manualOrder));
}

void RepositoryListModel::setPinnedAccountIds(
    QHash<QString, QString> pinnedAccountIds) {
  pinnedAccountIds_ = std::move(pinnedAccountIds);
  if (!visibleIndices_.isEmpty()) {
    emit dataChanged(index(0), index(rowCount() - 1), {pinnedAccountIdRole});
  }
}

const QList<RepositorySummary>& RepositoryListModel::repositories() const noexcept {
  return repositories_;
}

const RepositorySummary* RepositoryListModel::repositoryAt(int row) const noexcept {
  if (row < 0 || static_cast<qsizetype>(row) >= visibleIndices_.size()) {
    return nullptr;
  }
  return itemAt(repositories_, visibleIndices_.at(row));
}

const QString& RepositoryListModel::filter() const noexcept { return filter_; }

RepositoryOrder RepositoryListModel::order() const noexcept { return order_; }

const QStringList& RepositoryListModel::manualOrder() const noexcept {
  return manualOrder_;
}

bool RepositoryListModel::canManuallyReorder() const noexcept {
  return order_.mode == RepositoryOrderMode::manual && filter_.isEmpty();
}

bool RepositoryListModel::moveRepository(const QString& repositoryPath,
                                         const QString& targetPath,
                                         bool placeAfter) {
  if (!canManuallyReorder() || repositoryPath == targetPath) {
    return false;
  }

  QStringList paths;
  paths.reserve(visibleIndices_.size());
  for (const int sourceIndex : visibleIndices_) {
    paths.append(repositories_.at(sourceIndex).path);
  }
  const qsizetype from = paths.indexOf(repositoryPath);
  if (from < 0 || !paths.contains(targetPath)) {
    return false;
  }

  paths.removeAt(from);
  const qsizetype target = paths.indexOf(targetPath);
  if (target < 0) {
    return false;
  }
  paths.insert(target + (placeAfter ? 1 : 0), repositoryPath);

  beginResetModel();
  manualOrder_ = std::move(paths);
  rebuildVisible();
  endResetModel();
  emit manualOrderChanged(manualOrder_);
  return true;
}

void RepositoryListModel::rebuildVisible() {
  visibleIndices_.clear();
  visibleIndices_.reserve(repositories_.size());
  for (qsizetype sourceIndex = 0; sourceIndex < repositories_.size(); ++sourceIndex) {
    const RepositorySummary& repository = repositories_.at(sourceIndex);
    const QString searchable =
        repository.owner + QChar{u'/'} + repository.name + QChar{u' '} + repository.path;
    if (filter_.isEmpty() || searchable.contains(filter_, Qt::CaseInsensitive)) {
      visibleIndices_.append(static_cast<int>(sourceIndex));
    }
  }

  QHash<QString, qsizetype> manualPositions;
  manualPositions.reserve(manualOrder_.size());
  for (qsizetype position = 0; position < manualOrder_.size(); ++position) {
    manualPositions.insert(manualOrder_.at(position), position);
  }
  const qsizetype missingPosition = std::numeric_limits<qsizetype>::max();
  const RepositoryOrder order = order_;

  std::stable_sort(visibleIndices_.begin(), visibleIndices_.end(),
                   [this, &manualPositions, missingPosition, order](int leftIndex,
                                                                   int rightIndex) {
    const RepositorySummary& left = repositories_.at(leftIndex);
    const RepositorySummary& right = repositories_.at(rightIndex);
    if (order.mode == RepositoryOrderMode::manual) {
      const qsizetype leftPosition = manualPositions.value(left.path, missingPosition);
      const qsizetype rightPosition = manualPositions.value(right.path, missingPosition);
      if (leftPosition != rightPosition) return leftPosition < rightPosition;
      return compareByName(left, right) < 0;
    }

    if (order.mode == RepositoryOrderMode::name) {
      const int nameComparison = collator_.compare(left.name, right.name);
      if (nameComparison != 0) {
        return order.direction == SortDirection::ascending ? nameComparison < 0
                                                           : nameComparison > 0;
      }
      // Electron always uses the same ascending name/path tie-breaker, even
      // when the primary name ordering is descending.
      return compareByName(left, right) < 0;
    }

    const std::optional<QDateTime>& leftDate =
        order.mode == RepositoryOrderMode::age ? left.firstCommit : left.latestCommit;
    const std::optional<QDateTime>& rightDate =
        order.mode == RepositoryOrderMode::age ? right.firstCommit : right.latestCommit;
    const bool leftValid = leftDate && leftDate->isValid();
    const bool rightValid = rightDate && rightDate->isValid();
    if (leftValid != rightValid) return leftValid;
    if (leftValid && *leftDate != *rightDate) {
      return order.direction == SortDirection::ascending ? *leftDate < *rightDate
                                                         : *leftDate > *rightDate;
    }
    return compareByName(left, right) < 0;
  });
}

int RepositoryListModel::compareByName(const RepositorySummary& left,
                                       const RepositorySummary& right) const {
  int comparison = collator_.compare(left.name, right.name);
  if (comparison == 0) {
    comparison = collator_.compare(left.path, right.path);
  }
  return comparison;
}

ChangedFileListModel::ChangedFileListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int ChangedFileListModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : boundedSize(files_.size());
}

QVariant ChangedFileListModel::data(const QModelIndex& index, int role) const {
  const ChangedFile* file = fileAt(index.row());
  if (!index.isValid() || index.column() != 0 || file == nullptr) {
    return {};
  }
  const QVariant standard = fileData(*file, role, true, checkedPaths_.contains(file->path));
  if (standard.isValid()) {
    return standard;
  }
  return fileCustomData(*file, role, pathRole, nameRole, directoryRole, statusCodeRole,
                        statusToneRole, addedCountRole, removedCountRole, binaryRole);
}

bool ChangedFileListModel::setData(const QModelIndex& index, const QVariant& value,
                                   int role) {
  const ChangedFile* file = fileAt(index.row());
  if (!index.isValid() || file == nullptr || role != Qt::CheckStateRole) {
    return false;
  }
  const bool checked = value.toInt() == Qt::Checked;
  const bool wasChecked = checkedPaths_.contains(file->path);
  if (checked == wasChecked) {
    return false;
  }
  if (checked) {
    checkedPaths_.insert(file->path);
  } else {
    checkedPaths_.remove(file->path);
  }
  emit dataChanged(index, index, {Qt::CheckStateRole, Qt::AccessibleTextRole});
  return true;
}

Qt::ItemFlags ChangedFileListModel::flags(const QModelIndex& index) const {
  if (!index.isValid()) {
    return Qt::NoItemFlags;
  }
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
}

QHash<int, QByteArray> ChangedFileListModel::roleNames() const {
  auto roles = QAbstractListModel::roleNames();
  const auto custom = fileRoleNames(pathRole, nameRole, directoryRole, statusCodeRole,
                                    statusToneRole, addedCountRole, removedCountRole,
                                    binaryRole);
  for (auto it = custom.cbegin(); it != custom.cend(); ++it) roles.insert(it.key(), it.value());
  return roles;
}

void ChangedFileListModel::setFiles(QList<ChangedFile> files, FileCheckPolicy policy) {
  const QSet<QString> previous = checkedPaths_;
  beginResetModel();
  files_ = std::move(files);
  checkedPaths_.clear();
  if (policy != FileCheckPolicy::checkNone) {
    for (const ChangedFile& file : files_) {
      if (policy == FileCheckPolicy::checkAll || previous.contains(file.path)) {
        checkedPaths_.insert(file.path);
      }
    }
  }
  endResetModel();
}

void ChangedFileListModel::setAllChecked(bool checked) {
  QSet<QString> next;
  if (checked) {
    next.reserve(files_.size());
    for (const ChangedFile& file : files_) next.insert(file.path);
  }
  if (next == checkedPaths_) {
    return;
  }
  checkedPaths_ = std::move(next);
  if (!files_.isEmpty()) {
    emit dataChanged(index(0), index(rowCount() - 1),
                     {Qt::CheckStateRole, Qt::AccessibleTextRole});
  }
}

const QList<ChangedFile>& ChangedFileListModel::files() const noexcept { return files_; }

const ChangedFile* ChangedFileListModel::fileAt(int row) const noexcept {
  return itemAt(files_, row);
}

QStringList ChangedFileListModel::checkedPaths() const {
  QStringList paths;
  paths.reserve(checkedPaths_.size());
  for (const ChangedFile& file : files_) {
    if (checkedPaths_.contains(file.path)) paths.append(file.path);
  }
  return paths;
}

qsizetype ChangedFileListModel::checkedCount() const noexcept {
  return checkedPaths_.size();
}

Qt::CheckState ChangedFileListModel::aggregateCheckState() const noexcept {
  if (files_.isEmpty() || checkedPaths_.isEmpty()) return Qt::Unchecked;
  return checkedPaths_.size() == files_.size() ? Qt::Checked : Qt::PartiallyChecked;
}

HistoryCommitListModel::HistoryCommitListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int HistoryCommitListModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : boundedSize(visibleIndices_.size());
}

QVariant HistoryCommitListModel::data(const QModelIndex& index, int role) const {
  const HistoryCommit* commit = commitAt(index.row());
  if (!index.isValid() || index.column() != 0 || commit == nullptr) {
    return {};
  }

  switch (role) {
    case Qt::DisplayRole:
      return commit->title;
    case Qt::ToolTipRole:
      return commit->fullHash;
    case Qt::AccessibleTextRole:
      return tr("%1, commit %2 by %3 on %4")
          .arg(commit->title, commit->hash, commit->author,
               QLocale{}.toString(commit->date.toLocalTime(), QLocale::LongFormat));
    case fullHashRole:
      return commit->fullHash;
    case shortHashRole:
      return commit->hash;
    case titleRole:
      return commit->title;
    case authorRole:
      return commit->author;
    case emailRole:
      return commit->email;
    case dateRole:
      return commit->date;
    case parentsRole:
      return commit->parents;
    case refsRole:
      return commit->refs;
    case dayLabelRole:
      return dayLabel(*commit);
    case startsDayGroupRole:
      return !(graphEnabled_ && search_.isEmpty()) && (index.row() == 0 || dayLabel(*commit) != dayLabel(*commitAt(index.row() - 1)));
    default:
      return {};
  }
}

Qt::ItemFlags HistoryCommitListModel::flags(const QModelIndex& index) const {
  return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}

QHash<int, QByteArray> HistoryCommitListModel::roleNames() const {
  auto roles = QAbstractListModel::roleNames();
  roles.insert(fullHashRole, QByteArrayLiteral("fullHash"));
  roles.insert(shortHashRole, QByteArrayLiteral("shortHash"));
  roles.insert(titleRole, QByteArrayLiteral("title"));
  roles.insert(authorRole, QByteArrayLiteral("author"));
  roles.insert(emailRole, QByteArrayLiteral("email"));
  roles.insert(dateRole, QByteArrayLiteral("date"));
  roles.insert(parentsRole, QByteArrayLiteral("parents"));
  roles.insert(refsRole, QByteArrayLiteral("refs"));
  roles.insert(dayLabelRole, QByteArrayLiteral("dayLabel"));
  roles.insert(startsDayGroupRole, QByteArrayLiteral("startsDayGroup"));
  return roles;
}

void HistoryCommitListModel::resetHistory(QList<HistoryCommit> commits, QString anchor,
                                          bool endOfHistory) {
  beginResetModel();
  commits_.clear();
  graphRows_.clear();
  graphLanes_.clear();
  graphColors_.clear();
  visibleIndices_.clear();
  hashes_.clear();
  anchor_ = std::move(anchor);
  endOfHistory_ = endOfHistory;
  hashes_.reserve(commits.size());
  commits_.reserve(commits.size());
  for (HistoryCommit& commit : commits) {
    if (hashes_.contains(commit.fullHash)) continue;
    hashes_.insert(commit.fullHash);
    appendGraph(commit);
    commits_.append(std::move(commit));
  }
  rebuildVisible();
  endResetModel();
}

void HistoryCommitListModel::resetPage(const HistoryPage& page) {
  resetHistory(page.commits, page.anchor, page.endOfHistory);
}

qsizetype HistoryCommitListModel::appendCommits(QList<HistoryCommit> commits) {
  QList<HistoryCommit> unique;
  unique.reserve(commits.size());
  QSet<QString> newHashes;
  newHashes.reserve(commits.size());
  for (HistoryCommit& commit : commits) {
    if (hashes_.contains(commit.fullHash) || newHashes.contains(commit.fullHash)) continue;
    newHashes.insert(commit.fullHash);
    unique.append(std::move(commit));
  }
  if (unique.isEmpty()) return 0;

  qsizetype visibleToAdd = 0;
  for (const HistoryCommit& commit : unique) {
    if (matchesSearch(commit)) ++visibleToAdd;
  }
  const int firstVisible = rowCount();
  if (visibleToAdd > 0) {
    beginInsertRows({}, firstVisible, firstVisible + boundedSize(visibleToAdd) - 1);
  }
  for (HistoryCommit& commit : unique) {
    const bool visible = matchesSearch(commit);
    hashes_.insert(commit.fullHash);
    appendGraph(commit);
    commits_.append(std::move(commit));
    if (visible) visibleIndices_.append(static_cast<int>(commits_.size() - 1));
  }
  if (visibleToAdd > 0) endInsertRows();
  return unique.size();
}

qsizetype HistoryCommitListModel::appendPage(const HistoryPage& page) {
  anchor_ = page.anchor;
  endOfHistory_ = page.endOfHistory;
  return appendCommits(page.commits);
}

void HistoryCommitListModel::clear() { resetHistory({}); }

void HistoryCommitListModel::setSearch(QString search) {
  search = search.trimmed();
  if (search_ == search) return;
  beginResetModel();
  search_ = std::move(search);
  rebuildVisible();
  endResetModel();
}

void HistoryCommitListModel::setGraphEnabled(bool enabled) {
  if (graphEnabled_ == enabled) return;
  beginResetModel();
  graphEnabled_ = enabled;
  graphRows_.clear();
  graphLanes_.clear();
  graphColors_.clear();
  if (enabled) for (const auto& commit : commits_) appendGraph(commit);
  endResetModel();
}

const HistoryGraphRow* HistoryCommitListModel::graphRowAt(int row) const {
  if (!graphEnabled_ || !search_.isEmpty() || row < 0 || row >= visibleIndices_.size()) return nullptr;
  return &graphRows_.at(visibleIndices_.at(row));
}

void HistoryCommitListModel::appendGraph(const HistoryCommit& commit) {
  if (!graphEnabled_) return;
  const auto before = graphLanes_;
  const auto availableColor = [this] {
    QSet<int> used;
    for (const int color : graphColors_) used.insert(color);
    int color = 0;
    while (used.contains(color)) ++color;
    return color;
  };
  if (!graphColors_.contains(commit.fullHash)) graphColors_.insert(commit.fullHash, availableColor());
  const int color = graphColors_.value(commit.fullHash);
  auto lane = graphLanes_.indexOf(commit.fullHash);
  const bool incoming = lane >= 0;
  if (lane < 0) { lane = graphLanes_.size(); graphLanes_.append(commit.fullHash); }
  graphLanes_[lane].clear();
  bool firstParent = true;
  for (const auto& parent : commit.parents) {
    if (!graphColors_.contains(parent)) graphColors_.insert(parent, firstParent ? color : availableColor());
    firstParent = false;
    if (graphLanes_.contains(parent)) continue;
    const auto empty = graphLanes_.indexOf(QString{});
    if (empty < 0) graphLanes_.append(parent);
    else graphLanes_[empty] = parent;
  }
  graphLanes_.removeAll(QString{});
  QHash<QString, int> destinations;
  for (qsizetype i = 0; i < graphLanes_.size(); ++i) destinations.insert(graphLanes_.at(i), static_cast<int>(i));
  HistoryGraphRow row;
  row.lane = static_cast<int>(lane);
  row.width = static_cast<int>(std::max({before.size(), graphLanes_.size(), lane + 1}));
  row.incoming = incoming;
  row.color = color;
  for (qsizetype index = 0; index < before.size(); ++index) {
    if (before.at(index) != commit.fullHash) {
      row.passing.append({static_cast<int>(index), destinations.value(before.at(index), -1)});
      row.passingColors.append(graphColors_.value(before.at(index)));
    }
  }
  for (const auto& parent : commit.parents) {
    row.parents.append(destinations.value(parent, -1));
    row.parentColors.append(row.parentColors.isEmpty() ? color : graphColors_.value(parent));
  }
  graphColors_.remove(commit.fullHash);
  graphRows_.append(std::move(row));
}

void HistoryCommitListModel::setPagingState(QString anchor, bool endOfHistory) {
  anchor_ = std::move(anchor);
  endOfHistory_ = endOfHistory;
}

const HistoryCommit* HistoryCommitListModel::commitAt(int row) const noexcept {
  if (row < 0 || static_cast<qsizetype>(row) >= visibleIndices_.size()) return nullptr;
  return itemAt(commits_, visibleIndices_.at(row));
}

const QList<HistoryCommit>& HistoryCommitListModel::commits() const noexcept {
  return commits_;
}

const QString& HistoryCommitListModel::search() const noexcept { return search_; }

const QString& HistoryCommitListModel::anchor() const noexcept { return anchor_; }

bool HistoryCommitListModel::endOfHistory() const noexcept { return endOfHistory_; }

bool HistoryCommitListModel::matchesSearch(const HistoryCommit& commit) const {
  if (search_.isEmpty()) return true;
  const QString searchable = commit.title + QChar{u' '} + commit.author + QChar{u' '} +
                             commit.email + QChar{u' '} + commit.fullHash;
  return searchable.contains(search_, Qt::CaseInsensitive);
}

QString HistoryCommitListModel::dayLabel(const HistoryCommit& commit) const {
  if (!commit.date.isValid()) return tr("Unknown date");
  return QLocale{}.toString(commit.date.toLocalTime().date(), QLocale::LongFormat);
}

void HistoryCommitListModel::rebuildVisible() {
  visibleIndices_.clear();
  visibleIndices_.reserve(commits_.size());
  for (qsizetype sourceIndex = 0; sourceIndex < commits_.size(); ++sourceIndex) {
    if (matchesSearch(commits_.at(sourceIndex))) {
      visibleIndices_.append(static_cast<int>(sourceIndex));
    }
  }
}

CommitFileListModel::CommitFileListModel(QObject* parent) : QAbstractListModel(parent) {}

int CommitFileListModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : boundedSize(files_.size());
}

QVariant CommitFileListModel::data(const QModelIndex& index, int role) const {
  const ChangedFile* file = fileAt(index.row());
  if (!index.isValid() || index.column() != 0 || file == nullptr) return {};
  const QVariant standard = fileData(*file, role, false, false);
  if (standard.isValid()) return standard;
  return fileCustomData(*file, role, pathRole, nameRole, directoryRole, statusCodeRole,
                        statusToneRole, addedCountRole, removedCountRole, binaryRole);
}

Qt::ItemFlags CommitFileListModel::flags(const QModelIndex& index) const {
  return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}

QHash<int, QByteArray> CommitFileListModel::roleNames() const {
  auto roles = QAbstractListModel::roleNames();
  const auto custom = fileRoleNames(pathRole, nameRole, directoryRole, statusCodeRole,
                                    statusToneRole, addedCountRole, removedCountRole,
                                    binaryRole);
  for (auto it = custom.cbegin(); it != custom.cend(); ++it) roles.insert(it.key(), it.value());
  return roles;
}

void CommitFileListModel::setFiles(QList<ChangedFile> files) {
  beginResetModel();
  files_ = std::move(files);
  endResetModel();
}

const QList<ChangedFile>& CommitFileListModel::files() const noexcept { return files_; }

const ChangedFile* CommitFileListModel::fileAt(int row) const noexcept {
  return itemAt(files_, row);
}

}  // namespace relay
