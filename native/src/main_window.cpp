#include "relay/main_window.hpp"

#include "relay/diff_view.hpp"
#include "relay/dialogs.hpp"
#include "relay/item_delegates.hpp"
#include "relay/list_models.hpp"
#include "relay/relay_controller.hpp"
#include "relay/theme.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace relay {
namespace {

const Account* accountNamed(const AppState& state, const QString& id) {
  const auto iterator = std::find_if(state.accounts.cbegin(), state.accounts.cend(),
                                     [&id](const Account& account) { return account.id == id; });
  return iterator == state.accounts.cend() ? nullptr : &*iterator;
}

QString githubCommitUrl(QString remote, const QString& hash) {
  remote.replace(u'\\', u'/');
  static const QRegularExpression pattern(
      QStringLiteral("github\\.com[/:]([^/]+)/([^/]+?)(?:\\.git)?$"),
      QRegularExpression::CaseInsensitiveOption);
  const auto match = pattern.match(remote);
  if (!match.hasMatch() || hash.isEmpty()) return {};
  return QStringLiteral("https://github.com/%1/%2/commit/%3")
      .arg(match.captured(1), match.captured(2), hash);
}

QLabel* sectionLabel(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text, parent);
  label->setObjectName(QStringLiteral("sectionHeading"));
  return label;
}

}  // namespace

MainWindow::MainWindow(RelayController* controller, QWidget* parent)
    : QMainWindow(parent), controller_(controller) {
  Q_ASSERT(controller_);
  setWindowTitle(QStringLiteral("Relay"));
  setMinimumSize(820, 600);
  resize(1420, 880);
  buildMenus();
  buildShell();
  connectController();
}

bool MainWindow::event(QEvent* event) {
  if (event->type() == QEvent::WindowActivate && repository_ && busyOperations_.isEmpty()) {
    controller_->refreshRepository();
  }
  return QMainWindow::event(event);
}

void MainWindow::buildMenus() {
  openAction_ = new QAction(tr("Add Local Repository…"), this);
  openAction_->setShortcut(QKeySequence::Open);
  connect(openAction_, &QAction::triggered, this, &MainWindow::openRepositoryDialog);

  cloneAction_ = new QAction(tr("Clone Repository…"), this);
  cloneAction_->setShortcut(QKeySequence(tr("Ctrl+Shift+O")));
  connect(cloneAction_, &QAction::triggered, this, &MainWindow::cloneRepositoryDialog);

  scanAction_ = new QAction(tr("Scan Folder for Repositories…"), this);
  connect(scanAction_, &QAction::triggered, this, &MainWindow::scanFolderDialog);

  removeAction_ = new QAction(tr("Remove Current Repository from Relay"), this);
  removeAction_->setEnabled(false);
  connect(removeAction_, &QAction::triggered, this, &MainWindow::removeCurrentRepository);

  auto* file = menuBar()->addMenu(tr("&File"));
  file->addActions({openAction_, cloneAction_, scanAction_});
  file->addSeparator();
  file->addAction(removeAction_);
#ifndef Q_OS_MACOS
  file->addSeparator();
  file->addAction(tr("E&xit"), QKeySequence::Quit, qApp, &QApplication::quit);
#endif

  auto* edit = menuBar()->addMenu(tr("&Edit"));
  edit->addAction(tr("Undo"), QKeySequence::Undo);
  edit->addAction(tr("Redo"), QKeySequence::Redo);
  edit->addSeparator();
  edit->addAction(tr("Cut"), QKeySequence::Cut);
  edit->addAction(tr("Copy"), QKeySequence::Copy);
  edit->addAction(tr("Paste"), QKeySequence::Paste);
  edit->addAction(tr("Select All"), QKeySequence::SelectAll);

  auto* view = menuBar()->addMenu(tr("&View"));
  refreshAction_ = view->addAction(tr("Reload"), QKeySequence::Refresh, this, [this] {
    if (repository_) controller_->refreshRepository();
  });
  forceRefreshAction_ = view->addAction(tr("Force Reload"), QKeySequence(tr("Ctrl+Shift+R")), this, [this] {
    if (!repository_) return;
    historyModel_->clear();
    workingDiff_->clearDiff();
    historyDiff_->clearDiff();
    controller_->refreshRepository();
  });
  view->addSeparator();
  view->addAction(tr("Actual Size"), QKeySequence(tr("Ctrl+0")), this, [this] { setFont(theme::bodyFont()); });
  view->addAction(tr("Toggle Full Screen"), QKeySequence::FullScreen, this, [this] {
    isFullScreen() ? showNormal() : showFullScreen();
  });

  auto* window = menuBar()->addMenu(tr("&Window"));
  window->addAction(tr("Minimize"), QKeySequence(tr("Ctrl+M")), this, &QWidget::showMinimized);
  window->addAction(tr("Close"), QKeySequence::Close, this, &QWidget::close);
}

void MainWindow::buildShell() {
  auto* root = new QWidget(this);
  root->setObjectName(QStringLiteral("relayRoot"));
  auto* layout = new QVBoxLayout(root);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* actionRow = new QFrame(root);
  actionRow->setObjectName(QStringLiteral("actionRow"));
  auto* actions = new QHBoxLayout(actionRow);
  actions->setContentsMargins(8, 8, 12, 8);
  actions->setSpacing(8);
  repositoryButton_ = new QToolButton(actionRow);
  repositoryButton_->setText(tr("Choose a repository"));
  repositoryButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  repositoryButton_->setAccessibleName(tr("Current repository"));
  connect(repositoryButton_, &QToolButton::clicked, this, &MainWindow::openRepositoryDialog);
  branchPicker_ = new QComboBox(actionRow);
  branchPicker_->setAccessibleName(tr("Current branch"));
  branchPicker_->setMinimumWidth(150);
  branchPicker_->addItem(tr("No branch"));
  syncButton_ = new QPushButton(tr("Fetch origin"), actionRow);
  syncButton_->setEnabled(false);
  connect(syncButton_, &QPushButton::clicked, this, [this] {
    if (!repository_) return;
    if (!repository_->hasUpstream || repository_->ahead > 0) controller_->pushOrigin(currentAccountId());
    else controller_->fetchOrigin(currentAccountId());
  });
  accountButton_ = new QToolButton(actionRow);
  accountButton_->setPopupMode(QToolButton::InstantPopup);
  accountButton_->setText(tr("Connect GitHub account"));
  accountButton_->setAccessibleName(tr("Active GitHub account"));
  accountMenu_ = new QMenu(accountButton_);
  accountButton_->setMenu(accountMenu_);
  actions->addWidget(repositoryButton_);
  actions->addWidget(branchPicker_);
  actions->addWidget(syncButton_);
  actions->addStretch();
  actions->addWidget(accountButton_);
  layout->addWidget(actionRow);

  auto* workspace = new QSplitter(Qt::Horizontal, root);
  workspace->setChildrenCollapsible(false);
  workspace->addWidget(buildSidebar(workspace));
  workspaceStack_ = new QStackedWidget(workspace);
  workspaceStack_->addWidget(buildEmptyState(workspaceStack_));
  contentTabs_ = new QTabWidget(workspaceStack_);
  contentTabs_->setObjectName(QStringLiteral("contentTabs"));
  contentTabs_->addTab(buildChangesPage(contentTabs_), tr("Changes"));
  contentTabs_->addTab(buildHistoryPage(contentTabs_), tr("History"));
  workspaceStack_->addWidget(contentTabs_);
  workspace->addWidget(workspaceStack_);
  workspace->setSizes({270, 1150});
  workspace->setStretchFactor(1, 1);
  layout->addWidget(workspace, 1);
  setCentralWidget(root);

  statusBar()->setObjectName(QStringLiteral("statusBar"));
  statusBar()->showMessage(tr("No repository open"));
  statusIdentity_ = new QLabel(tr("No GitHub account connected"), this);
  statusIdentity_->setProperty("role", QStringLiteral("meta"));
  repositorySettingsButton_ = new QPushButton(tr("Repository settings"), this);
  repositorySettingsButton_->setProperty("kind", QStringLiteral("flat"));
  repositorySettingsButton_->setEnabled(false);
  connect(repositorySettingsButton_, &QPushButton::clicked, this, &MainWindow::showRepositoryAccountDialog);
  statusBar()->addPermanentWidget(statusIdentity_);
  statusBar()->addPermanentWidget(repositorySettingsButton_);

  connect(branchPicker_, &QComboBox::currentIndexChanged, this, [this](const int index) {
    if (!repository_ || index < 0) return;
    const auto branch = branchPicker_->itemText(index);
    if (!branch.isEmpty() && branch != repository_->branch) controller_->switchBranch(branch);
  });
  connect(contentTabs_, &QTabWidget::currentChanged, this, [this](const int index) {
    if (index == 1 && repository_ && historyModel_->rowCount() == 0) controller_->requestHistory();
  });
}

QWidget* MainWindow::buildSidebar(QWidget* parent) {
  auto* sidebar = new QFrame(parent);
  sidebar->setObjectName(QStringLiteral("sidebar"));
  sidebar->setMinimumWidth(225);
  sidebar->setMaximumWidth(340);
  auto* layout = new QVBoxLayout(sidebar);
  layout->setContentsMargins(10, 12, 10, 8);
  layout->setSpacing(8);
  auto* headingRow = new QHBoxLayout;
  headingRow->addWidget(sectionLabel(tr("Repositories"), sidebar));
  headingRow->addStretch();
  auto* add = new QToolButton(sidebar);
  add->setText(QStringLiteral("+"));
  add->setProperty("kind", QStringLiteral("icon"));
  add->setAccessibleName(tr("Add repository"));
  auto* addMenu = new QMenu(add);
  addMenu->addAction(openAction_);
  addMenu->addAction(cloneAction_);
  addMenu->addAction(scanAction_);
  add->setMenu(addMenu);
  add->setPopupMode(QToolButton::InstantPopup);
  headingRow->addWidget(add);
  layout->addLayout(headingRow);

  repositoryFilter_ = new QLineEdit(sidebar);
  repositoryFilter_->setPlaceholderText(tr("Filter repositories"));
  repositoryFilter_->setClearButtonEnabled(true);
  repositoryFilter_->setAccessibleName(tr("Filter repositories"));
  layout->addWidget(repositoryFilter_);

  auto* orderRow = new QHBoxLayout;
  repositoryOrder_ = new QComboBox(sidebar);
  repositoryOrder_->addItem(tr("Manual"), static_cast<int>(RepositoryOrderMode::manual));
  repositoryOrder_->addItem(tr("Age"), static_cast<int>(RepositoryOrderMode::age));
  repositoryOrder_->addItem(tr("Name"), static_cast<int>(RepositoryOrderMode::name));
  repositoryOrder_->addItem(tr("Latest commit"), static_cast<int>(RepositoryOrderMode::latest));
  repositoryOrder_->setAccessibleName(tr("Repository order"));
  orderDirection_ = new QToolButton(sidebar);
  orderDirection_->setText(tr("Ascending"));
  orderDirection_->setAccessibleName(tr("Reverse repository order"));
  orderRow->addWidget(repositoryOrder_, 1);
  orderRow->addWidget(orderDirection_);
  layout->addLayout(orderRow);

  repositoryModel_ = new RepositoryListModel(this);
  repositoryList_ = new QListView(sidebar);
  repositoryList_->setObjectName(QStringLiteral("repositoryList"));
  repositoryList_->setModel(repositoryModel_);
  repositoryList_->setItemDelegate(new RepositoryItemDelegate(repositoryList_));
  repositoryList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  repositoryList_->setDragDropMode(QAbstractItemView::InternalMove);
  repositoryList_->setDefaultDropAction(Qt::MoveAction);
  repositoryList_->setDropIndicatorShown(true);
  repositoryList_->setAccessibleName(tr("Repositories"));
  layout->addWidget(repositoryList_, 1);

  connect(repositoryFilter_, &QLineEdit::textChanged, repositoryModel_, &RepositoryListModel::setFilter);
  connect(repositoryOrder_, &QComboBox::currentIndexChanged, this, [this](const int index) {
    if (index < 0) return;
    const auto mode = static_cast<RepositoryOrderMode>(repositoryOrder_->itemData(index).toInt());
    controller_->setRepositoryOrder(mode, appState_.repositoryOrder.direction);
  });
  connect(orderDirection_, &QToolButton::clicked, this, [this] {
    const auto direction = appState_.repositoryOrder.direction == SortDirection::ascending
        ? SortDirection::descending : SortDirection::ascending;
    controller_->setRepositoryOrder(appState_.repositoryOrder.mode, direction);
  });
  connect(repositoryList_, &QListView::activated, this, [this](const QModelIndex& index) {
    if (const auto* repository = repositoryModel_->repositoryAt(index.row()))
      controller_->openRepository(repository->path);
  });
  connect(repositoryList_, &QListView::clicked, this, [this](const QModelIndex& index) {
    if (const auto* repository = repositoryModel_->repositoryAt(index.row()))
      controller_->openRepository(repository->path);
  });
  connect(repositoryModel_, &RepositoryListModel::manualOrderChanged, controller_,
          &RelayController::setManualOrder);
  const auto moveSelectedRepository = [this](const int direction) {
    const auto current = repositoryList_->currentIndex();
    if (!current.isValid() || !repositoryModel_->canManuallyReorder()) return;
    const int targetRow = current.row() + direction;
    if (targetRow < 0 || targetRow >= repositoryModel_->rowCount()) return;
    const auto* source = repositoryModel_->repositoryAt(current.row());
    const auto* target = repositoryModel_->repositoryAt(targetRow);
    if (source == nullptr || target == nullptr) return;
    const auto sourcePath = source->path;
    if (!repositoryModel_->moveRepository(sourcePath, target->path, direction > 0)) return;
    for (int row = 0; row < repositoryModel_->rowCount(); ++row) {
      if (repositoryModel_->index(row).data(RepositoryListModel::pathRole).toString() == sourcePath) {
        repositoryList_->setCurrentIndex(repositoryModel_->index(row));
        break;
      }
    }
  };
  auto* moveUp = new QShortcut(QKeySequence(QStringLiteral("Alt+Up")), repositoryList_);
  moveUp->setContext(Qt::WidgetWithChildrenShortcut);
  connect(moveUp, &QShortcut::activated, this, [moveSelectedRepository] {
    moveSelectedRepository(-1);
  });
  auto* moveDown = new QShortcut(QKeySequence(QStringLiteral("Alt+Down")), repositoryList_);
  moveDown->setContext(Qt::WidgetWithChildrenShortcut);
  connect(moveDown, &QShortcut::activated, this, [moveSelectedRepository] {
    moveSelectedRepository(1);
  });
  return sidebar;
}

QWidget* MainWindow::buildEmptyState(QWidget* parent) {
  auto* page = new QWidget(parent);
  auto* layout = new QVBoxLayout(page);
  layout->setAlignment(Qt::AlignCenter);
  auto* title = new QLabel(tr("Open a repository to get started"), page);
  title->setProperty("role", QStringLiteral("large"));
  auto* detail = new QLabel(tr("Choose an existing Git repository, clone one, or scan a folder."), page);
  detail->setProperty("role", QStringLiteral("small"));
  detail->setAlignment(Qt::AlignCenter);
  auto* actions = new QHBoxLayout;
  auto* open = new QPushButton(tr("Open repository…"), page);
  open->setProperty("kind", QStringLiteral("primary"));
  auto* clone = new QPushButton(tr("Clone repository…"), page);
  connect(open, &QPushButton::clicked, this, &MainWindow::openRepositoryDialog);
  connect(clone, &QPushButton::clicked, this, &MainWindow::cloneRepositoryDialog);
  actions->addWidget(open);
  actions->addWidget(clone);
  layout->addWidget(title, 0, Qt::AlignCenter);
  layout->addWidget(detail, 0, Qt::AlignCenter);
  layout->addLayout(actions);
  return page;
}

QWidget* MainWindow::buildChangesPage(QWidget* parent) {
  auto* splitter = new QSplitter(Qt::Horizontal, parent);
  splitter->setChildrenCollapsible(false);
  auto* left = new QWidget(splitter);
  auto* leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(0);
  auto* fileHeader = new QFrame(left);
  auto* fileHeaderLayout = new QHBoxLayout(fileHeader);
  fileHeaderLayout->setContentsMargins(10, 6, 10, 6);
  selectAllFiles_ = new QCheckBox(tr("Select all changes"), fileHeader);
  selectAllFiles_->setTristate(true);
  fileHeaderLayout->addWidget(selectAllFiles_);
  fileHeaderLayout->addStretch();
  auto* refresh = new QToolButton(fileHeader);
  refresh->setText(tr("Refresh"));
  refresh->setProperty("kind", QStringLiteral("flat"));
  connect(refresh, &QToolButton::clicked, this, [this] { controller_->refreshRepository(); });
  fileHeaderLayout->addWidget(refresh);
  leftLayout->addWidget(fileHeader);

  changedFileModel_ = new ChangedFileListModel(this);
  changedFileList_ = new QListView(left);
  changedFileList_->setObjectName(QStringLiteral("changedFileList"));
  changedFileList_->setModel(changedFileModel_);
  changedFileList_->setItemDelegate(new ChangedFileItemDelegate(changedFileList_));
  changedFileList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  changedFileList_->setAccessibleName(tr("Changed files"));
  leftLayout->addWidget(changedFileList_, 1);

  auto* commitBox = new QFrame(left);
  commitBox->setObjectName(QStringLiteral("commitBox"));
  auto* commitLayout = new QVBoxLayout(commitBox);
  commitLayout->setContentsMargins(10, 10, 10, 10);
  commitIdentity_ = new QLabel(tr("Connect an account before committing"), commitBox);
  commitIdentity_->setProperty("role", QStringLiteral("meta"));
  commitSummary_ = new QLineEdit(commitBox);
  commitSummary_->setPlaceholderText(tr("Summary (required)"));
  commitSummary_->setAccessibleName(tr("Commit summary"));
  commitDescription_ = new QPlainTextEdit(commitBox);
  commitDescription_->setPlaceholderText(tr("Description"));
  commitDescription_->setAccessibleName(tr("Commit description"));
  commitDescription_->setMaximumHeight(78);
  commitButton_ = new QPushButton(tr("Commit selected files"), commitBox);
  commitButton_->setProperty("kind", QStringLiteral("primary"));
  commitButton_->setEnabled(false);
  commitLayout->addWidget(commitIdentity_);
  commitLayout->addWidget(commitSummary_);
  commitLayout->addWidget(commitDescription_);
  commitLayout->addWidget(commitButton_);
  leftLayout->addWidget(commitBox);

  auto* right = new QWidget(splitter);
  auto* rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  auto* diffTitle = sectionLabel(tr("Diff"), right);
  diffTitle->setContentsMargins(12, 10, 12, 8);
  workingDiff_ = new DiffView(right);
  rightLayout->addWidget(diffTitle);
  rightLayout->addWidget(workingDiff_, 1);
  splitter->addWidget(left);
  splitter->addWidget(right);
  splitter->setSizes({310, 840});
  splitter->setStretchFactor(1, 1);

  connect(selectAllFiles_, &QCheckBox::checkStateChanged, this, [this](const Qt::CheckState state) {
    if (state != Qt::PartiallyChecked) changedFileModel_->setAllChecked(state == Qt::Checked);
    updateCommitAction();
  });
  connect(changedFileModel_, &QAbstractItemModel::dataChanged, this, [this] {
    const QSignalBlocker blocker(selectAllFiles_);
    selectAllFiles_->setCheckState(changedFileModel_->aggregateCheckState());
    updateCommitAction();
  });
  connect(changedFileList_, &QListView::clicked, this, [this](const QModelIndex& index) {
    if (const auto* file = changedFileModel_->fileAt(index.row())) controller_->requestFileDiff(file->path);
  });
  connect(commitSummary_, &QLineEdit::textChanged, this, &MainWindow::updateCommitAction);
  connect(commitButton_, &QPushButton::clicked, this, [this] {
    controller_->commit(changedFileModel_->checkedPaths(), commitSummary_->text(),
                        commitDescription_->toPlainText(), currentAccountId());
  });
  return splitter;
}

QWidget* MainWindow::buildHistoryPage(QWidget* parent) {
  auto* splitter = new QSplitter(Qt::Horizontal, parent);
  splitter->setChildrenCollapsible(false);
  auto* left = new QWidget(splitter);
  auto* leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  historySearch_ = new QLineEdit(left);
  historySearch_->setPlaceholderText(tr("Search loaded history"));
  historySearch_->setClearButtonEnabled(true);
  historySearch_->setAccessibleName(tr("Search loaded commits"));
  leftLayout->addWidget(historySearch_);
  historyModel_ = new HistoryCommitListModel(this);
  historyList_ = new QListView(left);
  historyList_->setObjectName(QStringLiteral("historyList"));
  historyList_->setModel(historyModel_);
  historyList_->setItemDelegate(new HistoryCommitItemDelegate(historyList_));
  historyList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  historyList_->setAccessibleName(tr("Commit history"));
  leftLayout->addWidget(historyList_, 1);

  auto* right = new QWidget(splitter);
  auto* rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(16, 12, 12, 8);
  auto* headingRow = new QHBoxLayout;
  historyTitle_ = new QLabel(tr("Select a commit"), right);
  historyTitle_->setProperty("role", QStringLiteral("large"));
  historyTitle_->setWordWrap(true);
  copyHashButton_ = new QPushButton(tr("Copy hash"), right);
  copyHashButton_->setEnabled(false);
  openGitHubButton_ = new QPushButton(tr("Open on GitHub"), right);
  openGitHubButton_->setEnabled(false);
  headingRow->addWidget(historyTitle_, 1);
  headingRow->addWidget(copyHashButton_);
  headingRow->addWidget(openGitHubButton_);
  historyMetadata_ = new QLabel(right);
  historyMetadata_->setProperty("role", QStringLiteral("meta"));
  historyMetadata_->setWordWrap(true);
  historyBody_ = new QLabel(right);
  historyBody_->setProperty("role", QStringLiteral("body"));
  historyBody_->setWordWrap(true);
  commitFileModel_ = new CommitFileListModel(this);
  commitFileList_ = new QListView(right);
  commitFileList_->setObjectName(QStringLiteral("commitFileList"));
  commitFileList_->setModel(commitFileModel_);
  commitFileList_->setItemDelegate(new CommitFileItemDelegate(commitFileList_));
  commitFileList_->setMaximumHeight(230);
  historyDiff_ = new DiffView(right);
  rightLayout->addLayout(headingRow);
  rightLayout->addWidget(historyMetadata_);
  rightLayout->addWidget(historyBody_);
  rightLayout->addWidget(sectionLabel(tr("Changed files"), right));
  rightLayout->addWidget(commitFileList_);
  rightLayout->addWidget(historyDiff_, 1);
  splitter->addWidget(left);
  splitter->addWidget(right);
  splitter->setSizes({330, 820});
  splitter->setStretchFactor(1, 1);

  connect(historySearch_, &QLineEdit::textChanged, historyModel_, &HistoryCommitListModel::setSearch);
  connect(historyList_, &QListView::clicked, this, [this](const QModelIndex& index) {
    if (const auto* commit = historyModel_->commitAt(index.row())) controller_->requestCommitDetail(commit->fullHash);
  });
  connect(historyList_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](const int value) {
    if (value >= historyList_->verticalScrollBar()->maximum() - 240) requestNextHistoryPage();
  });
  connect(commitFileList_, &QListView::clicked, this, [this](const QModelIndex& index) {
    const auto hash = currentCommitHash();
    if (const auto* file = commitFileModel_->fileAt(index.row()); file && !hash.isEmpty())
      controller_->requestCommitFileDiff(hash, file->path);
  });
  connect(copyHashButton_, &QPushButton::clicked, this, [this] {
    if (!commitDetail_) return;
    QApplication::clipboard()->setText(commitDetail_->fullHash);
    showNotice(tr("Commit hash copied."));
  });
  connect(openGitHubButton_, &QPushButton::clicked, this, [this] {
    if (!repository_ || !commitDetail_) return;
    const auto url = githubCommitUrl(repository_->remote, commitDetail_->fullHash);
    if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(url));
  });
  return splitter;
}

void MainWindow::connectController() {
  connect(controller_, &RelayController::stateChanged, this, &MainWindow::applyState);
  connect(controller_, &RelayController::currentRepositoryChanged, this, &MainWindow::applyRepository);
  connect(controller_, &RelayController::repositoryClosed, this, [this] {
    repository_.reset();
    commitDetail_.reset();
    workspaceStack_->setCurrentIndex(0);
    repositoryButton_->setText(tr("Choose a repository"));
    branchPicker_->clear();
    branchPicker_->addItem(tr("No branch"));
    changedFileModel_->setFiles({});
    historyModel_->clear();
    workingDiff_->clearDiff();
    historyDiff_->clearDiff();
    removeAction_->setEnabled(false);
    repositorySettingsButton_->setEnabled(false);
    syncButton_->setEnabled(false);
    statusBar()->showMessage(tr("No repository open"));
  });
  connect(controller_, &RelayController::fileDiffReady, this,
          [this](const QString& repositoryPath, const QString&, const QString& diff) {
            if (repository_ && repository_->path == repositoryPath) workingDiff_->setDiff(diff);
          });
  connect(controller_, &RelayController::historyReady, this,
          [this](const QString& repositoryPath, const HistoryPage& page) {
            if (!repository_ || repository_->path != repositoryPath) return;
            if (historyModel_->anchor().isEmpty() || historyModel_->anchor() != page.anchor)
              historyModel_->resetPage(page);
            else static_cast<void>(historyModel_->appendPage(page));
          });
  connect(controller_, &RelayController::commitDetailReady, this,
          [this](const QString& repositoryPath, const CommitDetail& detail) {
            if (!repository_ || repository_->path != repositoryPath) return;
            commitDetail_ = detail;
            historyTitle_->setText(detail.title);
            historyMetadata_->setText(tr("%1 <%2> · committed by %3 <%4> · %5")
                .arg(detail.author, detail.authorEmail, detail.committer, detail.committerEmail,
                     QLocale().toString(detail.committerDate.toLocalTime(), QLocale::ShortFormat)));
            historyBody_->setText(detail.body);
            commitFileModel_->setFiles(detail.files);
            copyHashButton_->setEnabled(true);
            openGitHubButton_->setEnabled(!githubCommitUrl(repository_->remote, detail.fullHash).isEmpty());
            historyDiff_->clearDiff();
            if (!detail.files.isEmpty()) controller_->requestCommitFileDiff(detail.fullHash, detail.files.front().path);
          });
  connect(controller_, &RelayController::commitFileDiffReady, this,
          [this](const QString& repositoryPath, const QString& hash, const QString&, const QString& diff) {
            if (repository_ && commitDetail_ && repository_->path == repositoryPath && commitDetail_->fullHash == hash)
              historyDiff_->setDiff(diff);
          });
  connect(controller_, &RelayController::accountEmailsReady, this,
          [this](const QString& accountId, const QList<EmailChoice>& choices, const QString& current) {
            const auto* selected = accountNamed(appState_, accountId);
            if (!selected) return;
            const bool newlyConnected = newlyConnectedAccountId_ == accountId;
            CommitEmailDialog dialog(this);
            dialog.setAccount(*selected, newlyConnected);
            dialog.setChoices(choices);
            dialog.setEmail(current);
            const auto result = dialog.exec();
            if (newlyConnected) newlyConnectedAccountId_.clear();
            if (result == QDialog::Accepted)
              controller_->setAccountEmail(accountId, dialog.email());
          });
  connect(controller_, &RelayController::loginProgress, this, [this](const GitHubLoginProgress& progress) {
    if (!progress.code.isEmpty() && progress.code != lastOpenedDeviceCode_) {
      lastOpenedDeviceCode_ = progress.code;
      QApplication::clipboard()->setText(progress.code);
      QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/login/device")));
    }
    showNotice(progress.code.isEmpty() ? progress.message
                                       : tr("GitHub code %1 copied. Complete sign-in in your browser.").arg(progress.code));
  });
  connect(controller_, &RelayController::repositoryScanFinished, this, [this](const RepositoryScanResult& result) {
    showNotice(tr("Found %1 repositories; added %2.").arg(result.found).arg(result.added));
  });
  connect(controller_, &RelayController::sshTestFinished, this, [this](const SshTestResult& result) {
    showNotice(result.message, !result.ok);
  });
  connect(controller_, &RelayController::busyChanged, this, [this](const QString& operation, const bool busy) {
    if (busy) busyOperations_.insert(operation);
    else busyOperations_.remove(operation);
    syncButton_->setEnabled(repository_.has_value() && busyOperations_.isEmpty());
    commitButton_->setEnabled(commitButton_->isEnabled() && busyOperations_.isEmpty());
    if (busy) statusBar()->showMessage(tr("Working: %1…").arg(operation));
    else if (repository_) statusBar()->showMessage(tr("%1 — %2").arg(repository_->name, repository_->branch));
    if (!busy && operation == QStringLiteral("accounts") && accountConnectionPending_)
      accountConnectionPending_ = false;
    updateCommitAction();
  });
  connect(controller_, &RelayController::operationFailed, this,
          [this](const QString& operation, const QString& message) {
            if (operation == QStringLiteral("connect-account")) accountConnectionPending_ = false;
            showNotice(message, true);
          });
}

void MainWindow::applyState(const AppState& state) {
  QString newlyConnected;
  if (accountConnectionPending_) {
    const auto iterator = std::find_if(
        state.accounts.cbegin(), state.accounts.cend(), [this](const Account& candidate) {
          return accountNamed(appState_, candidate.id) == nullptr;
        });
    if (iterator != state.accounts.cend()) newlyConnected = iterator->id;
  }
  appState_ = state;
  repositoryModel_->setRepositories(state.repositories);
  repositoryModel_->setOrder(state.repositoryOrder, state.manualOrder);
  repositoryModel_->setPinnedAccountIds(state.repositoryAccounts);
  const QSignalBlocker orderBlocker(repositoryOrder_);
  for (int index = 0; index < repositoryOrder_->count(); ++index) {
    if (repositoryOrder_->itemData(index).toInt() == static_cast<int>(state.repositoryOrder.mode)) {
      repositoryOrder_->setCurrentIndex(index);
      break;
    }
  }
  orderDirection_->setText(state.repositoryOrder.direction == SortDirection::ascending
                               ? tr("Ascending") : tr("Descending"));
  orderDirection_->setEnabled(state.repositoryOrder.mode != RepositoryOrderMode::manual);
  const auto* active = accountNamed(state, state.activeAccountId);
  accountButton_->setText(active ? QStringLiteral("%1  @%2").arg(active->name, active->handle)
                                 : tr("Connect GitHub account"));
  statusIdentity_->setText(active ? tr("Signed in as @%1").arg(active->handle)
                                  : tr("No GitHub account connected"));
  rebuildAccountMenu();
  if (repository_) {
    const auto* selected = accountNamed(state, currentAccountId());
    commitIdentity_->setText(selected
        ? tr("Committing as %1 <%2>").arg(selected->name, selected->email)
        : tr("Connect an account before committing"));
  }
  updateCommitAction();
  if (!newlyConnected.isEmpty()) {
    accountConnectionPending_ = false;
    newlyConnectedAccountId_ = newlyConnected;
    QTimer::singleShot(0, this, [this, newlyConnected] {
      showAccountEmailDialog(newlyConnected);
    });
  }
}

void MainWindow::applyRepository(const Repository& repository) {
  const auto sameRepository = repository_ && repository_->path == repository.path;
  repository_ = repository;
  workspaceStack_->setCurrentIndex(1);
  repositoryButton_->setText(QStringLiteral("%1  ·  %2").arg(repository.name, repository.owner));
  repositoryButton_->setToolTip(repository.path);
  {
    const QSignalBlocker blocker(branchPicker_);
    branchPicker_->clear();
    branchPicker_->addItems(repository.branches);
    if (!repository.branches.contains(repository.branch)) branchPicker_->addItem(repository.branch);
    branchPicker_->setCurrentText(repository.branch);
  }
  changedFileModel_->setFiles(repository.files,
                              sameRepository ? FileCheckPolicy::preserve : FileCheckPolicy::checkAll);
  {
    const QSignalBlocker blocker(selectAllFiles_);
    selectAllFiles_->setCheckState(changedFileModel_->aggregateCheckState());
  }
  if (!sameRepository) {
    workingDiff_->clearDiff();
    historyDiff_->clearDiff();
    historyModel_->clear();
    commitFileModel_->setFiles({});
    commitDetail_.reset();
  }
  if (!repository.files.isEmpty()) controller_->requestFileDiff(repository.files.front().path);
  if (contentTabs_->currentIndex() == 1 && historyModel_->rowCount() == 0) controller_->requestHistory();
  removeAction_->setEnabled(true);
  repositorySettingsButton_->setEnabled(true);
  syncButton_->setEnabled(busyOperations_.isEmpty());
  statusBar()->showMessage(tr("%1 — %2").arg(repository.name, repository.branch));
  updateSyncAction();
  applyState(controller_->state());
}

void MainWindow::rebuildAccountMenu() {
  accountMenu_->clear();
  for (const auto& account : appState_.accounts) {
    auto* action = accountMenu_->addAction(QStringLiteral("%1  @%2").arg(account.name, account.handle));
    action->setCheckable(true);
    action->setChecked(account.id == appState_.activeAccountId);
    connect(action, &QAction::triggered, this, [this, id = account.id] { controller_->setActiveAccount(id); });
  }
  if (!appState_.accounts.isEmpty()) accountMenu_->addSeparator();
  accountMenu_->addAction(tr("Connect another account…"), this, [this] {
    accountConnectionPending_ = true;
    controller_->connectAccount();
  });
  auto* emails = accountMenu_->addMenu(tr("Commit email"));
  emails->setEnabled(!appState_.accounts.isEmpty());
  for (const auto& account : appState_.accounts) {
    emails->addAction(QStringLiteral("@%1 — %2").arg(account.handle, account.email), this,
                      [this, id = account.id] { showAccountEmailDialog(id); });
  }
  accountMenu_->addAction(tr("Manage accounts…"), this, &MainWindow::showAccountsDialog);
  accountMenu_->addAction(tr("Manage SSH identities…"), this, &MainWindow::showSshProfilesDialog);
}

void MainWindow::updateSyncAction() {
  if (!repository_) {
    syncButton_->setText(tr("Fetch origin"));
    syncButton_->setEnabled(false);
    return;
  }
  if (!repository_->hasUpstream) syncButton_->setText(tr("Publish branch"));
  else if (repository_->ahead > 0) syncButton_->setText(tr("Push origin (%1)").arg(repository_->ahead));
  else syncButton_->setText(repository_->behind > 0 ? tr("Fetch origin (%1)").arg(repository_->behind)
                                                   : tr("Fetch origin"));
}

void MainWindow::updateCommitAction() {
  const auto valid = repository_ && !commitSummary_->text().trimmed().isEmpty() &&
                     changedFileModel_->checkedCount() > 0 && !currentAccountId().isEmpty() &&
                     busyOperations_.isEmpty();
  commitButton_->setEnabled(valid);
  commitButton_->setText(tr("Commit %1 selected file(s)").arg(changedFileModel_->checkedCount()));
}

void MainWindow::openRepositoryDialog() {
  const auto path = QFileDialog::getExistingDirectory(this, tr("Open a Git repository"));
  if (!path.isEmpty()) controller_->openRepository(path);
}

void MainWindow::scanFolderDialog() {
  const auto path = QFileDialog::getExistingDirectory(this, tr("Scan a folder for Git repositories"));
  if (!path.isEmpty()) controller_->scanFolder(path);
}

void MainWindow::cloneRepositoryDialog() {
  CloneDialog dialog(this);
  dialog.setAccounts(appState_.accounts, appState_.activeAccountId);
  dialog.setSshProfiles(appState_.sshProfiles);
  dialog.setParentPath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
  connect(&dialog, &CloneDialog::accountChanged, controller_,
          &RelayController::requestGitHubRepositories);
  connect(&dialog, &CloneDialog::refreshGithubRepositoriesRequested, controller_,
          &RelayController::requestGitHubRepositories);
  connect(controller_, &RelayController::githubRepositoriesReady, &dialog,
          [&dialog](const GitHubRepositoryPage& page) {
            dialog.setGithubRepositories(page.repositories, page.hidden);
          });
  connect(controller_, &RelayController::operationFailed, &dialog,
          [&dialog](const QString& operation, const QString& message) {
            if (operation == QStringLiteral("github-repositories"))
              dialog.setGithubRepositories({}, 0, message);
          });
  if (!appState_.activeAccountId.isEmpty())
    controller_->requestGitHubRepositories(appState_.activeAccountId);
  if (dialog.exec() != QDialog::Accepted) return;
  const auto request = dialog.request();
  controller_->cloneRepository(request.remoteUrl, request.parentPath,
                               request.repositoryName, request.accountId,
                               request.sshProfileId);
}

void MainWindow::removeCurrentRepository() {
  if (!repository_) return;
  const auto answer = QMessageBox::question(this, tr("Remove repository"),
      tr("Remove %1 from Relay? Files on disk will not be changed.").arg(repository_->name));
  if (answer == QMessageBox::Yes) controller_->removeRepository(repository_->path);
}

void MainWindow::showRepositoryAccountDialog() {
  if (!repository_) return;
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Repository settings"));
  dialog.setMinimumWidth(470);
  auto* layout = new QVBoxLayout(&dialog);
  auto* title = new QLabel(repository_->name, &dialog);
  title->setProperty("role", QStringLiteral("title"));
  layout->addWidget(title);
  auto* explanation = new QLabel(
      tr("Choose the GitHub identity used for commits and HTTPS operations, and an optional SSH identity for this repository."),
      &dialog);
  explanation->setWordWrap(true);
  layout->addWidget(explanation);
  auto* form = new QFormLayout;
  auto* accountCombo = new QComboBox(&dialog);
  accountCombo->addItem(tr("Follow the active account"), QString{});
  for (const auto& accountEntry : appState_.accounts) {
    accountCombo->addItem(QStringLiteral("%1  @%2").arg(accountEntry.name, accountEntry.handle),
                          accountEntry.id);
  }
  accountCombo->setCurrentIndex(std::max(0, accountCombo->findData(
      appState_.repositoryAccounts.value(repository_->path))));
  auto* ssh = new QComboBox(&dialog);
  ssh->addItem(tr("Use my SSH agent and ~/.ssh/config"), QString{});
  for (const auto& profile : appState_.sshProfiles)
    ssh->addItem(QStringLiteral("%1 — %2").arg(profile.label, profile.host), profile.id);
  ssh->setCurrentIndex(std::max(0, ssh->findData(
      appState_.repositorySshProfiles.value(repository_->path))));
  form->addRow(tr("GitHub account:"), accountCombo);
  form->addRow(tr("SSH identity:"), ssh);
  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save,
                                        &dialog);
  buttons->button(QDialogButtonBox::Save)->setProperty("kind", QStringLiteral("primary"));
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  if (dialog.exec() != QDialog::Accepted) return;
  controller_->setRepositoryAccount(repository_->path, accountCombo->currentData().toString());
  controller_->setRepositorySshProfile(repository_->path, ssh->currentData().toString());
}

void MainWindow::showAccountsDialog() {
  AccountManagementDialog dialog(this);
  dialog.setAccounts(appState_.accounts);
  connect(controller_, &RelayController::stateChanged, &dialog,
          [&dialog](const AppState& state) { dialog.setAccounts(state.accounts); });
  connect(&dialog, &AccountManagementDialog::addAccountRequested, &dialog, [this] {
    accountConnectionPending_ = true;
    controller_->connectAccount();
  });
  connect(&dialog, &AccountManagementDialog::editEmailRequested, &dialog,
          [this](const QString& accountId) { showAccountEmailDialog(accountId); });
  connect(&dialog, &AccountManagementDialog::removeAccountRequested, &dialog,
          [this, &dialog](const QString& accountId) {
            const auto* selected = accountNamed(appState_, accountId);
            if (!selected) return;
            if (QMessageBox::question(
                    &dialog, tr("Remove GitHub account"),
                    tr("Remove @%1 from Relay and sign it out on this computer?")
                        .arg(selected->handle)) == QMessageBox::Yes) {
              controller_->removeAccount(accountId);
            }
          });
  dialog.exec();
}

void MainWindow::showAccountEmailDialog(const QString& accountId) {
  controller_->requestAccountEmails(accountId);
}

void MainWindow::showSshProfilesDialog() {
  QStringList labels{tr("Add a new SSH identity…")};
  struct Choice { bool remove{}; qsizetype profileIndex{-1}; };
  QList<Choice> choices{{false, -1}};
  for (qsizetype index = 0; index < appState_.sshProfiles.size(); ++index) {
    const auto& profile = appState_.sshProfiles.at(index);
    labels.append(tr("Edit %1 — %2").arg(profile.label, profile.host));
    choices.append({false, index});
    labels.append(tr("Remove %1 — %2").arg(profile.label, profile.host));
    choices.append({true, index});
  }
  bool accepted{};
  const auto selected = QInputDialog::getItem(this, tr("Manage SSH identities"),
                                               tr("Choose an action:"), labels, 0,
                                               false, &accepted);
  if (!accepted) return;
  const auto selectedIndex = labels.indexOf(selected);
  if (selectedIndex < 0 || selectedIndex >= choices.size()) return;
  const auto choice = choices.at(selectedIndex);
  if (choice.remove) {
    const auto& profile = appState_.sshProfiles.at(choice.profileIndex);
    if (QMessageBox::question(this, tr("Remove SSH identity"),
          tr("Remove %1? Repository bindings using it will return to the default SSH configuration.")
              .arg(profile.label)) == QMessageBox::Yes) {
      controller_->removeSshProfile(profile.id);
    }
    return;
  }

  SshProfileDialog dialog(this);
  if (choice.profileIndex >= 0) dialog.setProfile(appState_.sshProfiles.at(choice.profileIndex));
  connect(&dialog, &SshProfileDialog::testRequested, &dialog,
          [this, &dialog](const SshProfile& profile) {
            dialog.setTesting(true);
            controller_->testSshProfile(profile);
          });
  connect(controller_, &RelayController::sshTestFinished, &dialog,
          [&dialog](const SshTestResult& result) {
            dialog.setTesting(false);
            dialog.setTestResult(result);
          });
  connect(controller_, &RelayController::operationFailed, &dialog,
          [&dialog](const QString& operation, const QString& message) {
            if (operation != QStringLiteral("ssh-test")) return;
            dialog.setTesting(false);
            dialog.setTestResult({false, message});
          });
  if (dialog.exec() == QDialog::Accepted) controller_->saveSshProfile(dialog.profile());
}

void MainWindow::requestNextHistoryPage() {
  if (!repository_ || historyModel_->endOfHistory() || busyOperations_.contains(QStringLiteral("history"))) return;
  controller_->requestHistory(static_cast<int>(historyModel_->commits().size()), 50,
                              historyModel_->anchor());
}

void MainWindow::showNotice(const QString& message, const bool error) {
  if (message.isEmpty()) return;
  statusBar()->setStyleSheet(error ? QStringLiteral("QStatusBar { color: #a54e43; }") : QString{});
  statusBar()->showMessage(message, error ? 4400 : 2800);
}

QString MainWindow::currentAccountId() const {
  if (!repository_) return appState_.activeAccountId;
  return controller_->resolvedAccountId(repository_->path);
}

QString MainWindow::currentCommitHash() const {
  return commitDetail_ ? commitDetail_->fullHash : QString{};
}

}  // namespace relay
