#include "relay/main_window.hpp"
#include "relay/compare_dialog.hpp"

#include "relay/diff_view.hpp"
#include "relay/dialogs.hpp"
#include "relay/item_delegates.hpp"
#include "relay/list_models.hpp"
#include "relay/relay_controller.hpp"
#include "relay/theme.hpp"
#include "relay/ssh_service.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
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
#include <QCloseEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTimer>
#include <utility>
#include <QVBoxLayout>

#include <algorithm>

namespace relay {
namespace {
// Branch picker data for the action that fetches every origin branch; not a ref.
constexpr char fetchOriginBranchesItem[] = "relay:fetch-origin-branches";

// The commit origin/<branch> pointed at in the last repository read, or empty.
QString originTrackingTip(const Repository& repository, const QString& branch) {
  const auto prefix = QStringLiteral("refs/remotes/origin/%1 ").arg(branch);
  for (const auto& line : repository.historyRefState.split(u'\n', Qt::SkipEmptyParts))
    if (line.startsWith(prefix)) return line.mid(prefix.size()).trimmed();
  return {};
}

QString originTrackingTip(const Repository& repository) { return originTrackingTip(repository, repository.branch); }

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
  // Row delegates paint a hover state; without tracking it only appeared
  // when something else (such as scrolling) repainted the row.
  for (auto* view : findChildren<QAbstractItemView*>()) view->setMouseTracking(true);
  noticeTimer_ = new QTimer(this);
  noticeTimer_->setSingleShot(true);
  connect(noticeTimer_, &QTimer::timeout, this, &MainWindow::updateStatus);
  // Save pane widths shortly after a drag ends rather than on every step.
  layoutTimer_ = new QTimer(this);
  layoutTimer_->setSingleShot(true);
  layoutTimer_->setInterval(500);
  connect(layoutTimer_, &QTimer::timeout, this, &MainWindow::saveLayout);
  for (auto* splitter : findChildren<QSplitter*>()) {
    if (splitter->objectName().isEmpty()) continue;
    splitters_.append(splitter);
    connect(splitter, &QSplitter::splitterMoved, layoutTimer_, qOverload<>(&QTimer::start));
  }
  // The sizes set while building the shell are what Reset Layout restores.
  defaultLayout_ = captureLayout();
  // Quit from the menu does not close the window first.
  connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::saveLayout);
  connectController();
  theme::apply(*qApp);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  // The sidebar heading takes the tab bar's exact height, so the line under
  // both is one straight line whatever the font or platform.
  if (contentTabs_ && watched == contentTabs_->tabBar() && event->type() == QEvent::Resize && sidebarHeading_)
    sidebarHeading_->setFixedHeight(contentTabs_->tabBar()->height());
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  saveLayout();
  QMainWindow::closeEvent(event);
}

QJsonObject MainWindow::captureLayout() const {
  QJsonObject splitters;
  for (const auto* splitter : splitters_)
    splitters.insert(splitter->objectName(), QString::fromLatin1(splitter->saveState().toBase64()));
  return {{QStringLiteral("window"), QString::fromLatin1(saveGeometry().toBase64())},
          {QStringLiteral("splitters"), splitters}};
}

void MainWindow::restoreLayout(const QJsonObject& layout, const bool includeWindow) {
  // restoreGeometry() and restoreState() reject malformed data themselves,
  // so a damaged or foreign value leaves the default layout in place.
  if (includeWindow) {
    const auto window = layout.value(QStringLiteral("window")).toString();
    if (!window.isEmpty()) restoreGeometry(QByteArray::fromBase64(window.toLatin1()));
  }
  const auto splitters = layout.value(QStringLiteral("splitters")).toObject();
  for (auto* splitter : splitters_) {
    const auto state = splitters.value(splitter->objectName()).toString();
    if (!state.isEmpty()) splitter->restoreState(QByteArray::fromBase64(state.toLatin1()));
  }
}

void MainWindow::saveLayout() {
  layoutTimer_->stop();
  if (layoutRestored_) controller_->saveLayout(captureLayout());
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
  forcePushAction_ = repositoryMenu->addAction(tr("Force push origin…"), this, &MainWindow::confirmForcePush);
  forcePushAction_->setObjectName(QStringLiteral("forcePushAction"));
  connect(repositoryMenu, &QMenu::aboutToShow, this, [this] {
    createBranchAction_->setEnabled(repository_.has_value() && busyOperations_.isEmpty());
    pullAction_->setEnabled(repository_ && repository_->hasUpstream && busyOperations_.isEmpty());
    forcePushAction_->setEnabled(canForcePush());
  });
  createBranchAction_->setEnabled(false);
  pullAction_->setEnabled(false);
  forcePushAction_->setEnabled(false);

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
  view->addSeparator();
  auto* resetLayout = view->addAction(tr("Reset Layout"), this, [this] {
    restoreLayout(defaultLayout_, false);
    saveLayout();
  });
  resetLayout->setObjectName(QStringLiteral("resetLayoutAction"));

  auto* window = menuBar()->addMenu(tr("&Window"));
  window->addAction(tr("Minimize"), QKeySequence(tr("Ctrl+M")), this, &QWidget::showMinimized);
  window->addAction(tr("Close"), QKeySequence::Close, this, &QWidget::close);
  auto* help = menuBar()->addMenu(tr("&Help"));
  help->addAction(tr("Check Git and GitHub CLI"), controller_, &RelayController::checkRuntimes);
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
  branchPicker_->setObjectName(QStringLiteral("branchPicker"));
  branchPicker_->setMinimumWidth(150);
  branchPicker_->setMaximumWidth(320);
  // Wide enough for the current branch name, up to the maximum.
  branchPicker_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  branchPicker_->addItem(tr("No branch"));
  syncButton_ = new QToolButton(actionRow);
  syncButton_->setObjectName(QStringLiteral("syncButton"));
  syncButton_->setText(tr("Fetch origin"));
  syncButton_->setPopupMode(QToolButton::MenuButtonPopup);
  syncButton_->setAccessibleName(tr("Synchronize with origin"));
  auto* syncMenu = new QMenu(syncButton_);
  auto* fetch = syncMenu->addAction(tr("Fetch origin"), this, [this] {
    if (repository_) controller_->fetchOrigin(currentAccountId());
  });
  syncMenu->addAction(pullAction_);
  auto* push = syncMenu->addAction(tr("Push origin"), this, [this] {
    if (repository_) controller_->pushOrigin(currentAccountId());
  });
  syncMenu->addAction(forcePushAction_);
  connect(syncMenu, &QMenu::aboutToShow, this, [this, fetch, push] {
    const bool remoteReady = repository_ && !repository_->remote.isEmpty() && busyOperations_.isEmpty();
    fetch->setEnabled(remoteReady);
    push->setEnabled(remoteReady);
    pullAction_->setEnabled(remoteReady && repository_->hasUpstream);
    forcePushAction_->setEnabled(canForcePush());
  });
  syncButton_->setMenu(syncMenu);
  syncButton_->setEnabled(false);
  connect(syncButton_, &QToolButton::clicked, this, [this] {
    if (!repository_) return;
    if (repository_->remote.isEmpty()) { showPublishDialog(); return; }
    if (repository_->hasUpstream && repository_->behind > 0) controller_->pullOrigin(currentAccountId());
    else if (!repository_->hasUpstream || repository_->ahead > 0) controller_->pushOrigin(currentAccountId());
    else controller_->fetchOrigin(currentAccountId());
  });
  accountButton_ = new QToolButton(actionRow);
  accountButton_->setPopupMode(QToolButton::InstantPopup);
  accountButton_->setObjectName(QStringLiteral("accountButton"));
  accountButton_->setText(tr("Accounts and SSH identities"));
  accountButton_->setAccessibleName(tr("Git hosting accounts and repository SSH identity"));
  accountMenu_ = new QMenu(accountButton_);
  accountButton_->setMenu(accountMenu_);
  connect(accountMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildAccountMenu);
  actions->addWidget(repositoryButton_);
  actions->addWidget(branchPicker_);
  actions->addWidget(syncButton_);
  actions->addStretch();
  actions->addWidget(accountButton_);
  layout->addWidget(actionRow);

  runtimeBanner_ = new QWidget(root);
  runtimeBanner_->setObjectName(QStringLiteral("runtimeBanner"));
  auto* runtimeLayout = new QHBoxLayout(runtimeBanner_);
  runtimeMessage_ = new QLabel(runtimeBanner_);
  runtimeMessage_->setObjectName(QStringLiteral("runtimeMessage"));
  runtimeMessage_->setTextFormat(Qt::PlainText);
  runtimeMessage_->setWordWrap(true);
  runtimeMessage_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  runtimeRetry_ = new QPushButton(tr("Check again"), runtimeBanner_);
  runtimeRetry_->setObjectName(QStringLiteral("runtimeRetry"));
  connect(runtimeRetry_, &QPushButton::clicked, controller_, &RelayController::checkRuntimes);
  runtimeLayout->addWidget(runtimeMessage_, 1);
  runtimeLayout->addWidget(runtimeRetry_);
  runtimeBanner_->hide();
  layout->addWidget(runtimeBanner_);

  conflictButton_ = new QPushButton(root);
  conflictButton_->setObjectName(QStringLiteral("conflictButton"));
  conflictButton_->hide();
  connect(conflictButton_, &QPushButton::clicked, this, &MainWindow::showConflicts);
  layout->addWidget(conflictButton_);

  auto* workspace = new QSplitter(Qt::Horizontal, root);
  workspace->setObjectName(QStringLiteral("workspaceSplitter"));
  workspace->setChildrenCollapsible(false);
  workspace->setHandleWidth(5);
  workspace->addWidget(buildSidebar(workspace));
  workspaceStack_ = new QStackedWidget(workspace);
  workspaceStack_->addWidget(buildEmptyState(workspaceStack_));
  contentTabs_ = new QTabWidget(workspaceStack_);
  contentTabs_->setObjectName(QStringLiteral("contentTabs"));
  contentTabs_->addTab(buildChangesPage(contentTabs_), tr("Changes"));
  contentTabs_->addTab(buildHistoryPage(contentTabs_), tr("History"));
  contentTabs_->tabBar()->installEventFilter(this);
  workspaceStack_->addWidget(contentTabs_);
  workspace->addWidget(workspaceStack_);
  workspace->setSizes({270, 1150});
  workspace->setStretchFactor(1, 1);
  layout->addWidget(workspace, 1);
  setCentralWidget(root);

  statusBar()->setObjectName(QStringLiteral("statusBar"));
  statusBar()->showMessage(tr("No repository open"));
  statusIdentity_ = new QLabel(tr("No account connected"), this);
  statusIdentity_->setProperty("role", QStringLiteral("meta"));
  repositorySettingsButton_ = new QPushButton(tr("Repository settings"), this);
  repositorySettingsButton_->setObjectName(QStringLiteral("repositorySettingsButton"));
  repositorySettingsButton_->setProperty("kind", QStringLiteral("flat"));
  repositorySettingsButton_->setEnabled(false);
  connect(repositorySettingsButton_, &QPushButton::clicked, this, &MainWindow::showRepositoryAccountDialog);
  statusBar()->addPermanentWidget(statusIdentity_);
  statusBar()->addPermanentWidget(repositorySettingsButton_);

  connect(branchPicker_, &QComboBox::currentIndexChanged, this, [this](const int index) {
    if (!repository_ || index < 0) return;
    const auto ref = branchPicker_->itemData(index).toString();
    { const QSignalBlocker blocker(branchPicker_); branchPicker_->setCurrentIndex(branchPicker_->findData(QStringLiteral("refs/heads/") + repository_->branch)); }
    if (ref == QLatin1StringView(fetchOriginBranchesItem))
      controller_->fetchOrigin(currentAccountId(), true);
    else if (ref.startsWith(QStringLiteral("refs/remotes/")))
      controller_->executeRepositoryAction(RepositoryAction::checkoutRemote, ref.mid(13));
    else if (ref.startsWith(QStringLiteral("refs/heads/")) && ref.mid(11) != repository_->branch)
      controller_->switchBranch(ref.mid(11));
  });
  connect(contentTabs_, &QTabWidget::currentChanged, this, [this](const int index) {
    if (index == 1 && repository_ && historyModel_->rowCount() == 0) controller_->requestHistory(0, 50, {}, historyBranch_->currentData().toString());
  });
}

QWidget* MainWindow::buildSidebar(QWidget* parent) {
  auto* sidebar = new QFrame(parent);
  sidebar->setObjectName(QStringLiteral("sidebar"));
  sidebar->setMinimumWidth(225);
  sidebar->setMaximumWidth(340);
  // The sidebar shares the content grid: its heading is as tall as the tab
  // bar (one continuous line under both), and its two control rows sit on
  // the same lines as the History header's two rows.
  auto* layout = new QVBoxLayout(sidebar);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  auto* heading = sidebarHeading_ = new QWidget(sidebar);
  heading->setFixedHeight(31);  // until the tab bar reports its real height
  heading->setObjectName(QStringLiteral("sidebarHeading"));
  heading->setAttribute(Qt::WA_StyledBackground);
  auto* headingRow = new QHBoxLayout(heading);
  headingRow->setContentsMargins(10, 0, 6, 0);
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
  layout->addWidget(heading);

  auto* filterRow = new QHBoxLayout;
  filterRow->setContentsMargins(6, 5, 6, 5);
  repositoryFilter_ = new QLineEdit(sidebar);
  repositoryFilter_->setObjectName(QStringLiteral("repositoryFilter"));
  repositoryFilter_->setPlaceholderText(tr("Filter repositories"));
  repositoryFilter_->setClearButtonEnabled(true);
  repositoryFilter_->setAccessibleName(tr("Filter repositories"));
  filterRow->addWidget(repositoryFilter_);
  layout->addLayout(filterRow);

  auto* orderRow = new QHBoxLayout;
  orderRow->setContentsMargins(6, 0, 6, 5);
  orderRow->setSpacing(4);
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
  splitter->setObjectName(QStringLiteral("changesSplitter"));
  splitter->setChildrenCollapsible(false);
  splitter->setHandleWidth(5);
  auto* left = new QWidget(splitter);
  auto* leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(0);
  auto* fileHeader = new QFrame(left);
  auto* fileHeaderLayout = new QHBoxLayout(fileHeader);
  // Same 5px rhythm as the History header, so row 1 lines up across tabs;
  // the checkbox sits over the file rows' checkboxes.
  fileHeaderLayout->setContentsMargins(8, 5, 6, 5);
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
  commitLayout->setContentsMargins(6, 6, 6, 6);
  commitLayout->setSpacing(5);
  commitIdentity_ = new QLabel(tr("Connect an account before committing"), commitBox);
  commitIdentity_->setProperty("role", QStringLiteral("meta"));
  commitIdentity_->setWordWrap(true);
  commitSummary_ = new QLineEdit(commitBox);
  commitSummary_->setObjectName(QStringLiteral("commitSummary"));
  commitSummary_->setPlaceholderText(tr("Summary (required)"));
  commitSummary_->setAccessibleName(tr("Commit summary"));
  commitDescription_ = new QPlainTextEdit(commitBox);
  commitDescription_->setPlaceholderText(tr("Description"));
  commitDescription_->setAccessibleName(tr("Commit description"));
  commitDescription_->setMaximumHeight(78);
  commitCoAuthors_ = new QLineEdit(commitBox);
  commitCoAuthors_->setObjectName(QStringLiteral("commitCoAuthors"));
  commitCoAuthors_->setPlaceholderText(tr("Co-authors: Name <email>, …"));
  commitCoAuthors_->setAccessibleName(tr("Commit co-authors"));
  coAuthorButton_ = new QToolButton(commitBox);
  coAuthorButton_->setObjectName(QStringLiteral("coAuthorButton"));
  coAuthorButton_->setText(tr("Recent"));
  coAuthorButton_->setToolTip(tr("Add a recent author of this repository as a co-author"));
  coAuthorButton_->setAccessibleName(tr("Add a recent co-author"));
  coAuthorButton_->setPopupMode(QToolButton::InstantPopup);
  auto* coAuthorMenu = new QMenu(coAuthorButton_);
  coAuthorButton_->setMenu(coAuthorMenu);
  connect(coAuthorMenu, &QMenu::aboutToShow, this, [this, coAuthorMenu] {
    coAuthorMenu->clear();
    QStringList seen;
    for (const auto& item : repository_ ? repository_->history : QList<HistoryItem>{}) {
      const auto entry = QStringLiteral("%1 <%2>").arg(item.author, item.email);
      if (item.email.isEmpty() || seen.contains(item.email, Qt::CaseInsensitive) || commitCoAuthors_->text().contains(item.email, Qt::CaseInsensitive)) continue;
      seen.append(item.email);
      coAuthorMenu->addAction(entry, this, [this, entry] {
        const auto current = commitCoAuthors_->text().trimmed();
        commitCoAuthors_->setText(current.isEmpty() ? entry : current + QStringLiteral(", ") + entry);
      });
      if (seen.size() == 15) break;
    }
    if (coAuthorMenu->isEmpty()) coAuthorMenu->addAction(tr("No other recent authors"))->setEnabled(false);
  });
  auto* coAuthorRow = new QHBoxLayout;
  coAuthorRow->setSpacing(6);
  coAuthorRow->addWidget(commitCoAuthors_, 1);
  coAuthorRow->addWidget(coAuthorButton_);
  commitButton_ = new QPushButton(tr("Commit selected files"), commitBox);
  commitButton_->setObjectName(QStringLiteral("commitButton"));
  commitButton_->setProperty("kind", QStringLiteral("primary"));
  commitButton_->setEnabled(false);
  commitLayout->addWidget(commitIdentity_);
  commitLayout->addWidget(commitSummary_);
  commitLayout->addWidget(commitDescription_);
  commitLayout->addLayout(coAuthorRow);
  commitLayout->addWidget(commitButton_);
  leftLayout->addWidget(commitBox);

  auto* right = new QWidget(splitter);
  auto* rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  auto* diffTitle = sectionLabel(tr("Diff"), right);
  // A 34px header row, centred on the same line as the file list header.
  diffTitle->setContentsMargins(10, 0, 10, 0);
  diffTitle->setFixedHeight(34);
  diffTitle->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
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
                        commitDescription_->toPlainText(), currentAccountId(), commitCoAuthors_->text());
  });
  return splitter;
}

QWidget* MainWindow::buildHistoryPage(QWidget* parent) {
  auto* splitter = new QSplitter(Qt::Horizontal, parent);
  splitter->setObjectName(QStringLiteral("historySplitter"));
  splitter->setChildrenCollapsible(false);
  splitter->setHandleWidth(5);
  auto* left = new QWidget(splitter);
  auto* leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(0);  // gaps come from row margins, as in the sidebar
  // Two dense rows: what to show, then what to find in it.
  auto* scopeRow = new QHBoxLayout;
  scopeRow->setContentsMargins(6, 5, 6, 5);
  scopeRow->setSpacing(4);
  historyBranch_ = new QComboBox(left);
  historyBranch_->setObjectName(QStringLiteral("historyBranch"));
  historyBranch_->setAccessibleName(tr("Browse branch history without checkout"));
  historyBranch_->setToolTip(tr("Browse local and fetched remote branches without changing your working files."));
  historyMode_ = new QComboBox(left);
  historyMode_->setObjectName(QStringLiteral("historyMode"));
  historyMode_->setAccessibleName(tr("History display"));
  historyMode_->addItem(tr("List"));
  historyMode_->addItem(tr("Graph"));
  historyMode_->setToolTip(tr("Graph shows parallel work and merges across all branches."));
  checkoutHistoryBranch_ = new QPushButton(tr("Check out"), left);
  checkoutHistoryBranch_->setObjectName(QStringLiteral("checkoutHistoryBranch"));
  checkoutHistoryBranch_->setToolTip(tr("Check out the branch shown here"));
  createHistoryBranch_ = new QPushButton(tr("Branch from…"), left);
  createHistoryBranch_->setObjectName(QStringLiteral("createHistoryBranch"));
  createHistoryBranch_->setToolTip(tr("Create and check out a new branch from the branch shown here"));
  fetchHistoryBranches_ = new QPushButton(tr("Fetch all"), left);
  fetchHistoryBranches_->setObjectName(QStringLiteral("fetchHistoryBranches"));
  fetchHistoryBranches_->setToolTip(tr("Discover every branch on origin, including in single-branch clones. Does not change your checked-out branch or files."));
  historyFocus_ = new QToolButton(left);
  historyFocus_->setObjectName(QStringLiteral("historyFocus"));
  historyFocus_->setText(tr("Focus"));
  historyFocus_->setCheckable(true);
  historyFocus_->setToolTip(tr("Show only the history, full screen (Esc to leave)"));
  historyFocus_->setAccessibleName(tr("Full-screen history"));
  scopeRow->addWidget(historyBranch_, 1);
  scopeRow->addWidget(historyMode_);
  scopeRow->addWidget(checkoutHistoryBranch_);
  scopeRow->addWidget(createHistoryBranch_);
  scopeRow->addWidget(fetchHistoryBranches_);
  scopeRow->addWidget(historyFocus_);
  leftLayout->addLayout(scopeRow);

  auto* searchRow = new QHBoxLayout;
  searchRow->setContentsMargins(6, 0, 6, 5);
  searchRow->setSpacing(4);
  historySearchField_ = new QComboBox(left);
  historySearchField_->setObjectName(QStringLiteral("historySearchField"));
  historySearchField_->setAccessibleName(tr("Search in"));
  historySearchField_->addItem(tr("All"), static_cast<int>(HistoryCommitListModel::SearchField::any));
  historySearchField_->addItem(tr("Message"), static_cast<int>(HistoryCommitListModel::SearchField::message));
  historySearchField_->addItem(tr("Author"), static_cast<int>(HistoryCommitListModel::SearchField::author));
  historySearchField_->addItem(tr("Branch"), static_cast<int>(HistoryCommitListModel::SearchField::branch));
  historySearchField_->addItem(tr("Hash"), static_cast<int>(HistoryCommitListModel::SearchField::hash));
  historySearch_ = new QLineEdit(left);
  historySearch_->setObjectName(QStringLiteral("historySearch"));
  historySearch_->setPlaceholderText(tr("Find in history  (Enter next, Shift+Enter previous)"));
  historySearch_->setClearButtonEnabled(true);
  historySearch_->setAccessibleName(tr("Search commits"));
  historyHighlight_ = new QToolButton(left);
  historyHighlight_->setObjectName(QStringLiteral("historyHighlight"));
  historyHighlight_->setText(tr("Highlight"));
  historyHighlight_->setCheckable(true);
  historyHighlight_->setChecked(true);
  historyHighlight_->setToolTip(tr("Highlight matches and dim the rest, keeping the graph. Turn off to show only matches and search all history."));
  historyMatches_ = new QLabel(left);
  historyMatches_->setObjectName(QStringLiteral("historyMatches"));
  historyMatches_->setProperty("role", QStringLiteral("meta"));
  historyMatches_->setMinimumWidth(48);
  historyMatches_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  // Square buttons with the bundled chevrons; icons follow the theme.
  auto* previousMatch = historyPreviousMatch_ = new QToolButton(left);
  previousMatch->setObjectName(QStringLiteral("historyPreviousMatch"));
  previousMatch->setProperty("kind", QStringLiteral("square"));
  previousMatch->setIconSize(QSize(12, 12));
  previousMatch->setToolTip(tr("Previous match (Shift+Enter)"));
  previousMatch->setAccessibleName(tr("Previous match"));
  auto* nextMatch = historyNextMatch_ = new QToolButton(left);
  nextMatch->setObjectName(QStringLiteral("historyNextMatch"));
  nextMatch->setProperty("kind", QStringLiteral("square"));
  nextMatch->setIconSize(QSize(12, 12));
  nextMatch->setToolTip(tr("Next match (Enter)"));
  nextMatch->setAccessibleName(tr("Next match"));
  searchRow->addWidget(historySearchField_);
  searchRow->addWidget(historySearch_, 1);
  searchRow->addWidget(historyHighlight_);
  searchRow->addWidget(historyMatches_);
  searchRow->addWidget(previousMatch);
  searchRow->addWidget(nextMatch);
  leftLayout->addLayout(searchRow);

  connect(historyMode_, &QComboBox::currentIndexChanged, this, [this](int index) {
    auto preferences = appState_.preferences;
    if (preferences.graphHistory == (index == 1)) return;
    preferences.graphHistory = index == 1;
    controller_->setPreferences(preferences);
  });
  connect(fetchHistoryBranches_, &QPushButton::clicked, this, [this] {
    controller_->fetchOrigin(currentAccountId(), true);
  });
  connect(historyFocus_, &QToolButton::toggled, this, &MainWindow::setHistoryFocus);
  auto* leaveFocus = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  connect(leaveFocus, &QShortcut::activated, this, [this] { if (historyFocus_->isChecked()) historyFocus_->setChecked(false); });
  connect(previousMatch, &QToolButton::clicked, this, [this] { stepHistoryMatch(-1); });
  connect(nextMatch, &QToolButton::clicked, this, [this] { stepHistoryMatch(1); });
  connect(historySearch_, &QLineEdit::returnPressed, this, [this] { stepHistoryMatch(1); });
  auto* previousShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Return), historySearch_);
  previousShortcut->setContext(Qt::WidgetShortcut);
  connect(previousShortcut, &QShortcut::activated, this, [this] { stepHistoryMatch(-1); });
  connect(historyBranch_, &QComboBox::currentIndexChanged, this, [this] { reloadHistory(); });
  connect(checkoutHistoryBranch_, &QPushButton::clicked, this, [this] {
    const auto ref = historyBranch_->currentData().toString();
    if (ref.startsWith(QStringLiteral("refs/remotes/")))
      controller_->executeRepositoryAction(RepositoryAction::checkoutRemote, ref.mid(13));
    else if (ref.startsWith(QStringLiteral("refs/heads/"))) controller_->switchBranch(ref.mid(11));
  });
  connect(createHistoryBranch_, &QPushButton::clicked, this, [this] {
    bool accepted = false;
    const auto name = QInputDialog::getText(this, tr("New branch"),
        tr("Create and check out a branch from %1:").arg(historyBranch_->currentData().toString().isEmpty() && repository_ ? repository_->branch : historyBranch_->currentText()),
        QLineEdit::Normal, {}, &accepted).trimmed();
    if (accepted && !name.isEmpty()) controller_->createBranch(name, historyBranch_->currentData().toString());
  });
  historyModel_ = new HistoryCommitListModel(this);
  historyModel_->setSearchMode(HistoryCommitListModel::SearchMode::highlight);
  historyList_ = new QListView(left);
  historyList_->setObjectName(QStringLiteral("historyList"));
  historyList_->setModel(historyModel_);
  historyList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  connect(historyList_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { updateWorkflowActions(); });
  historyList_->setItemDelegate(new HistoryCommitItemDelegate(historyList_));
  historyList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  // Rows elide to the view; they never need sideways scrolling.
  historyList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  historyList_->setAccessibleName(tr("Commit history"));
  leftLayout->addWidget(historyList_, 1);

  auto* right = new QSplitter(Qt::Vertical, splitter);
  right->setObjectName(QStringLiteral("historyDetailSplitter"));
  right->setHandleWidth(5);
  right->setChildrenCollapsible(false);
  historyDetails_ = right;
  auto* details = new QWidget(right);
  details->setMinimumHeight(100);
  auto* rightLayout = new QVBoxLayout(details);
  rightLayout->setContentsMargins(10, 5, 6, 6);
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
  commitFileList_->setMinimumHeight(40);
  historyDiff_ = new DiffView(right);
  rightLayout->addLayout(headingRow);
  rightLayout->addWidget(historyMetadata_);
  rightLayout->addWidget(historyBody_);
  rightLayout->addWidget(sectionLabel(tr("Changed files"), right));
  rightLayout->addWidget(commitFileList_);
  right->addWidget(details);
  right->addWidget(historyDiff_);
  historyDiff_->setMinimumHeight(100);
  right->setSizes({280, 400});
  right->setStretchFactor(1, 1);
  splitter->addWidget(left);
  splitter->addWidget(right);
  splitter->setSizes({430, 720});
  splitter->setStretchFactor(1, 1);

  connect(historyModel_, &QAbstractItemModel::modelAboutToBeReset, this, &MainWindow::clearCommitDetail);
  connect(historySearch_, &QLineEdit::textChanged, historyModel_, &HistoryCommitListModel::setSearch);
  connect(historySearchField_, &QComboBox::currentIndexChanged, this, [this] {
    historyModel_->setSearchField(static_cast<HistoryCommitListModel::SearchField>(historySearchField_->currentData().toInt()));
    if (historyModel_->searchMode() == HistoryCommitListModel::SearchMode::filter) historySearchTimer_->start();
  });
  connect(historyHighlight_, &QToolButton::toggled, this, [this](const bool highlight) {
    // Leaving whole-history results returns to the normal pages first.
    if (highlight && historyModel_->showingSearchResults()) reloadHistory();
    historyModel_->setSearchMode(highlight ? HistoryCommitListModel::SearchMode::highlight
                                           : HistoryCommitListModel::SearchMode::filter);
    if (!highlight) historySearchTimer_->start();
  });
  connect(historyModel_, &QAbstractItemModel::modelReset, this, &MainWindow::updateHistoryMatches);
  connect(historyModel_, &QAbstractItemModel::rowsInserted, this, &MainWindow::updateHistoryMatches);
  // Typing in highlight mode brings the first match into view.
  connect(historySearch_, &QLineEdit::textChanged, this, [this] {
    if (historyModel_->searchMode() != HistoryCommitListModel::SearchMode::highlight) return;
    if (const auto rows = historyModel_->matchingRows(); !rows.isEmpty())
      historyList_->scrollTo(historyModel_->index(rows.first()), QAbstractItemView::PositionAtCenter);
  });
  // Loaded commits filter as you type; once typing pauses, Git searches the
  // rest of the history if it is not all loaded.
  historySearchTimer_ = new QTimer(this);
  historySearchTimer_->setSingleShot(true);
  historySearchTimer_->setInterval(400);
  connect(historySearch_, &QLineEdit::textChanged, historySearchTimer_, qOverload<>(&QTimer::start));
  connect(historySearchTimer_, &QTimer::timeout, this, [this] {
    if (!repository_) return;
    const auto query = historySearch_->text().trimmed();
    const auto reference = historyBranch_->currentData().toString();
    if (query.isEmpty()) {
      if (historyModel_->showingSearchResults()) {
        historyModel_->clear();
        clearCommitDetail();
        controller_->requestHistory(0, 50, {}, reference);
      }
      return;
    }
    // Highlight works on loaded commits and keeps the graph; only filtering
    // replaces them with Git's whole-history results. Git searches message,
    // author and hash, so a branch filter stays on loaded commits.
    if (historyModel_->searchMode() != HistoryCommitListModel::SearchMode::filter ||
        historyModel_->searchField() == HistoryCommitListModel::SearchField::branch) return;
    if (historyModel_->endOfHistory() && !historyModel_->showingSearchResults()) return;
    controller_->searchHistory(query, reference);
  });
  connect(historyList_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
    updateHistoryMatches();
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
  connect(controller_, &RelayController::runtimeIssuesChanged, this, [this](const QStringList& issues) {
    runtimeMessage_->setText(issues.join(u'\n'));
    runtimeBanner_->setVisible(!issues.isEmpty());
  });
  connect(controller_, &RelayController::busyChanged, this, [this](const QString& operation, bool busy) {
    if (operation == QStringLiteral("runtime-check")) runtimeRetry_->setEnabled(!busy);
  });
  connect(controller_, &RelayController::pullDiverged, this, [this](const QString& path, const QString& upstream) {
    if (!repository_ || repository_->path != path) return;
    const auto shortName = upstream.mid(QStringLiteral("refs/remotes/").size());
    QMessageBox box(QMessageBox::Question, tr("Pull origin"),
        tr("%1 and %2 both have new commits, so a fast-forward is not possible.")
            .arg(repository_->branch, shortName), QMessageBox::Cancel, this);
    box.setObjectName(QStringLiteral("pullDivergedDialog"));
    box.setInformativeText(tr("Merge creates a merge commit. Rebase replays your local commits on top of %1 "
                              "and is only possible while they have not been pushed.").arg(shortName));
    auto* merge = box.addButton(tr("Merge"), QMessageBox::AcceptRole);
    merge->setObjectName(QStringLiteral("pullMergeButton"));
    auto* rebase = box.addButton(tr("Rebase"), QMessageBox::AcceptRole);
    rebase->setObjectName(QStringLiteral("pullRebaseButton"));
    box.setDefaultButton(merge);
    box.exec();
    if (box.clickedButton() == merge) controller_->executeRepositoryAction(RepositoryAction::mergeBranch, upstream);
    else if (box.clickedButton() == rebase) {
      preconfirmedRebase_ = upstream;
      controller_->planRebase(upstream);
    }
  });
  connect(controller_, &RelayController::rebasePlanReady, this, [this](const QString& path, const RebasePlan& plan) {
    if (!repository_ || repository_->path != path) return;
    const auto onto = plan.onto.section(u'/', 2);
    const bool preconfirmed = std::exchange(preconfirmedRebase_, QString{}) == plan.onto;
    if (plan.replayed == 0) { showNotice(tr("%1 already contains every commit of %2; there is nothing to rebase.").arg(onto, repository_->branch)); return; }
    if (plan.published == 0 && preconfirmed) {
      controller_->executeRepositoryAction(RepositoryAction::rebaseBranch, plan.onto);
      return;
    }
    if (plan.published == 0) {
      QMessageBox box(QMessageBox::Question, tr("Rebase current branch"),
          tr("Replay %n commit(s) of %1 on top of %2?", nullptr, plan.replayed).arg(repository_->branch, onto),
          QMessageBox::Ok | QMessageBox::Cancel, this);
      box.setObjectName(QStringLiteral("rebaseDialog"));
      box.setDefaultButton(QMessageBox::Cancel);
      if (box.exec() == QMessageBox::Ok) controller_->executeRepositoryAction(RepositoryAction::rebaseBranch, plan.onto);
      return;
    }
    QMessageBox box(QMessageBox::Warning, tr("Rebase current branch"),
        tr("%1 of the %2 commits to replay are already on a remote branch.").arg(plan.published).arg(plan.replayed),
        QMessageBox::Cancel, this);
    box.setObjectName(QStringLiteral("rebasePublishedDialog"));
    box.setTextFormat(Qt::PlainText);
    box.setInformativeText(tr("Rebasing %1 onto %2 rewrites them. Afterwards origin needs a force push, and anyone who "
                              "already has those commits must reconcile their work. Merging keeps history unchanged.")
                               .arg(repository_->branch, onto));
    auto* rewrite = box.addButton(tr("Rebase and rewrite"), QMessageBox::DestructiveRole);
    rewrite->setObjectName(QStringLiteral("rebasePublishedButton"));
    auto* merge = box.addButton(tr("Merge instead"), QMessageBox::AcceptRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == merge) {
      controller_->executeRepositoryAction(RepositoryAction::mergeBranch,
          plan.onto.startsWith(QStringLiteral("refs/heads/")) ? plan.onto.mid(11) : plan.onto);
    } else if (box.clickedButton() == rewrite) {
      forcePushOfferPath_ = path;
      controller_->executeRepositoryAction(RepositoryAction::rebasePublished, plan.onto);
    }
  });
  connect(controller_, &RelayController::originTagChanged, this, [this](const QString& path, const QString& message) {
    if (repository_ && repository_->path == path) showNotice(message);
  });
  connect(controller_, &RelayController::originTagsReady, this, [this](const QString& path, const QList<QPair<QString, QString>>& tags) {
    if (!repository_ || repository_->path != path) return;
    if (tags.isEmpty()) { showNotice(tr("Origin has no tags.")); return; }
    QStringList names;
    for (const auto& tag : tags) names.append(tag.first);
    bool accepted = false;
    const auto name = QInputDialog::getItem(this, tr("Delete tag on origin"), tr("Tag on origin"), names,
                                            int(names.size()) - 1, false, &accepted);
    if (!accepted || !repository_ || repository_->path != path) return;
    const auto expected = tags.at(names.indexOf(name)).second;
    QMessageBox box(QMessageBox::Warning, tr("Delete tag on origin"), tr("Delete tag %1 on origin?").arg(name), QMessageBox::Cancel, this);
    box.setObjectName(QStringLiteral("deleteOriginTagDialog"));
    box.setTextFormat(Qt::PlainText);
    box.setInformativeText(tr("It is removed for everyone; your local tags are kept. It points to %1. "
                              "Relay refuses the deletion if the tag on origin has changed since this list.").arg(expected));
    auto* remove = box.addButton(tr("Delete on origin"), QMessageBox::DestructiveRole);
    remove->setObjectName(QStringLiteral("deleteOriginTagButton"));
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == remove) controller_->deleteOriginTag(currentAccountId(), name, expected);
  });
  connect(controller_, &RelayController::editorsDetected, this, [this](const QList<QPair<QString, QString>>& editors) {
    editors_ = editors;
    updateEditorAction();
  });
  connect(controller_, &RelayController::unpublishedCommitsReady, this, [this](const QString& path, const QList<UnpublishedCommit>& commits) {
    if (!repository_ || repository_->path != path) return;
    if (commits.size() < 2) { showNotice(tr("Squashing or reordering needs at least two commits that are not on a remote yet.")); return; }
    if (commits.size() > 100) { showNotice(tr("Relay rewrites at most 100 unpublished commits."), true); return; }
    if (std::any_of(commits.cbegin(), commits.cend(), [](const UnpublishedCommit& commit) { return commit.merge; })) {
      showNotice(tr("Unpublished merge commits cannot be squashed or reordered here."), true);
      return;
    }
    CommitRewriteDialog dialog(commits, this);
    if (dialog.exec() == QDialog::Accepted && repository_ && repository_->path == path)
      controller_->rewriteUnpublishedCommits(dialog.steps());
  });
  connect(controller_, &RelayController::pullRequestReady, this, [this](const QString& path, const QString& url, const bool existing) {
    if (!repository_ || repository_->path != path || !url.startsWith(QStringLiteral("https://github.com/"))) return;
    statusBar()->showMessage(existing ? tr("Opening the pull request in your browser") : tr("Opening GitHub to create a pull request"), 5000);
    QDesktopServices::openUrl(QUrl(url));
  });
  connect(controller_, &RelayController::commitCreated, this, [this](const QString& path) {
    commitDrafts_.remove(path);
    if (repository_ && repository_->path == path) {
      commitSummary_->clear();
      commitDescription_->clear();
      commitCoAuthors_->clear();
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
    if (compareDialog_) compareDialog_->close();
    if (repository_) commitDrafts_.insert(repository_->path, {commitSummary_->text(), commitDescription_->toPlainText()});
    repository_.reset();
    applyState(controller_->state());
    updateWorkflowActions();
    commitSummary_->clear();
    commitDescription_->clear();
    commitCoAuthors_->clear();
    clearCommitDetail();
    createBranchAction_->setEnabled(false);
    pullAction_->setEnabled(false);
    forcePushAction_->setEnabled(false);
    branchPicker_->setEnabled(false);
    workspaceStack_->setCurrentIndex(0);
    repositoryButton_->setText(tr("Choose a repository"));
    branchPicker_->clear();
    branchPicker_->addItem(tr("No branch"));
    updateHistoryBranches();
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
            if (historyModel_->anchor().isEmpty() || historyModel_->anchor() != page.anchor) {
              historyModel_->resetPage(page);
              // A reload replaces search results; search the new history again.
              if (!historySearch_->text().trimmed().isEmpty() && !page.endOfHistory) historySearchTimer_->start();
            } else static_cast<void>(historyModel_->appendPage(page));
          });
  connect(controller_, &RelayController::historySearchReady, this,
          [this](const QString& repositoryPath, const QString& query, const QList<HistoryCommit>& commits, const bool truncated) {
            if (!repository_ || repository_->path != repositoryPath || query != historySearch_->text().trimmed()) return;
            historyModel_->showSearchResults(commits);
            clearCommitDetail();
            if (truncated) showNotice(tr("Showing the newest %1 matching commits.").arg(commits.size()));
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
                     QLocale().toString(detail.committerDate.toLocalTime(), QLocale::ShortFormat))
                + (detail.isSigned ? tr(" · Signed") : QString{}));
            historyBody_->setText(detail.body);
            commitFileModel_->setFiles(detail.files);
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
    // After a confirmed rewrite of published commits finishes (possibly after
    // conflicts), offer the lease-protected force push once.
    if (!busy && operation == QStringLiteral("repository-action") && !forcePushOfferPath_.isEmpty() && repository_ &&
        repository_->path == forcePushOfferPath_ && repository_->pendingOperation.isEmpty()) {
      forcePushOfferPath_.clear();
      // Deferred: this operation still counts as busy until the handler ends.
      QTimer::singleShot(0, this, [this] {
        if (repository_ && repository_->ahead > 0 && repository_->behind > 0 && canForcePush()) confirmForcePush();
      });
    }
    // "Push first" from the pull request prompt: continue only if the push
    // left nothing unpushed. A failed push has already reported its error.
    if (!busy && operation == QStringLiteral("push") && !pullRequestAfterPush_.isEmpty()) {
      const auto path = std::exchange(pullRequestAfterPush_, QString{});
      if (repository_ && repository_->path == path && repository_->ahead == 0)
        QTimer::singleShot(0, this, [this] { controller_->openPullRequest(currentAccountId()); });
    }
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
    updateHistoryBranchActions();
    createBranchAction_->setEnabled(repository_.has_value() && busyOperations_.isEmpty());
    pullAction_->setEnabled(repository_ && repository_->hasUpstream && busyOperations_.isEmpty());
    forcePushAction_->setEnabled(canForcePush());
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
  if (!layoutRestored_) {
    // The first state is the stored one.
    restoreLayout(state.preferences.layout, true);
    layoutRestored_ = true;
  }
  QString newlyConnected;
  if (accountConnectionPending_) {
    const auto iterator = std::find_if(
        state.accounts.cbegin(), state.accounts.cend(), [this](const Account& candidate) {
          return accountNamed(appState_, candidate.id) == nullptr;
        });
    if (iterator != state.accounts.cend()) newlyConnected = iterator->id;
  }
  const bool historyModeChanged = appState_.preferences.graphHistory != state.preferences.graphHistory;
  const bool themeChanged = appState_.preferences.themeId != state.preferences.themeId ||
      appState_.preferences.customTheme != state.preferences.customTheme;
  appState_ = state;
  if (themeChanged) {
    theme::configure(state.preferences.themeId, state.preferences.customTheme);
    theme::apply(*qApp);
  }
  { const QSignalBlocker blocker(historyMode_); historyMode_->setCurrentIndex(state.preferences.graphHistory ? 1 : 0); }
  historyModel_->setGraphEnabled(state.preferences.graphHistory);
  if (historyModeChanged) {
    updateHistoryBranches();
    historyModel_->clear();
    clearCommitDetail();
    if (repository_) controller_->requestHistory(0, 50, {}, historyBranch_->currentData().toString());
  }
  workingDiff_->setCodeFontSize(state.preferences.diffFontSize);
  historyDiff_->setCodeFontSize(state.preferences.diffFontSize);
  if (compareDialog_) compareDialog_->setDiffFontSize(state.preferences.diffFontSize);
  {
    const bool dark = theme::colors().panel.lightness() < 128;
    historyPreviousMatch_->setIcon(QIcon(dark ? QStringLiteral(":/relay/chevron-up-dark.svg") : QStringLiteral(":/relay/chevron-up-light.svg")));
    historyNextMatch_->setIcon(QIcon(dark ? QStringLiteral(":/relay/chevron-dark.svg") : QStringLiteral(":/relay/chevron-light.svg")));
  }
  updateEditorAction();
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
                                 : tr("Accounts and SSH identities"));
  statusIdentity_->setText(active ? tr("Signed in as @%1").arg(active->handle)
                                  : state.forgeAccounts.isEmpty() ? tr("No account connected")
                                      : tr("%1 server accounts connected").arg(state.forgeAccounts.size()));
  if (repository_ && SshService::parseRemote(repository_->remote)) {
    const auto profileId = controller_->resolvedSshProfileId(repository_->path);
    const auto profile = std::find_if(state.sshProfiles.cbegin(), state.sshProfiles.cend(),
        [&profileId](const SshProfile& value) { return value.id == profileId; });
    const auto label = profile == state.sshProfiles.cend() ? tr("SSH agent") : profile->label;
    accountButton_->setText(tr("SSH · %1").arg(label));
    statusIdentity_->setText(tr("Using SSH · %1").arg(label));
  }
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
  if (!sameRepository) { const QSignalBlocker blocker(historyBranch_); historyBranch_->clear(); }
  repository_ = repository;
  updateHistoryBranches();
  if (compareDialog_ && compareDialog_->isVisible()) {
    if (sameRepository) compareDialog_->setRepository(repository);
    else compareDialog_->close();
  }
  workspaceStack_->setCurrentIndex(1);
  repositoryButton_->setText(QStringLiteral("%1  ·  %2").arg(repository.name, repository.owner));
  repositoryButton_->setToolTip(repository.path);
  {
    const QSignalBlocker blocker(branchPicker_);
    branchPicker_->clear();
    for (const auto& branch : repository.branches)
      branchPicker_->addItem(branch, QStringLiteral("refs/heads/") + branch);
    if (!repository.branches.contains(repository.branch)) branchPicker_->addItem(repository.branch, QStringLiteral("refs/heads/") + repository.branch);
    // A remote branch with a local namesake would only switch to that local
    // branch, which is already listed above.
    for (const auto& branch : repository.remoteBranches)
      if (!repository.branches.contains(branch.mid(branch.indexOf(u'/') + 1)))
        branchPicker_->addItem(tr("Remote · %1").arg(branch), QStringLiteral("refs/remotes/") + branch);
    // Remote branches are only what the last fetch recorded. A branch that so
    // far exists only on origin appears here after this fetch.
    if (!repository.remote.isEmpty()) {
      branchPicker_->insertSeparator(branchPicker_->count());
      branchPicker_->addItem(tr("Fetch all branches from origin"), QString::fromLatin1(fetchOriginBranchesItem));
    }
    branchPicker_->setCurrentIndex(branchPicker_->findData(QStringLiteral("refs/heads/") + repository.branch));
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
  if (contentTabs_->currentIndex() == 1 && historyModel_->rowCount() == 0) controller_->requestHistory(0, 50, {}, historyBranch_->currentData().toString());
  branchPicker_->setEnabled(busyOperations_.isEmpty());
  createBranchAction_->setEnabled(busyOperations_.isEmpty());
  pullAction_->setEnabled(repository.hasUpstream && busyOperations_.isEmpty());
  forcePushAction_->setEnabled(canForcePush());
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
  auto* signing = accountMenu_->addMenu(tr("Commit signing"));
  signing->setObjectName(QStringLiteral("commitSigningMenu"));
  signing->setEnabled(!appState_.accounts.isEmpty());
  for (const auto& account : appState_.accounts) {
    auto* menu = signing->addMenu(account.signingKey.isEmpty()
        ? tr("@%1 — Git configuration").arg(account.handle)
        : tr("@%1 — %2").arg(account.handle, QFileInfo(account.signingKey).fileName()));
    menu->addAction(tr("Sign with SSH key…"), this, [this, id = account.id, current = account.signingKey] {
      const auto start = current.isEmpty() ? QDir::home().filePath(QStringLiteral(".ssh")) : QFileInfo(current).absolutePath();
      const auto path = QFileDialog::getOpenFileName(this, tr("SSH signing key"), start);
      if (!path.isEmpty()) controller_->setAccountSigningKey(id, path);
    });
    auto* follow = menu->addAction(tr("Follow Git configuration"), this, [this, id = account.id] {
      controller_->setAccountSigningKey(id, {});
    });
    follow->setCheckable(true);
    follow->setChecked(account.signingKey.isEmpty());
  }
  accountMenu_->addAction(tr("Manage accounts…"), this, &MainWindow::showAccountsDialog);
  accountMenu_->addAction(tr("Gitea / GitLab accounts and repositories…"), this, &MainWindow::showForgeDialog);
  accountMenu_->addSeparator();
  accountMenu_->addSection(tr("SSH identity for this repository"));
  const auto remote = repository_ ? SshService::parseRemote(repository_->remote) : std::nullopt;
  const bool supportsSsh = remote.has_value();
  const auto selected = repository_ ? controller_->resolvedSshProfileId(repository_->path) : QString{};
  auto* useAgent = accountMenu_->addAction(tr("Use SSH agent and configuration"));
  useAgent->setObjectName(QStringLiteral("useSshAgentAction"));
  useAgent->setCheckable(true);
  useAgent->setChecked(selected.isEmpty());
  useAgent->setEnabled(supportsSsh && busyOperations_.isEmpty());
  connect(useAgent, &QAction::triggered, this, [this] {
    if (repository_) controller_->setRepositorySshProfile(repository_->path, {});
  });
  for (const auto& profile : appState_.sshProfiles) {
    auto* action = accountMenu_->addAction(QStringLiteral("%1 (%2)").arg(profile.label, profile.host));
    action->setObjectName(QStringLiteral("sshProfileAction"));
    action->setData(profile.id);
    action->setCheckable(true);
    action->setChecked(profile.id == selected);
    action->setEnabled(supportsSsh && busyOperations_.isEmpty() &&
                       remote->host.compare(profile.host, Qt::CaseInsensitive) == 0);
    connect(action, &QAction::triggered, this, [this, id = profile.id] {
      if (repository_) controller_->setRepositorySshProfile(repository_->path, id);
    });
  }
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
  dialog.setEditors(editors_);
  // Editors installed since startup appear the next time.
  controller_->detectEditors();
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
  dialog.setObjectName(QStringLiteral("repositorySettingsDialog"));
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
  accountCombo->setObjectName(QStringLiteral("repositoryAccountCombo"));
  accountCombo->addItem(tr("Follow the active account"), QString{});
  for (const auto& accountEntry : appState_.accounts) {
    accountCombo->addItem(QStringLiteral("%1  @%2").arg(accountEntry.name, accountEntry.handle),
                          accountEntry.id);
  }
  accountCombo->setCurrentIndex(std::max(0, accountCombo->findData(
      controller_->boundAccountId(repository_->path))));
  auto* ssh = new QComboBox(&dialog);
  ssh->addItem(tr("Use my SSH agent and ~/.ssh/config"), QString{});
  for (const auto& profile : appState_.sshProfiles)
    ssh->addItem(QStringLiteral("%1 — %2").arg(profile.label, profile.host), profile.id);
  ssh->setCurrentIndex(std::max(0, ssh->findData(
      controller_->resolvedSshProfileId(repository_->path))));
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

void MainWindow::updateHistoryMatches() {
  if (historySearch_->text().trimmed().isEmpty()) { historyMatches_->clear(); return; }
  const auto rows = historyModel_->matchingRows();
  const auto current = historyList_->currentIndex().row();
  const auto position = rows.indexOf(current);
  // "+" means more history exists than is loaded.
  const auto more = historyModel_->endOfHistory() ? QString{} : QStringLiteral("+");
  historyMatches_->setText(position >= 0 ? tr("%1/%2%3").arg(position + 1).arg(rows.size()).arg(more)
                                         : tr("%1%2").arg(rows.size()).arg(more));
}

void MainWindow::stepHistoryMatch(const int direction) {
  const auto rows = historyModel_->matchingRows();
  if (rows.isEmpty()) return;
  const auto current = historyList_->currentIndex().row();
  int target = direction > 0 ? rows.first() : rows.last();
  if (direction > 0) {
    for (const auto row : rows) if (row > current) { target = row; break; }
  } else {
    for (auto it = rows.crbegin(); it != rows.crend(); ++it) if (*it < current) { target = *it; break; }
  }
  const auto index = historyModel_->index(target);
  historyList_->setCurrentIndex(index);
  historyList_->scrollTo(index, QAbstractItemView::PositionAtCenter);
  updateHistoryMatches();
}

void MainWindow::setHistoryFocus(const bool focused) {
  // Full-width history: no sidebar, no commit details, whole screen.
  if (auto* sidebar = findChild<QWidget*>(QStringLiteral("sidebar"))) sidebar->setVisible(!focused);
  if (historyDetails_) historyDetails_->setVisible(!focused);
  if (focused) {
    contentTabs_->setCurrentIndex(1);
    focusRestoreMaximized_ = isMaximized();
    showFullScreen();
  } else if (isFullScreen()) {
    if (focusRestoreMaximized_) showMaximized();
    else showNormal();
  }
  { const QSignalBlocker blocker(historyFocus_); historyFocus_->setChecked(focused); }
}

void MainWindow::reloadHistory() {
  if (!repository_) return;
  historyModel_->clear();
  clearCommitDetail();
  updateHistoryBranchActions();
  controller_->requestHistory(0, 50, {}, historyBranch_->currentData().toString());
}

void MainWindow::updateHistoryBranches() {
  const auto selected = historyBranch_->currentData().toString();
  const QSignalBlocker blocker(historyBranch_);
  historyBranch_->clear();
  historyBranch_->addItem(appState_.preferences.graphHistory ? tr("All branches (graph default)") : tr("Current branch"), QString{});
  historyBranch_->addItem(tr("All local and remote branches"), QStringLiteral("*"));
  if (repository_) {
    for (const auto& branch : repository_->branches)
      historyBranch_->addItem(tr("Local · %1").arg(branch), QStringLiteral("refs/heads/") + branch);
    for (const auto& branch : repository_->remoteBranches)
      historyBranch_->addItem(tr("Remote · %1").arg(branch), QStringLiteral("refs/remotes/") + branch);
  }
  const auto index = historyBranch_->findData(selected);
  historyBranch_->setCurrentIndex(index < 0 ? 0 : index);
  historyBranch_->setEnabled(repository_.has_value());
  updateHistoryBranchActions();
}

void MainWindow::updateHistoryBranchActions() {
  const auto ref = historyBranch_->currentData().toString();
  const bool ready = repository_ && busyOperations_.isEmpty();
  checkoutHistoryBranch_->setEnabled(ready && ref.startsWith(QStringLiteral("refs/")) && ref != QStringLiteral("refs/heads/") + repository_->branch);
  createHistoryBranch_->setEnabled(ready && ref != QStringLiteral("*"));
  fetchHistoryBranches_->setEnabled(ready && !repository_->remote.isEmpty());
}

void MainWindow::requestNextHistoryPage() {
  if (!repository_ || historyModel_->endOfHistory() || historyModel_->showingSearchResults() ||
      busyOperations_.contains(QStringLiteral("history"))) return;
  controller_->requestHistory(static_cast<int>(historyModel_->commits().size()), 50,
                              historyModel_->anchor(), historyBranch_->currentData().toString());
}

void MainWindow::updateStatus() {
  if (noticeTimer_ && noticeTimer_->isActive()) return;
  statusBar()->setProperty("error", false);
  statusBar()->style()->unpolish(statusBar());
  statusBar()->style()->polish(statusBar());
  if (!busyOperations_.isEmpty())
    statusBar()->showMessage(tr("Working: %1…").arg(busyOperations_.constBegin().key()));
  else if (repository_)
    statusBar()->showMessage(tr("%1 — %2").arg(repository_->name, repository_->branch));
  else
    statusBar()->showMessage(tr("No repository open"));
}

void MainWindow::showNotice(const QString& message, const bool error) {
  if (message.isEmpty()) return;
  statusBar()->setProperty("error", error);
  statusBar()->style()->unpolish(statusBar());
  statusBar()->style()->polish(statusBar());
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

void MainWindow::updateEditorAction() {
  if (!openEditorAction_) return;
  const auto& preferences = appState_.preferences;
  QString name;
  if (preferences.editorId == QStringLiteral("custom")) name = QFileInfo(preferences.editorPath).completeBaseName();
  for (const auto& editor : std::as_const(editors_))
    if (name.isEmpty() && (preferences.editorId.isEmpty() || editor.first == preferences.editorId)) name = editor.second;
  openEditorAction_->setText(name.isEmpty() ? tr("Open in editor") : tr("Open in %1").arg(name));
}

void MainWindow::showCompareDialog() {
  if (!repository_) return;
  if (!compareDialog_) {
    compareDialog_ = new CompareDialog(this);
    compareDialog_->setDiffFontSize(appState_.preferences.diffFontSize);
    connect(compareDialog_, &CompareDialog::comparisonRequested, controller_, &RelayController::compareBranches);
    connect(compareDialog_, &CompareDialog::fileDiffRequested, controller_, &RelayController::requestComparisonFileDiff);
    connect(compareDialog_, &CompareDialog::mergeRequested, this, [this](const QString& ref) {
      if (!repository_ || !busyOperations_.isEmpty()) return;
      const auto target = ref.startsWith(QStringLiteral("refs/heads/")) ? ref.mid(11) : ref;
      QMessageBox box(QMessageBox::Question, tr("Merge"), tr("Merge %1 into %2?").arg(ref.section(u'/', 2), repository_->branch),
                      QMessageBox::Ok | QMessageBox::Cancel, compareDialog_);
      box.setDefaultButton(QMessageBox::Cancel);
      if (box.exec() == QMessageBox::Ok) controller_->executeRepositoryAction(RepositoryAction::mergeBranch, target);
    });
    connect(controller_, &RelayController::comparisonReady, compareDialog_, [this](const QString& path, const BranchComparison& comparison) {
      if (repository_ && repository_->path == path) compareDialog_->showComparison(comparison);
    });
    connect(controller_, &RelayController::comparisonFileDiffReady, compareDialog_,
            [this](const QString& path, const QString& from, const QString& to, const QString& file, const QString& diff) {
      if (repository_ && repository_->path == path) compareDialog_->showFileDiff(from, to, file, diff);
    });
  }
  compareDialog_->setRepository(*repository_);
  // Open on the branch being browsed in History, if it is another branch.
  const auto browsed = historyBranch_->currentData().toString();
  if (!browsed.isEmpty() && browsed != QStringLiteral("refs/heads/") + repository_->branch)
    compareDialog_->selectBranches(QStringLiteral("refs/heads/") + repository_->branch, browsed);
  compareDialog_->show();
  compareDialog_->raise();
  compareDialog_->activateWindow();
}

void MainWindow::requestPullRequest() {
  if (!repository_) return;
  // Ahead counts are only meaningful once the branch exists on origin; an
  // unpublished branch gets the controller's "push first" message instead.
  if (!originTrackingTip(*repository_).isEmpty() && repository_->ahead > 0) {
    QMessageBox box(QMessageBox::Question, tr("Pull request"),
        tr("%1 has %n commit(s) that are not on origin yet.", nullptr, repository_->ahead).arg(repository_->branch),
        QMessageBox::Cancel, this);
    box.setObjectName(QStringLiteral("pullRequestUnpushedDialog"));
    box.setInformativeText(tr("The pull request will not include them until you push."));
    auto* push = box.addButton(tr("Push first"), QMessageBox::AcceptRole);
    auto* open = box.addButton(tr("Open without pushing"), QMessageBox::AcceptRole);
    box.setDefaultButton(push);
    box.exec();
    if (box.clickedButton() == push) {
      pullRequestAfterPush_ = repository_->path;
      controller_->pushOrigin(currentAccountId());
      return;
    }
    if (box.clickedButton() != open) return;
  }
  controller_->openPullRequest(currentAccountId());
}

void MainWindow::confirmDeleteOriginBranch() {
  if (!repository_ || !busyOperations_.isEmpty()) return;
  QStringList choices;
  for (const auto& branch : repository_->remoteBranches)
    if (branch.startsWith(QStringLiteral("origin/"))) choices.append(branch.mid(7));
  if (choices.isEmpty()) { showNotice(tr("There are no fetched branches on origin.")); return; }
  // Offer the remote branch being browsed in History first.
  const auto browsed = historyBranch_->currentData().toString();
  const auto preferred = browsed.startsWith(QStringLiteral("refs/remotes/origin/"))
      ? choices.indexOf(browsed.mid(20)) : -1;
  bool accepted = false;
  const auto branch = QInputDialog::getItem(this, tr("Delete branch on origin"), tr("Branch on origin"),
                                            choices, qMax(0, preferred), false, &accepted);
  if (!accepted || !repository_) return;
  const auto expected = originTrackingTip(*repository_, branch);
  if (expected.isEmpty()) return;
  QMessageBox box(QMessageBox::Warning, tr("Delete branch on origin"),
      tr("Delete %1 on origin?").arg(branch), QMessageBox::Cancel, this);
  box.setObjectName(QStringLiteral("deleteOriginBranchDialog"));
  box.setTextFormat(Qt::PlainText);
  box.setInformativeText(tr("The branch is removed from origin for everyone. Local branches, including your own %1, "
                            "are kept. To restore it later, push a branch at its last commit:\n%2\n\n"
                            "Relay refuses the deletion if origin/%1 has changed since your last fetch.")
                             .arg(branch, expected));
  auto* remove = box.addButton(tr("Delete on origin"), QMessageBox::DestructiveRole);
  remove->setObjectName(QStringLiteral("deleteOriginBranchButton"));
  box.setDefaultButton(QMessageBox::Cancel);
  box.exec();
  if (box.clickedButton() == remove && repository_)
    controller_->deleteOriginBranch(currentAccountId(), branch, expected);
}

bool MainWindow::canForcePush() const {
  return repository_ && busyOperations_.isEmpty() && !repository_->remote.isEmpty() &&
         !originTrackingTip(*repository_).isEmpty();
}

void MainWindow::confirmForcePush() {
  if (!canForcePush()) return;
  const auto branch = repository_->branch;
  const auto expected = originTrackingTip(*repository_);
  QMessageBox box(QMessageBox::Warning, tr("Force push origin"),
      tr("Replace origin/%1 with your local %1?").arg(branch), QMessageBox::Cancel, this);
  box.setObjectName(QStringLiteral("forcePushDialog"));
  box.setInformativeText(tr("Commits on origin/%1 that are not in your local branch will be removed from it, "
                            "and anyone who already has them must reconcile their work. Relay refuses the push "
                            "if origin/%1 is no longer at %2, the commit from your last fetch.")
                             .arg(branch, expected.left(7)));
  auto* force = box.addButton(tr("Force push"), QMessageBox::DestructiveRole);
  force->setObjectName(QStringLiteral("forcePushButton"));
  box.setDefaultButton(QMessageBox::Cancel);
  box.exec();
  if (box.clickedButton() == force && repository_ && repository_->branch == branch)
    controller_->pushOrigin(currentAccountId(), expected);
}

}  // namespace relay
