#include "relay/main_window.hpp"
#include "relay/relay_controller.hpp"
#include "relay/list_models.hpp"

#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace relay {
namespace {
bool confirm(QWidget* parent, const QString& title, const QString& text) {
  QMessageBox dialog(QMessageBox::Question, title, text, QMessageBox::Ok | QMessageBox::Cancel, parent);
  dialog.setTextFormat(Qt::PlainText);
  dialog.setDefaultButton(QMessageBox::Cancel);
  return dialog.exec() == QMessageBox::Ok;
}
}

void MainWindow::buildWorkflowMenus(QMenu* file, QMenu* repositoryMenu) {
  auto* create = new QAction(tr("New Repository…"), this);
  create->setObjectName(QStringLiteral("createRepositoryAction"));
  file->insertAction(openAction_, create);
  connect(create, &QAction::triggered, this, [this] {
    const auto parent = QFileDialog::getExistingDirectory(this, tr("Choose a parent folder"));
    if (parent.isEmpty()) return;
    bool accepted = false;
    const auto name = QInputDialog::getText(this, tr("New Repository"), tr("Repository name"), QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    if (name == QStringLiteral(".") || name == QStringLiteral("..") || name.contains(u'/') || name.contains(u'\\') || name.contains(u':')) {
      showNotice(tr("Enter a folder name without path separators."), true);
      return;
    }
    controller_->createLocalRepository(QDir(parent).filePath(name));
  });
  const auto add = [this, repositoryMenu](const QString& text, const QString& name,
                                         const QString& rule, auto callback) {
    auto* action = repositoryMenu->addAction(text, this, callback);
    action->setObjectName(name);
    action->setProperty("workflowRule", rule);
    action->setEnabled(false);
    workflowActions_.append(action);
    return action;
  };
  const auto branchOperation = [this](RepositoryAction action, const QString& title, bool remote = false) {
    if (!repository_ || !busyOperations_.isEmpty()) return;
    auto choices = remote ? repository_->remoteBranches : repository_->branches;
    if (!remote) choices.removeAll(repository_->branch);
    if (action == RepositoryAction::mergeBranch || action == RepositoryAction::rebaseBranch)
      for (const auto& branch : repository_->remoteBranches) choices.append(QStringLiteral("[remote] ") + branch);
    if (choices.isEmpty()) { showNotice(tr("There are no other branches to choose from.")); return; }
    bool accepted = false;
    auto target = QInputDialog::getItem(this, title, tr("Branch"), choices, 0, false, &accepted);
    if (!accepted) return;
    if (target.startsWith(QStringLiteral("[remote] "))) target = QStringLiteral("refs/remotes/") + target.mid(9);
    if (action == RepositoryAction::deleteBranch && !confirm(this, title,
        tr("Delete local branch %1? Relay only deletes branches already merged into the current branch.").arg(target))) return;
    if (action == RepositoryAction::rebaseBranch && !confirm(this, title,
        tr("Replay the current branch’s unpublished commits on this branch? Commits known to remote branches will not be rewritten."))) return;
    controller_->executeRepositoryAction(action, target);
  };
  add(tr("Publish repository…"), QStringLiteral("publishRepositoryAction"), QStringLiteral("publish"), [this] { showPublishDialog(); });
  add(tr("Origin remote URL…"), QStringLiteral("originRemoteAction"), QStringLiteral("idle"), [this] {
    if (!repository_) return;
    bool accepted = false;
    const auto remote = QInputDialog::getText(this, tr("Origin remote"), tr("HTTPS or SSH URL"),
        QLineEdit::Normal, repository_->remote, &accepted).trimmed();
    if (accepted && !remote.isEmpty() && (repository_->remote.isEmpty() || confirm(this, tr("Change origin"), tr("Use this URL for future fetches and pushes?"))))
      controller_->executeRepositoryAction(RepositoryAction::setOrigin, remote);
  });
  add(tr("Rename current branch…"), QStringLiteral("renameBranchAction"), QStringLiteral("idle"), [this] {
    if (!repository_) return;
    bool accepted = false;
    const auto name = QInputDialog::getText(this, tr("Rename branch"), tr("New name"),
        QLineEdit::Normal, repository_->branch, &accepted).trimmed();
    if (accepted && !name.isEmpty()) controller_->executeRepositoryAction(RepositoryAction::renameBranch, name);
  });
  add(tr("Delete merged branch…"), QStringLiteral("deleteBranchAction"), QStringLiteral("head"),
      [branchOperation] { branchOperation(RepositoryAction::deleteBranch, tr("Delete branch")); });
  add(tr("Check out remote branch…"), QStringLiteral("checkoutRemoteAction"), QStringLiteral("idle"),
      [branchOperation] { branchOperation(RepositoryAction::checkoutRemote, tr("Check out remote branch"), true); });
  add(tr("Merge into current branch…"), QStringLiteral("mergeBranchAction"), QStringLiteral("head"),
      [branchOperation] { branchOperation(RepositoryAction::mergeBranch, tr("Merge into current branch")); });
  add(tr("Rebase unpublished commits…"), QStringLiteral("rebaseBranchAction"), QStringLiteral("head"),
      [branchOperation] { branchOperation(RepositoryAction::rebaseBranch, tr("Rebase current branch")); });
  repositoryMenu->addSeparator();
  add(tr("Stash all changes…"), QStringLiteral("stashAction"), QStringLiteral("changes"), [this] {
    bool accepted = false;
    const auto message = QInputDialog::getText(this, tr("Stash changes"),
        tr("Description (tracked and untracked changes will be saved)"), QLineEdit::Normal, {}, &accepted);
    if (accepted) controller_->executeRepositoryAction(RepositoryAction::stash, message);
  });
  add(tr("Saved stashes…"), QStringLiteral("stashesAction"), QStringLiteral("stashes"), [this] { showStashes(); });
  add(tr("Discard selected changes…"), QStringLiteral("discardAction"), QStringLiteral("changes"), [this] {
    const auto paths = changedFileModel_->checkedPaths();
    if (paths.isEmpty()) { showNotice(tr("Select changed files first.")); return; }
    if (confirm(this, tr("Discard selected changes"),
        tr("Discard changes in %1 selected file(s)? A full recovery snapshot, including other current changes, will remain under Repository → Saved stashes.").arg(paths.size())))
      controller_->executeRepositoryAction(RepositoryAction::discardFiles, {}, paths);
  });
  repositoryMenu->addSeparator();
  add(tr("Undo latest unpushed commit…"), QStringLiteral("undoCommitAction"), QStringLiteral("head"), [this] {
    if (!repository_ || repository_->history.isEmpty()) return;
    const auto hash = repository_->history.first().fullHash;
    if (confirm(this, tr("Undo latest commit"), tr("Move the latest commit back into Changes? File contents will be preserved. Commits known to remote branches cannot be undone.")))
      controller_->executeRepositoryAction(RepositoryAction::undoCommit, hash);
  });
  add(tr("Edit latest commit message…"), QStringLiteral("amendMessageAction"), QStringLiteral("latest"), [this] {
    if (!commitDetail_) return;
    const auto hash = currentCommitHash();
    bool accepted = false;
    const auto message = QInputDialog::getMultiLineText(this, tr("Edit latest commit message"),
        tr("Message (the commit must not be published)"), commitDetail_->title + QStringLiteral("\n\n") + commitDetail_->body, &accepted);
    if (accepted && !message.trimmed().isEmpty()) controller_->executeRepositoryAction(RepositoryAction::amendMessage, message, {hash});
  });
  add(tr("Create tag on selected commit…"), QStringLiteral("createTagAction"), QStringLiteral("commit"), [this] {
    if (!commitDetail_) return;
    const auto hash = currentCommitHash();
    bool accepted = false;
    const auto name = QInputDialog::getText(this, tr("Create local tag"), tr("Tag name"), QLineEdit::Normal, {}, &accepted).trimmed();
    if (accepted && !name.isEmpty()) controller_->executeRepositoryAction(RepositoryAction::createTag, name, {hash});
  });
  add(tr("Delete local tag…"), QStringLiteral("deleteTagAction"), QStringLiteral("tags"), [this] {
    if (!repository_ || repository_->tags.isEmpty()) return;
    bool accepted = false;
    const auto tag = QInputDialog::getItem(this, tr("Delete local tag"), tr("Tag"), repository_->tags, 0, false, &accepted);
    if (accepted && confirm(this, tr("Delete local tag"), tr("Delete tag %1 from this local repository? The remote tag will remain unchanged.").arg(tag)))
      controller_->executeRepositoryAction(RepositoryAction::deleteTag, tag);
  });
  add(tr("Revert selected commit…"), QStringLiteral("revertCommitAction"), QStringLiteral("commit"), [this] {
    if (!commitDetail_) return;
    const auto hash = currentCommitHash();
    if (confirm(this, tr("Revert commit"), tr("Create a new commit that reverses “%1”? Merge commits are reversed against their first parent.").arg(commitDetail_->title)))
      controller_->executeRepositoryAction(RepositoryAction::revertCommit, hash);
  });
  add(tr("Cherry-pick selected commit…"), QStringLiteral("cherryPickAction"), QStringLiteral("commit"), [this] {
    if (!commitDetail_) return;
    const auto hash = currentCommitHash();
    if (confirm(this, tr("Cherry-pick commit"), tr("Apply “%1” to the current branch? For merge commits, changes are taken against the first parent.").arg(commitDetail_->title)))
      controller_->executeRepositoryAction(RepositoryAction::cherryPick, hash);
  });
  add(tr("Resolve conflicts / continue…"), QStringLiteral("conflictsAction"), QStringLiteral("conflict"), [this] { showConflicts(); });
  connect(repositoryMenu, &QMenu::aboutToShow, this, &MainWindow::updateWorkflowActions);
}

void MainWindow::updateWorkflowActions() {
  const bool available = repository_ && busyOperations_.isEmpty();
  const bool pending = repository_ && (!repository_->pendingOperation.isEmpty() || !repository_->conflictedFiles.isEmpty());
  for (auto* action : workflowActions_) {
    const auto rule = action->property("workflowRule").toString();
    bool enabled = available && !pending;
    if (rule == QStringLiteral("conflict")) enabled = available && pending;
    else if (rule == QStringLiteral("publish")) enabled = enabled && repository_->remote.isEmpty() && repository_->hasHead && !currentAccountId().isEmpty();
    else if (rule == QStringLiteral("head")) enabled = enabled && repository_->hasHead;
    else if (rule == QStringLiteral("changes")) enabled = enabled && repository_->hasHead && !repository_->files.isEmpty();
    else if (rule == QStringLiteral("stashes")) enabled = enabled && !repository_->stashes.isEmpty();
    else if (rule == QStringLiteral("commit")) enabled = enabled && commitDetail_.has_value();
    else if (rule == QStringLiteral("latest")) enabled = enabled && commitDetail_ && !repository_->history.isEmpty() && commitDetail_->fullHash == repository_->history.first().fullHash;
    else if (rule == QStringLiteral("tags")) enabled = enabled && !repository_->tags.isEmpty();
    action->setEnabled(enabled);
  }
  if (conflictButton_) {
    conflictButton_->setVisible(pending);
    conflictButton_->setEnabled(available);
    if (pending) conflictButton_->setText(repository_->conflictedFiles.isEmpty()
        ? tr("%1 is ready to continue — review and finish").arg(repository_->pendingOperation)
        : tr("%1 conflicted file(s) — resolve conflicts").arg(repository_->conflictedFiles.size()));
  }
}

void MainWindow::showPublishDialog() {
  if (!repository_ || !repository_->remote.isEmpty()) return;
  if (currentAccountId().isEmpty() || !repository_->hasHead) {
    showNotice(tr("Select a GitHub account and create an initial commit before publishing."), true);
    return;
  }
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Publish repository to GitHub"));
  auto* layout = new QVBoxLayout(&dialog);
  const auto identity = controller_->commitIdentity();
  const auto publishingPath = repository_->path;
  auto* explanation = new QLabel(tr("Create a GitHub repository under @%1 and push the current branch and its history.").arg(identity.handle), &dialog);
  explanation->setTextFormat(Qt::PlainText);
  explanation->setWordWrap(true);
  layout->addWidget(explanation);
  auto* form = new QFormLayout;
  auto* name = new QLineEdit(repository_->name, &dialog);
  auto* description = new QLineEdit(&dialog);
  description->setMaxLength(350);
  auto* privateRepository = new QCheckBox(tr("Keep this repository private"), &dialog);
  privateRepository->setChecked(true);
  form->addRow(tr("Name"), name);
  form->addRow(tr("Description"), description);
  form->addRow(privateRepository);
  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
  auto* publish = buttons->addButton(tr("Publish repository"), QDialogButtonBox::AcceptRole);
  publish->setDefault(false);
  buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  if (dialog.exec() == QDialog::Accepted && repository_ && repository_->path == publishingPath)
    controller_->publishRepository(name->text().trimmed(), description->text().trimmed(), privateRepository->isChecked(), identity.id);
}

void MainWindow::showStashes() {
  if (!repository_ || repository_->stashes.isEmpty()) return;
  const auto entries = repository_->stashes;
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Saved stashes"));
  dialog.resize(620, 200);
  auto* layout = new QVBoxLayout(&dialog);
  auto* explanation = new QLabel(tr("Apply restores a stash and keeps its backup. Delete permanently removes the selected backup."), &dialog);
  explanation->setWordWrap(true);
  layout->addWidget(explanation);
  auto* picker = new QComboBox(&dialog);
  picker->setAccessibleName(tr("Saved stash"));
  for (const auto& stash : entries) picker->addItem(stash.description, stash.hash);
  layout->addWidget(picker);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  auto* apply = buttons->addButton(tr("Apply stash"), QDialogButtonBox::ActionRole);
  auto* drop = buttons->addButton(tr("Delete stash…"), QDialogButtonBox::ActionRole);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(apply, &QPushButton::clicked, &dialog, [this, &dialog, picker] {
    controller_->executeRepositoryAction(RepositoryAction::applyStash, picker->currentData().toString());
    dialog.accept();
  });
  connect(drop, &QPushButton::clicked, &dialog, [this, &dialog, picker] {
    if (!confirm(&dialog, tr("Delete stash"), tr("Permanently delete this stash? This removes its recovery copy."))) return;
    controller_->executeRepositoryAction(RepositoryAction::dropStash, picker->currentData().toString());
    dialog.accept();
  });
  dialog.exec();
}

void MainWindow::showConflicts() {
  if (!repository_) return;
  const auto path = repository_->path;
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("conflictsDialog"));
  dialog.setWindowTitle(tr("Resolve conflicts"));
  dialog.resize(640, 300);
  auto* layout = new QVBoxLayout(&dialog);
  auto* explanation = new QLabel(tr("Edit conflicting files in your editor, save them, then mark them resolved. You can also keep an entire Git side. During a rebase, Ours is the base branch and Theirs is the replayed commit."), &dialog);
  explanation->setWordWrap(true);
  layout->addWidget(explanation);
  auto* picker = new QComboBox(&dialog);
  picker->setAccessibleName(tr("Conflicted file"));
  layout->addWidget(picker);
  auto* ours = new QPushButton(tr("Keep ours"), &dialog);
  auto* theirs = new QPushButton(tr("Keep theirs"), &dialog);
  auto* resolved = new QPushButton(tr("Mark resolved after editing"), &dialog);
  layout->addWidget(ours);
  layout->addWidget(theirs);
  layout->addWidget(resolved);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  auto* finish = buttons->addButton(tr("Continue operation"), QDialogButtonBox::ActionRole);
  auto* abort = buttons->addButton(tr("Abort operation…"), QDialogButtonBox::ActionRole);
  auto* skip = buttons->addButton(tr("Skip commit…"), QDialogButtonBox::ActionRole);
  layout->addWidget(buttons);
  const auto update = [picker, ours, theirs, resolved, finish, abort, skip](const Repository& repository, bool busy) {
    const auto previous = picker->currentText();
    picker->clear();
    picker->addItems(repository.conflictedFiles);
    if (repository.conflictedFiles.contains(previous)) picker->setCurrentText(previous);
    ours->setEnabled(!busy && !repository.conflictedFiles.isEmpty());
    theirs->setEnabled(ours->isEnabled());
    resolved->setEnabled(ours->isEnabled());
    finish->setEnabled(!busy && repository.conflictedFiles.isEmpty() && !repository.pendingOperation.isEmpty());
    abort->setEnabled(!busy && !repository.pendingOperation.isEmpty());
    skip->setEnabled(!busy && !repository.pendingOperation.isEmpty() && repository.pendingOperation != QStringLiteral("merge"));
  };
  auto* error = new QLabel(&dialog);
  error->setObjectName(QStringLiteral("conflictError"));
  error->setTextFormat(Qt::PlainText);
  error->setWordWrap(true);
  error->setStyleSheet(QStringLiteral("color: #a54e43"));
  layout->addWidget(error);
  connect(controller_, &RelayController::operationFailed, &dialog, [error](const QString& operation, const QString& message) {
    if (operation == QStringLiteral("repository-action")) error->setText(message);
  });
  connect(controller_, &RelayController::busyChanged, &dialog, [error](const QString& operation, bool busy) {
    if (operation == QStringLiteral("repository-action") && busy) error->clear();
  });
  update(*repository_, !busyOperations_.isEmpty());
  connect(controller_, &RelayController::currentRepositoryChanged, &dialog,
      [&dialog, path, update](const Repository& repository) {
        if (repository.path != path || (repository.conflictedFiles.isEmpty() && repository.pendingOperation.isEmpty())) { dialog.accept(); return; }
        update(repository, true);
      });
  connect(controller_, &RelayController::busyChanged, &dialog, [this, update](const QString&, bool) {
    if (repository_) update(*repository_, !busyOperations_.isEmpty());
  });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  const auto resolve = [this, &dialog, picker](RepositoryAction action) {
    const auto file = picker->currentText();
    if (file.isEmpty()) return;
    if (action != RepositoryAction::markResolved && !confirm(&dialog, tr("Resolve file"),
        tr("Replace the whole conflicted file with this Git side? For a deleted side, the file will be removed."))) return;
    controller_->executeRepositoryAction(action, {}, {file});
  };
  connect(ours, &QPushButton::clicked, &dialog, [resolve] { resolve(RepositoryAction::resolveOurs); });
  connect(theirs, &QPushButton::clicked, &dialog, [resolve] { resolve(RepositoryAction::resolveTheirs); });
  connect(resolved, &QPushButton::clicked, &dialog, [resolve] { resolve(RepositoryAction::markResolved); });
  connect(finish, &QPushButton::clicked, &dialog, [this] { controller_->executeRepositoryAction(RepositoryAction::continueOperation); });
  connect(controller_, &RelayController::repositoryClosed, &dialog, &QDialog::reject);
  connect(skip, &QPushButton::clicked, &dialog, [this, &dialog] {
    if (confirm(&dialog, tr("Skip commit"), tr("Skip this commit and discard its current conflict resolutions?")))
      controller_->executeRepositoryAction(RepositoryAction::skipOperation);
  });
  connect(abort, &QPushButton::clicked, &dialog, [this, &dialog] {
    if (confirm(&dialog, tr("Abort operation"), tr("Abort this operation and discard its conflict resolutions?")))
      controller_->executeRepositoryAction(RepositoryAction::abortOperation);
  });
  dialog.exec();
}
}  // namespace relay
