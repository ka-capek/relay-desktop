#pragma once

#include "relay/domain.hpp"

#include <QAbstractListModel>
#include <QCollator>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

class QMimeData;

namespace relay {

class RepositoryListModel final : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Role {
    pathRole = Qt::UserRole + 1,
    nameRole,
    ownerRole,
    branchRole,
    changeCountRole,
    lastOpenedRole,
    addedAtRole,
    latestCommitRole,
    firstCommitRole,
    pinnedAccountIdRole,
    reorderPositionRole,
  };

  explicit RepositoryListModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index,
                              int role = Qt::DisplayRole) const override;
  [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
  [[nodiscard]] Qt::DropActions supportedDropActions() const override;
  [[nodiscard]] QStringList mimeTypes() const override;
  [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
  [[nodiscard]] bool dropMimeData(const QMimeData* data, Qt::DropAction action,
                                  int row, int column,
                                  const QModelIndex& parent) override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  void setRepositories(QList<RepositorySummary> repositories);
  void setFilter(QString filter);
  void setOrder(RepositoryOrder order, QStringList manualOrder);
  void setOrder(RepositoryOrder order);
  void setManualOrder(QStringList manualOrder);
  void setPinnedAccountIds(QHash<QString, QString> pinnedAccountIds);

  [[nodiscard]] const QList<RepositorySummary>& repositories() const noexcept;
  [[nodiscard]] const RepositorySummary* repositoryAt(int row) const noexcept;
  [[nodiscard]] const QString& filter() const noexcept;
  [[nodiscard]] RepositoryOrder order() const noexcept;
  [[nodiscard]] const QStringList& manualOrder() const noexcept;
  [[nodiscard]] bool canManuallyReorder() const noexcept;
  [[nodiscard]] bool moveRepository(const QString& repositoryPath,
                                    const QString& targetPath,
                                    bool placeAfter = false);

 signals:
  void manualOrderChanged(const QStringList& repositoryPaths);

 private:
  void rebuildVisible();
  [[nodiscard]] int compareByName(const RepositorySummary& left,
                                  const RepositorySummary& right) const;

  QList<RepositorySummary> repositories_;
  QList<int> visibleIndices_;
  QString filter_;
  RepositoryOrder order_;
  QStringList manualOrder_;
  QHash<QString, QString> pinnedAccountIds_;
  QCollator collator_;
};

enum class FileCheckPolicy { preserve, checkAll, checkNone };

class ChangedFileListModel final : public QAbstractListModel {
 public:
  enum Role {
    pathRole = Qt::UserRole + 1,
    nameRole,
    directoryRole,
    statusCodeRole,
    statusToneRole,
    addedCountRole,
    removedCountRole,
    binaryRole,
  };

  explicit ChangedFileListModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index,
                              int role = Qt::DisplayRole) const override;
  [[nodiscard]] bool setData(const QModelIndex& index, const QVariant& value,
                             int role = Qt::EditRole) override;
  [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  void setFiles(QList<ChangedFile> files,
                FileCheckPolicy policy = FileCheckPolicy::checkAll);
  void setAllChecked(bool checked);

  [[nodiscard]] const QList<ChangedFile>& files() const noexcept;
  [[nodiscard]] const ChangedFile* fileAt(int row) const noexcept;
  [[nodiscard]] QStringList checkedPaths() const;
  [[nodiscard]] qsizetype checkedCount() const noexcept;
  [[nodiscard]] Qt::CheckState aggregateCheckState() const noexcept;

 private:
  QList<ChangedFile> files_;
  QSet<QString> checkedPaths_;
};

struct HistoryGraphRow {
  int lane{};
  int width{};
  bool incoming{};
  int color{};
  QList<int> passingColors;
  QList<int> parentColors;
  QList<QPair<int, int>> passing;
  QList<int> parents;
};

class HistoryCommitListModel final : public QAbstractListModel {
 public:
  enum Role {
    fullHashRole = Qt::UserRole + 1,
    shortHashRole,
    titleRole,
    authorRole,
    emailRole,
    dateRole,
    parentsRole,
    refsRole,
    dayLabelRole,
    startsDayGroupRole,
  };

  explicit HistoryCommitListModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index,
                              int role = Qt::DisplayRole) const override;
  [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  void resetHistory(QList<HistoryCommit> commits, QString anchor = {},
                    bool endOfHistory = false);
  void resetPage(const HistoryPage& page);
  [[nodiscard]] qsizetype appendCommits(QList<HistoryCommit> commits);
  [[nodiscard]] qsizetype appendPage(const HistoryPage& page);
  void clear();
  void setSearch(QString search);
  void setGraphEnabled(bool enabled);
  [[nodiscard]] const HistoryGraphRow* graphRowAt(int row) const;
  void setPagingState(QString anchor, bool endOfHistory);

  [[nodiscard]] const HistoryCommit* commitAt(int row) const noexcept;
  [[nodiscard]] const QList<HistoryCommit>& commits() const noexcept;
  [[nodiscard]] const QString& search() const noexcept;
  [[nodiscard]] const QString& anchor() const noexcept;
  [[nodiscard]] bool endOfHistory() const noexcept;

 private:
  [[nodiscard]] bool matchesSearch(const HistoryCommit& commit) const;
  [[nodiscard]] QString dayLabel(const HistoryCommit& commit) const;
  void rebuildVisible();
  void appendGraph(const HistoryCommit& commit);

  bool graphEnabled_{};
  QStringList graphLanes_;
  QList<HistoryGraphRow> graphRows_;
  QHash<QString, int> graphColors_;
  QList<HistoryCommit> commits_;
  QList<int> visibleIndices_;
  QSet<QString> hashes_;
  QString search_;
  QString anchor_;
  bool endOfHistory_{};
};

class CommitFileListModel final : public QAbstractListModel {
 public:
  enum Role {
    pathRole = Qt::UserRole + 1,
    nameRole,
    directoryRole,
    statusCodeRole,
    statusToneRole,
    addedCountRole,
    removedCountRole,
    binaryRole,
  };

  explicit CommitFileListModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index,
                              int role = Qt::DisplayRole) const override;
  [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  void setFiles(QList<ChangedFile> files);

  [[nodiscard]] const QList<ChangedFile>& files() const noexcept;
  [[nodiscard]] const ChangedFile* fileAt(int row) const noexcept;

 private:
  QList<ChangedFile> files_;
};

}  // namespace relay
