#pragma once

#include "relay/domain.hpp"

#include <QMainWindow>
#include <QHash>

#include <optional>

class QAction;
class QCheckBox;
class QComboBox;
class QEvent;
class QDialog;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QCloseEvent;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class QTimer;

namespace relay {

class ChangedFileListModel;
class CommitFileListModel;
class DiffView;
class HistoryCommitListModel;
class RelayController;
class RepositoryListModel;

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(RelayController* controller, QWidget* parent = nullptr);

 protected:
  bool event(QEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void buildMenus();
  void buildWorkflowMenus(QMenu* file, QMenu* repositoryMenu);
  void updateWorkflowActions();
  void showConflicts();
  void showStashes();
  void showPublishDialog();
  void showSettingsDialog();
  void buildShell();
  QWidget* buildSidebar(QWidget* parent);
  QWidget* buildEmptyState(QWidget* parent);
  QWidget* buildChangesPage(QWidget* parent);
  QWidget* buildHistoryPage(QWidget* parent);
  void connectController();
  void applyState(const AppState& state);
  [[nodiscard]] QJsonObject captureLayout() const;
  void restoreLayout(const QJsonObject& layout, bool includeWindow);
  void saveLayout();
  void applyRepository(const Repository& repository);
  void rebuildAccountMenu();
  void updateSyncAction();
  void clearCommitDetail();
  void updateCommitAction();
  void openRepositoryDialog();
  void scanFolderDialog();
  void cloneRepositoryDialog();
  void removeCurrentRepository();
  void showRepositoryAccountDialog();
  void showAccountsDialog();
  void showForgeDialog();
  void showAccountEmailDialog(const QString& accountId);
  void showSshProfilesDialog();
  void requestNextHistoryPage();
  void reloadHistory();
  void updateHistoryBranches();
  void updateHistoryBranchActions();
  void updateStatus();
  void showNotice(const QString& message, bool error = false);
  [[nodiscard]] QString currentAccountId() const;
  [[nodiscard]] QString currentCommitHash() const;

  RelayController* controller_{};
  RepositoryListModel* repositoryModel_{};
  ChangedFileListModel* changedFileModel_{};
  HistoryCommitListModel* historyModel_{};
  CommitFileListModel* commitFileModel_{};

  QStackedWidget* workspaceStack_{};
  QTabWidget* contentTabs_{};
  QListView* repositoryList_{};
  QListView* changedFileList_{};
  QListView* historyList_{};
  QListView* commitFileList_{};
  DiffView* workingDiff_{};
  DiffView* historyDiff_{};
  QLineEdit* repositoryFilter_{};
  QComboBox* repositoryOrder_{};
  QToolButton* orderDirection_{};
  QToolButton* repositoryButton_{};
  QComboBox* branchPicker_{};
  QToolButton* syncButton_{};
  QToolButton* accountButton_{};
  QMenu* accountMenu_{};
  QCheckBox* selectAllFiles_{};
  QLineEdit* commitSummary_{};
  QPlainTextEdit* commitDescription_{};
  QLineEdit* commitCoAuthors_{};
  QToolButton* coAuthorButton_{};
  QLabel* commitIdentity_{};
  QPushButton* commitButton_{};
  QLineEdit* historySearch_{};
  QComboBox* historyMode_{};
  QComboBox* historySearchField_{};
  QToolButton* historyHighlight_{};
  QLabel* historyMatches_{};
  QToolButton* historyFocus_{};
  QWidget* sidebarHeading_{};
  QToolButton* historyPreviousMatch_{};
  QToolButton* historyNextMatch_{};
  QWidget* historyDetails_{};
  bool focusRestoreMaximized_{};
  // Moves the history selection to the next (+1) or previous (-1) match.
  void stepHistoryMatch(int direction);
  void updateHistoryMatches();
  void setHistoryFocus(bool focused);
  QComboBox* historyBranch_{};
  QPushButton* checkoutHistoryBranch_{};
  QPushButton* createHistoryBranch_{};
  QPushButton* fetchHistoryBranches_{};
  QLabel* historyTitle_{};
  QLabel* historyMetadata_{};
  QLabel* historyBody_{};
  QPushButton* copyHashButton_{};
  QPushButton* openGitHubButton_{};
  QLabel* statusIdentity_{};
  QPushButton* repositorySettingsButton_{};

  QAction* openAction_{};
  QAction* cloneAction_{};
  QAction* scanAction_{};
  QAction* removeAction_{};
  QAction* refreshAction_{};
  QAction* forceRefreshAction_{};
  QAction* createBranchAction_{};
  QAction* pullAction_{};
  QAction* forcePushAction_{};
  QString pullRequestAfterPush_;
  QString forcePushOfferPath_;
  QString preconfirmedRebase_;  // chosen in the diverged-pull dialog
  QList<QPair<QString, QString>> editors_;
  QAction* openEditorAction_{};
  void updateEditorAction();
  class CompareDialog* compareDialog_{};
  void confirmForcePush();
  void confirmDeleteOriginBranch();
  void requestPullRequest();
  void showCompareDialog();
  [[nodiscard]] bool canForcePush() const;

  AppState appState_;
  std::optional<Repository> repository_;
  std::optional<CommitDetail> commitDetail_;
  QHash<QString, int> busyOperations_;
  QString workingPreviewPath_;
  QString commitPreviewKey_;
  QString lastOpenedDeviceCode_;
  QString newlyConnectedAccountId_;
  bool accountConnectionPending_{};
  QHash<QString, QPair<QString, QString>> commitDrafts_;
  QList<QAction*> workflowActions_;
  QPushButton* conflictButton_{};
  QTimer* noticeTimer_{};
  QTimer* layoutTimer_{};
  QTimer* historySearchTimer_{};
  QList<QSplitter*> splitters_;
  QJsonObject defaultLayout_;
  bool layoutRestored_{};
  QDialog* loginDialog_{};
  QWidget* runtimeBanner_{};
  QLabel* runtimeMessage_{};
  QPushButton* runtimeRetry_{};
  QLineEdit* loginCode_{};
};

}  // namespace relay
