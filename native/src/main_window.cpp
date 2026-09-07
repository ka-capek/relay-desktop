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
#include <QTextEdit>
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

class SelectAllCheckBox final : public QCheckBox {
 public:
  using QCheckBox::QCheckBox;
 protected:
  void nextCheckState() override {
    setCheckState(checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
  }
};

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
  for (auto* label : findChildren<QLabel*>()) label->setTextFormat(Qt::PlainText);
  noticeTimer_ = new QTimer(this);
  noticeTimer_->setSingleShot(true);
  connect(noticeTimer_, &QTimer::timeout, this, &MainWindow::updateStatus);
  connectController();
}

bool MainWindow::event(QEvent* event) {
  if (event->type() == QEvent::WindowActivate && appState_.preferences.refreshOnFocus && repository_ && busyOperations_.isEmpty()) {
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
  file->addSeparator();
  auto* quit = file->addAction(tr("Quit Relay"), QKeySequence::Quit, qApp, &QApplication::quit);
  quit->setMenuRole(QAction::QuitRole);

  auto* edit = menuBar()->addMenu(tr("&Edit"));
  const auto addEdit = [this, edit](const QString& text, QKeySequence::StandardKey key,
                                    const char* method) {
    auto* action = edit->addAction(text);
    action->setObjectName(QString::fromLatin1(method) + QStringLiteral("Action"));
    action->setShortcut(QKeySequence(key));
    // Text widgets and the diff own their keyboard shortcuts. The menu routes
    // mouse activation to the focused editor without competing for Ctrl/Cmd+C.
    action->setShortcutContext(Qt::WidgetShortcut);
    connect(action, &QAction::triggered, this, [method] {
      auto* focus = QApplication::focusWidget();
      if (!focus) return;
      if (qobject_cast<QLineEdit*>(focus) || qobject_cast<QPlainTextEdit*>(focus) ||
          qobject_cast<QTextEdit*>(focus)) {
        QMetaObject::invokeMethod(focus, method, Qt::DirectConnection);
        return;
      }
      for (auto* widget = focus; widget; widget = widget->parentWidget()) {
        if (auto* diff = dynamic_cast<DiffView*>(widget)) {
          if (QByteArray(method) == "copy") diff->copySelection();
          else if (QByteArray(method) == "selectAll") diff->selectAll();
          break;
        }
      }
    });
  };
  addEdit(tr("Undo"), QKeySequence::Undo, "undo");
  addEdit(tr("Redo"), QKeySequence::Redo, "redo");
  edit->addSeparator();
  addEdit(tr("Cut"), QKeySequence::Cut, "cut");
  addEdit(tr("Copy"), QKeySequence::Copy, "copy");
  addEdit(tr("Paste"), QKeySequence::Paste, "paste");
  addEdit(tr("Select All"), QKeySequence::SelectAll, "selectAll");
  edit->addSeparator();
  auto* settings = edit->addAction(tr("Settings…"), this, &MainWindow::showSettingsDialog);
  settings->setObjectName(QStringLiteral("settingsAction"));
  settings->setMenuRole(QAction::PreferencesRole);
  settings->setShortcut(QKeySequence::Preferences);

  auto* repositoryMenu = menuBar()->addMenu(tr("&Repository"));
  createBranchAction_ = repositoryMenu->addAction(tr("New branch…"), this, [this] {
    if (!repository_ || !busyOperations_.isEmpty()) return;
    bool accepted = false;
    const auto name = QInputDialog::getText(this, tr("New branch"), tr("Branch name"),
                                           QLineEdit::Normal, {}, &accepted).trimmed();
    if (accepted && !name.isEmpty()) controller_->createBranch(name);
  });
  createBranchAction_->setObjectName(QStringLiteral("createBranchAction"));
  createBranchAction_->setShortcut(QKeySequence(tr("Ctrl+Shift+N")));
  pullAction_ = repositoryMenu->addAction(tr("Pull origin"), this, [this] {
    controller_->pullOrigin(currentAccountId());
  });
  pullAction_->setObjectName(QStringLiteral("pullAction"));
  connect(repositoryMenu, &QMenu::aboutToShow, this, [this] {
    createBranchAction_->setEnabled(repository_.has_value() && busyOperations_.isEmpty());
    pullAction_->setEnabled(repository_ && repository_->hasUpstream && busyOperations_.isEmpty());
  });
  createBranchAction_->setEnabled(false);
  pullAction_->setEnabled(false);

  buildWorkflowMenus(file, repositoryMenu);

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
    if (repository_->remote.isEmpty()) { showPublishDialog(); return; }
    if (repository_->hasUpstream && repository_->behind > 0) controller_->pullOrigin(currentAccountId());
    else if (!repository_->hasUpstream || repository_->ahead > 0) controller_->pushOrigin(currentAccountId());
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

  conflictButton_ = new QPushButton(root);
  conflictButton_->setObjectName(QStringLiteral("conflictButton"));
  conflictButton_->hide();
  connect(conflictButton_, &QPushButton::clicked, this, &MainWindow::showConflicts);
  layout->addWidget(conflictButton_);

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
    if (!branch.isEmpty() && branch != repository_->branch) {
      { const QSignalBlocker blocker(branchPicker_); branchPicker_->setCurrentText(repository_->branch); }
      controller_->switchBranch(branch);
    }
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
  repositoryFilter_->setObjectName(QStringLiteral("repositoryFilter"));
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
  selectAllFiles_ = new SelectAllCheckBox(tr("Select all changes"), fileHeader);
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
  connect(changedFileList_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
    if (const auto* file = changedFileModel_->fileAt(index.row())) {
      if (workingPreviewPath_ != file->path) workingDiff_->clearDiff();
      workingPreviewPath_ = file->path;
      controller_->requestFileDiff(file->path);
    }
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
  historyTitle_->setTextFormat(Qt::PlainText);
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
  historyMetadata_->setTextFormat(Qt::PlainText);
  historyBody_ = new QLabel(right);
  historyBody_->setProperty("role", QStringLiteral("body"));
  historyBody_->setWordWrap(true);
  historyBody_->setTextFormat(Qt::PlainText);
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

  connect(historyModel_, &QAbstractItemModel::modelAboutToBeReset, this, &MainWindow::clearCommitDetail);
  connect(historySearch_, &QLineEdit::textChanged, historyModel_, &HistoryCommitListModel::setSearch);
  connect(historyList_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
    clearCommitDetail();
    if (const auto* commit = historyModel_->commitAt(index.row())) controller_->requestCommitDetail(commit->fullHash);
  });
  connect(historyList_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](const int value) {
    if (value >= historyList_->verticalScrollBar()->maximum() - 240) requestNextHistoryPage();
  });
  connect(commitFileList_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
    const auto hash = currentCommitHash();
    if (const auto* file = commitFileModel_->fileAt(index.row()); file && !hash.isEmpty()) {
      const auto key = hash + u':' + file->path;
      if (commitPreviewKey_ != key) historyDiff_->clearDiff();
      commitPreviewKey_ = key;
      controller_->requestCommitFileDiff(hash, file->path);
    }
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
  connect(controller_, &RelayController::commitCreated, this, [this](const QString& path) {
    commitDrafts_.remove(path);
    if (repository_ && repository_->path == path) {
      commitSummary_->clear();
      commitDescription_->clear();
    }
  });
  connect(controller_, &RelayController::commitUndone, this, [this](const QString& path, const QString& summary, const QString& description) {
    if (repository_ && repository_->path == path) {
      if (commitSummary_->text().isEmpty() && commitDescription_->toPlainText().isEmpty()) {
        commitSummary_->setText(summary);
        commitDescription_->setPlainText(description);
      } else showNotice(tr("Commit undone. Your existing draft message was kept."));
    } else if (!commitDrafts_.contains(path) || (commitDrafts_.value(path).first.isEmpty() && commitDrafts_.value(path).second.isEmpty())) {
      commitDrafts_.insert(path, {summary, description});
    }
  });
  connect(controller_, &RelayController::stateChanged, this, &MainWindow::applyState);
  connect(controller_, &RelayController::currentRepositoryChanged, this, &MainWindow::applyRepository);
  connect(controller_, &RelayController::repositoryClosed, this, [this] {
    if (repository_) commitDrafts_.insert(repository_->path, {commitSummary_->text(), commitDescription_->toPlainText()});
    repository_.reset();
    updateWorkflowActions();
    commitSummary_->clear();
    commitDescription_->clear();
    clearCommitDetail();
    createBranchAction_->setEnabled(false);
    pullAction_->setEnabled(false);
    branchPicker_->setEnabled(false);
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
    updateStatus();
  });
  connect(controller_, &RelayController::filePreviewReady, this,
          [this](const QString& repositoryPath, const QString& path, const FilePreview& preview) {
            const auto* selected = changedFileModel_->fileAt(changedFileList_->currentIndex().row());
            if (repository_ && repository_->path == repositoryPath && selected && selected->path == path) workingDiff_->setPreview(preview);
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
            const auto* selectedCommit = historyModel_->commitAt(historyList_->currentIndex().row());
            if (!selectedCommit || selectedCommit->fullHash != detail.fullHash) return;
            commitDetail_ = detail;
            historyTitle_->setText(detail.title);
            historyMetadata_->setText(tr("%1 <%2> · committed by %3 <%4> · %5")
                .arg(detail.author, detail.authorEmail, detail.committer, detail.committerEmail,
                     QLocale().toString(detail.committerDate.toLocalTime(), QLocale::ShortFormat)));
            historyBody_->setText(detail.body);
            commitFileModel_->setFiles(detail.files);
            commitFileList_->setMaximumHeight(static_cast<int>(std::clamp<qsizetype>(detail.files.size() * 52 + 2, 52, 230)));
            copyHashButton_->setEnabled(true);
            openGitHubButton_->setEnabled(!githubCommitUrl(repository_->remote, detail.fullHash).isEmpty());
            historyDiff_->clearDiff();
            if (!detail.files.isEmpty()) commitFileList_->setCurrentIndex(commitFileModel_->index(0));
          });
  connect(controller_, &RelayController::commitFilePreviewReady, this,
          [this](const QString& repositoryPath, const QString& hash, const QString& path, const FilePreview& preview) {
            const auto* selected = commitFileModel_->fileAt(commitFileList_->currentIndex().row());
            if (repository_ && commitDetail_ && repository_->path == repositoryPath && commitDetail_->fullHash == hash && selected && selected->path == path)
              historyDiff_->setPreview(preview);
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
    if (loginCode_ && !progress.code.isEmpty()) loginCode_->setText(progress.code);
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
    if (operation == QStringLiteral("connect-account")) {
      if (busy && !loginDialog_) {
        lastOpenedDeviceCode_.clear();
        loginDialog_ = new QDialog(this);
        loginDialog_->setObjectName(QStringLiteral("loginDialog"));
        loginDialog_->setWindowTitle(tr("Connect GitHub account"));
        loginDialog_->setWindowModality(Qt::WindowModal);
        auto* layout = new QVBoxLayout(loginDialog_);
        auto* label = new QLabel(tr("Enter this code in your browser to connect your GitHub account."), loginDialog_);
        label->setWordWrap(true);
        layout->addWidget(label);
        loginCode_ = new QLineEdit(loginDialog_);
        loginCode_->setReadOnly(true);
        loginCode_->setPlaceholderText(tr("Waiting for GitHub…"));
        loginCode_->setAccessibleName(tr("GitHub device code"));
        layout->addWidget(loginCode_);
        auto* browser = new QPushButton(tr("Open GitHub in browser"), loginDialog_);
        layout->addWidget(browser);
        connect(browser, &QPushButton::clicked, loginDialog_, [] {
          QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/login/device")));
        });
        auto* cancel = new QDialogButtonBox(QDialogButtonBox::Cancel, loginDialog_);
        layout->addWidget(cancel);
        connect(cancel, &QDialogButtonBox::rejected, loginDialog_, &QDialog::reject);
        connect(loginDialog_, &QDialog::rejected, controller_, &RelayController::cancelAccountConnection);
        loginDialog_->show();
      } else if (!busy && loginDialog_) {
        loginDialog_->accept();
        loginDialog_->deleteLater();
        loginDialog_ = nullptr;
        loginCode_ = nullptr;
      }
    }
    if (busy) ++busyOperations_[operation];
    else if (busyOperations_.value(operation) <= 1) busyOperations_.remove(operation);
    else --busyOperations_[operation];
    branchPicker_->setEnabled(repository_.has_value() && busyOperations_.isEmpty());
    createBranchAction_->setEnabled(repository_.has_value() && busyOperations_.isEmpty());
    pullAction_->setEnabled(repository_ && repository_->hasUpstream && busyOperations_.isEmpty());
    syncButton_->setEnabled(repository_.has_value() && busyOperations_.isEmpty());
    commitButton_->setEnabled(commitButton_->isEnabled() && busyOperations_.isEmpty());
    updateStatus();
    if (!busy && operation == QStringLiteral("accounts") && accountConnectionPending_)
      accountConnectionPending_ = false;
    commitSummary_->setEnabled(!busyOperations_.contains(QStringLiteral("commit")) && !busyOperations_.contains(QStringLiteral("repository-action")));
    commitDescription_->setEnabled(!busyOperations_.contains(QStringLiteral("commit")) && !busyOperations_.contains(QStringLiteral("repository-action")));
    updateCommitAction();
    updateWorkflowActions();
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
  const bool historyModeChanged = appState_.preferences.graphHistory != state.preferences.graphHistory;
  appState_ = state;
  historyModel_->setGraphEnabled(state.preferences.graphHistory);
  historySearch_->setPlaceholderText(state.preferences.graphHistory
      ? tr("Filter loaded commits (graph hidden while filtering)") : tr("Search loaded history"));
  if (historyModeChanged) {
    historyModel_->clear();
    clearCommitDetail();
    if (repository_) controller_->requestHistory();
  }
  workingDiff_->setCodeFontSize(state.preferences.diffFontSize);
  historyDiff_->setCodeFontSize(state.preferences.diffFontSize);
  repositoryModel_->setRepositories(state.repositories);
  repositoryModel_->setOrder(state.repositoryOrder, state.manualOrder);
  repositoryModel_->setPinnedAccountIds(state.repositoryAccounts);
  if (repository_) {
    for (int row = 0; row < repositoryModel_->rowCount(); ++row) {
      if (repositoryModel_->repositoryAt(row)->path == repository_->path) {
        repositoryList_->setCurrentIndex(repositoryModel_->index(row));
        break;
      }
    }
  }
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
    const auto identity = controller_->commitIdentity();
    commitIdentity_->setText(!identity.name.isEmpty() && !identity.email.isEmpty()
        ? tr("Committing as %1 <%2>").arg(identity.name, identity.email)
        : tr("Set your Git identity in Settings before committing"));
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
  if (!sameRepository) {
    if (repository_) commitDrafts_.insert(repository_->path, {commitSummary_->text(), commitDescription_->toPlainText()});
    const auto draft = commitDrafts_.value(repository.path);
    commitSummary_->setText(draft.first);
    commitDescription_->setPlainText(draft.second);
  }
  const auto historyChanged = !sameRepository || repository_->branch != repository.branch ||
      repository_->history.value(0).fullHash != repository.history.value(0).fullHash ||
      repository_->historyRefState != repository.historyRefState;
  if (historyChanged) {
    historyModel_->clear();
    historyDiff_->clearDiff();
    commitFileModel_->setFiles({});
    commitDetail_.reset();
    historyTitle_->setText(tr("Select a commit"));
    historyMetadata_->clear();
    historyBody_->clear();
    copyHashButton_->setEnabled(false);
    openGitHubButton_->setEnabled(false);
  }
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
  const auto* previousFile = changedFileModel_->fileAt(changedFileList_->currentIndex().row());
  const QString selectedPath = sameRepository && previousFile ? previousFile->path : QString{};
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
  if (!repository.files.isEmpty()) {
    int selectedRow = 0;
    for (int row = 0; row < repository.files.size(); ++row) {
      if (repository.files.at(row).path == selectedPath) { selectedRow = row; break; }
    }
    if (repository.files.at(selectedRow).path != selectedPath) workingDiff_->clearDiff();
    changedFileList_->setCurrentIndex(changedFileModel_->index(selectedRow, 0));
  } else workingDiff_->clearDiff();
  if (contentTabs_->currentIndex() == 1 && historyModel_->rowCount() == 0) controller_->requestHistory();
  branchPicker_->setEnabled(busyOperations_.isEmpty());
  createBranchAction_->setEnabled(busyOperations_.isEmpty());
  pullAction_->setEnabled(repository.hasUpstream && busyOperations_.isEmpty());
  removeAction_->setEnabled(true);
  repositorySettingsButton_->setEnabled(true);
  syncButton_->setEnabled(busyOperations_.isEmpty());
  updateStatus();
  updateSyncAction();
  updateWorkflowActions();
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

void MainWindow::clearCommitDetail() {
  commitDetail_.reset();
  historyTitle_->setText(tr("Select a commit"));
  historyMetadata_->clear();
  historyBody_->clear();
  commitFileModel_->setFiles({});
  historyDiff_->clearDiff();
  copyHashButton_->setEnabled(false);
  openGitHubButton_->setEnabled(false);
}

void MainWindow::updateSyncAction() {
  if (!repository_) {
    syncButton_->setText(tr("Fetch origin"));
    syncButton_->setEnabled(false);
    return;
  }
  if (repository_->remote.isEmpty()) syncButton_->setText(tr("Publish repository"));
  else if (!repository_->hasUpstream) syncButton_->setText(tr("Publish branch"));
  else if (repository_->behind > 0) syncButton_->setText(tr("Pull origin (%1)").arg(repository_->behind));
  else if (repository_->ahead > 0) syncButton_->setText(tr("Push origin (%1)").arg(repository_->ahead));
  else syncButton_->setText(repository_->behind > 0 ? tr("Fetch origin (%1)").arg(repository_->behind)
                                                   : tr("Fetch origin"));
}

void MainWindow::updateCommitAction() {
  const auto identity = controller_->commitIdentity();
  const auto valid = repository_ && !commitSummary_->text().trimmed().isEmpty() &&
                     changedFileModel_->checkedCount() > 0 && !identity.name.isEmpty() && !identity.email.isEmpty() &&
                     busyOperations_.isEmpty() && repository_->pendingOperation.isEmpty() &&
                     repository_->conflictedFiles.isEmpty();
  commitButton_->setEnabled(valid);
  commitButton_->setText(tr("Commit %1 selected file(s)").arg(changedFileModel_->checkedCount()));
}

void MainWindow::showSettingsDialog() {
  SettingsDialog dialog(appState_.preferences, this);
  connect(&dialog, &SettingsDialog::manageAccountsRequested, this, &MainWindow::showAccountsDialog);
  if (dialog.exec() == QDialog::Accepted) controller_->setPreferences(dialog.preferences());
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
  title->setTextFormat(Qt::PlainText);
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

void MainWindow::updateStatus() {
  if (noticeTimer_ && noticeTimer_->isActive()) return;
  statusBar()->setStyleSheet({});
  if (!busyOperations_.isEmpty())
    statusBar()->showMessage(tr("Working: %1…").arg(busyOperations_.constBegin().key()));
  else if (repository_)
    statusBar()->showMessage(tr("%1 — %2").arg(repository_->name, repository_->branch));
  else
    statusBar()->showMessage(tr("No repository open"));
}

void MainWindow::showNotice(const QString& message, const bool error) {
  if (message.isEmpty()) return;
  statusBar()->setStyleSheet(error ? QStringLiteral("QStatusBar { color: #a54e43; }") : QString{});
  noticeTimer_->start(error ? 4400 : 2800);
  statusBar()->showMessage(message);
}

QString MainWindow::currentAccountId() const {
  if (!repository_) return appState_.activeAccountId;
  return controller_->resolvedAccountId(repository_->path);
}

QString MainWindow::currentCommitHash() const {
  return commitDetail_ ? commitDetail_->fullHash : QString{};
}

}  // namespace relay
