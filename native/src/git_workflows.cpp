#include "relay/git_service.hpp"
#include "relay/process_runner.hpp"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>
#include <QUrl>

namespace relay {
namespace {
QProcessEnvironment identityEnvironment(const Account& account) {
  QProcessEnvironment environment;
  environment.insert(QStringLiteral("GIT_EDITOR"), QStringLiteral("true"));
  environment.insert(QStringLiteral("GIT_MERGE_AUTOEDIT"), QStringLiteral("no"));
  if (!account.name.isEmpty() && !account.email.isEmpty()) {
    environment.insert(QStringLiteral("GIT_AUTHOR_NAME"), account.name);
    environment.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), account.email);
    environment.insert(QStringLiteral("GIT_COMMITTER_NAME"), account.name);
    environment.insert(QStringLiteral("GIT_COMMITTER_EMAIL"), account.email);
  }
  return environment;
}
}

void GitService::readOperationState(Repository& repository) const {
  const auto& root = repository.path;
  repository.commitName = runGitOrEmpty(root, {QStringLiteral("config"), QStringLiteral("user.name")});
  repository.commitEmail = runGitOrEmpty(root, {QStringLiteral("config"), QStringLiteral("user.email")});
  repository.hasHead = !runGitOrEmpty(root, {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                                            QStringLiteral("HEAD")}).isEmpty();
  repository.historyRefState = runGit(root, {QStringLiteral("for-each-ref"), QStringLiteral("--format=%(refname) %(objectname)"),
      QStringLiteral("refs/heads/"), QStringLiteral("refs/remotes/"), QStringLiteral("refs/tags/")});
  repository.tags = runGit(root, {QStringLiteral("tag"), QStringLiteral("--list")}).split(u'\n', Qt::SkipEmptyParts);
  const auto branches = runGit(root, {QStringLiteral("for-each-ref"),
      QStringLiteral("--format=%(refname:short)%09%(symref)"), QStringLiteral("refs/remotes/")});
  for (const auto& line : branches.split(u'\n', Qt::SkipEmptyParts)) {
    const auto fields = line.split(u'\t');
    if (fields.size() < 2 || fields.at(1).isEmpty()) repository.remoteBranches.append(fields.first());
  }
  const auto stashes = runGit(root, {QStringLiteral("stash"), QStringLiteral("list"),
      QStringLiteral("-z"), QStringLiteral("--format=%H%x00%gs")}, {}, true).split(QChar{0});
  for (qsizetype index = 0; index + 1 < stashes.size(); index += 2)
    repository.stashes.append({stashes.at(index), stashes.at(index + 1)});
  // Recreated merges during --rebase-merges also have MERGE_HEAD. The
  // enclosing sequencer takes precedence for continue/abort/skip.
  for (const auto& pair : {qMakePair(QStringLiteral("rebase-merge"), QStringLiteral("rebase")),
                           qMakePair(QStringLiteral("rebase-apply"), QStringLiteral("rebase")),
                           qMakePair(QStringLiteral("MERGE_HEAD"), QStringLiteral("merge")),
                           qMakePair(QStringLiteral("REVERT_HEAD"), QStringLiteral("revert")),
                           qMakePair(QStringLiteral("CHERRY_PICK_HEAD"), QStringLiteral("cherry-pick"))}) {
    const auto path = runGit(root, {QStringLiteral("rev-parse"), QStringLiteral("--git-path"), pair.first});
    if (QFileInfo::exists(QDir(root).absoluteFilePath(path))) {
      repository.pendingOperation = pair.second;
      break;
    }
  }
  repository.conflictedFiles = runGit(root, {QStringLiteral("diff"), QStringLiteral("--name-only"),
      QStringLiteral("--diff-filter=U"), QStringLiteral("-z")}, {}, true).split(QChar{0}, Qt::SkipEmptyParts);
}

void GitService::requireIdle(const QString& repositoryPath, const bool clean) const {
  Repository state;
  state.path = repositoryPath;
  readOperationState(state);
  if (!state.pendingOperation.isEmpty() || !state.conflictedFiles.isEmpty())
    throw ProcessError(QStringLiteral("Finish or abort the current conflict resolution before starting another operation."));
  if (clean && !runGit(repositoryPath, {QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
      QStringLiteral("-z"), QStringLiteral("--untracked-files=all")}).isEmpty())
    throw ProcessError(QStringLiteral("Commit or stash your changes before this operation."));
}

Repository GitService::createRepository(const QString& destinationPath) const {
  const auto path = QFileInfo(destinationPath).absoluteFilePath();
  if (QFileInfo::exists(path))
    throw ProcessError(QStringLiteral("Choose a new folder; the destination already exists."));
  const QFileInfo info(path);
  if (!info.dir().exists() || info.fileName().isEmpty())
    throw ProcessError(QStringLiteral("Choose an existing parent folder and a repository name."));
  static_cast<void>(runGitWithoutRepository({QStringLiteral("init"), QStringLiteral("--initial-branch=main"),
      QStringLiteral("--"), path}, {}, info.absolutePath()));
  return readRepository(path);
}

void GitService::setOriginRemote(const QString& root, const QString& remote, const bool replace) const {
  const QUrl url(remote);
  if (remote.isEmpty() || remote.contains(u'\n') || remote.contains(u'\r') || remote.startsWith(u'-') ||
      !url.password().isEmpty() || !url.query().isEmpty() || !url.fragment().isEmpty() ||
      (url.scheme() == QStringLiteral("ssh") && (!url.isValid() || url.host().isEmpty())) ||
      (!isSshRemote(remote) && (url.scheme() != QStringLiteral("https") || url.host().isEmpty() || !url.userInfo().isEmpty())))
    throw ProcessError(QStringLiteral("Enter an HTTPS URL without embedded credentials, or an SSH URL."));
  const auto existing = originRemoteUrl(root);
  const auto explicitPush = runGitOrEmpty(root, {QStringLiteral("config"), QStringLiteral("--get-all"), QStringLiteral("remote.origin.pushurl")});
  if (replace && !explicitPush.isEmpty())
    throw ProcessError(QStringLiteral("Origin has an explicit push URL. Update or remove remote.origin.pushurl in Git before changing the origin URL here."));
  if (!existing.isEmpty() && !replace) throw ProcessError(QStringLiteral("This repository already has an origin remote."));
  static_cast<void>(runGit(root, {QStringLiteral("remote"), existing.isEmpty() ? QStringLiteral("add") : QStringLiteral("set-url"),
      QStringLiteral("origin"), remote}));
}

void GitService::performAction(const QString& root, const RepositoryAction action,
                               const QString& target, const QStringList& paths,
                               const Account& account) const {
  const auto execute = [this, &root](QStringList arguments, const QProcessEnvironment& environment = QProcessEnvironment{}) {
    return runGit(root, arguments, environment);
  };
  const auto localRef = [&execute](const QString& branch) {
    if (branch.isEmpty() || branch.startsWith(u'-')) throw ProcessError(QStringLiteral("Choose a valid local branch."));
    const auto ref = QStringLiteral("refs/heads/") + branch;
    execute({QStringLiteral("show-ref"), QStringLiteral("--verify"), QStringLiteral("--quiet"), ref});
    return ref;
  };
  const auto environment = identityEnvironment(account);
  switch (action) {
    case RepositoryAction::createTag:
    case RepositoryAction::deleteTag: {
      requireIdle(root);
      if (target.isEmpty() || target.startsWith(u'-')) throw ProcessError(QStringLiteral("Choose a valid tag name."));
      execute({QStringLiteral("check-ref-format"), QStringLiteral("refs/tags/") + target});
      if (action == RepositoryAction::deleteTag) execute({QStringLiteral("tag"), QStringLiteral("--delete"), QStringLiteral("--"), target});
      else {
        const auto hash = assertCommitInRepository(root, paths.value(0));
        execute({QStringLiteral("tag"), target, hash});
      }
      break;
    }
    case RepositoryAction::amendMessage: {
      requireIdle(root);
      if (target.trimmed().isEmpty()) throw ProcessError(QStringLiteral("Enter a commit message."));
      const auto expected = assertCommitInRepository(root, paths.value(0));
      const auto actual = execute({QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
      if (expected != actual) throw ProcessError(QStringLiteral("Only the latest commit can be amended. Refresh and select it again."));
      if (!execute({QStringLiteral("for-each-ref"), QStringLiteral("--format=%(refname)"),
          QStringLiteral("--contains=") + actual, QStringLiteral("refs/remotes/")}).isEmpty())
        throw ProcessError(QStringLiteral("This commit is already on a remote branch and cannot be amended here."));
      execute({QStringLiteral("commit"), QStringLiteral("--amend"), QStringLiteral("--only"),
          QStringLiteral("--message"), target}, environment);
      break;
    }
    case RepositoryAction::setOrigin:
      setOriginRemote(root, target, true);
      break;
    case RepositoryAction::renameBranch:
      requireIdle(root);
      if (target.isEmpty() || target.startsWith(u'-')) throw ProcessError(QStringLiteral("Enter a valid branch name."));
      execute({QStringLiteral("check-ref-format"), QStringLiteral("refs/heads/") + target});
      execute({QStringLiteral("branch"), QStringLiteral("--move"), target});
      break;
    case RepositoryAction::deleteBranch: {
      requireIdle(root);
      const auto ref = localRef(target);
      // Git's -d can accept a branch merged only into its upstream. Require
      // containment in the current branch as well before removing its name.
      execute({QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"), ref, QStringLiteral("HEAD")});
      execute({QStringLiteral("branch"), QStringLiteral("--delete"), QStringLiteral("--"), target});
      break;
    }
    case RepositoryAction::checkoutRemote: {
      requireIdle(root);
      Repository state;
      state.path = root;
      readOperationState(state);
      if (!state.remoteBranches.contains(target)) throw ProcessError(QStringLiteral("Choose an existing remote branch."));
      const auto name = target.mid(target.indexOf(u'/') + 1);
      execute({QStringLiteral("switch"), QStringLiteral("--create"), name, QStringLiteral("--track"),
               QStringLiteral("refs/remotes/") + target});
      break;
    }
    case RepositoryAction::mergeBranch:
    case RepositoryAction::rebaseBranch: {
      requireIdle(root, true);
      QString ref;
      if (target.startsWith(QStringLiteral("refs/remotes/"))) {
        execute({QStringLiteral("show-ref"), QStringLiteral("--verify"), QStringLiteral("--quiet"), target});
        ref = target;
      } else ref = localRef(target);
      if (action == RepositoryAction::mergeBranch) {
        execute({QStringLiteral("merge"), QStringLiteral("--no-edit"), ref}, environment);
      } else {
        const auto replayed = execute({QStringLiteral("rev-list"), QStringLiteral("--count"), QStringLiteral("HEAD"), QStringLiteral("--not"), ref});
        const auto unpublished = execute({QStringLiteral("rev-list"), QStringLiteral("--count"), QStringLiteral("HEAD"),
            QStringLiteral("--not"), ref, QStringLiteral("--remotes")});
        if (replayed != unpublished)
          throw ProcessError(QStringLiteral("This would rewrite a commit already on a remote branch. Merge the branch instead."));
        execute({QStringLiteral("rebase"), QStringLiteral("--rebase-merges"), ref}, environment);
      }
      break;
    }
    case RepositoryAction::abortOperation:
    case RepositoryAction::skipOperation:
    case RepositoryAction::continueOperation: {
      Repository state;
      state.path = root;
      readOperationState(state);
      if (state.pendingOperation.isEmpty()) throw ProcessError(QStringLiteral("There is no operation to continue or abort."));
      if (action == RepositoryAction::continueOperation && !state.conflictedFiles.isEmpty())
        throw ProcessError(QStringLiteral("Resolve and mark every conflicted file before continuing."));
      if (action == RepositoryAction::skipOperation && state.pendingOperation == QStringLiteral("merge"))
        throw ProcessError(QStringLiteral("A merge cannot skip a commit. Continue or abort it."));
      execute({state.pendingOperation, action == RepositoryAction::abortOperation
          ? QStringLiteral("--abort") : (action == RepositoryAction::skipOperation ? QStringLiteral("--skip") : QStringLiteral("--continue"))}, environment);
      break;
    }
    case RepositoryAction::resolveOurs:
    case RepositoryAction::resolveTheirs:
    case RepositoryAction::markResolved: {
      Repository state;
      state.path = root;
      readOperationState(state);
      if (paths.isEmpty()) throw ProcessError(QStringLiteral("Choose a conflicted file."));
      for (const auto& path : paths) {
        if (!state.conflictedFiles.contains(path)) throw ProcessError(QStringLiteral("The selected file is no longer conflicted."));
      }
      for (const auto& path : paths) {
        if (action != RepositoryAction::markResolved) {
          const auto stage = action == RepositoryAction::resolveOurs ? QStringLiteral("2") : QStringLiteral("3");
          const auto entry = execute({QStringLiteral("ls-files"), QStringLiteral("--stage"), QStringLiteral("--"), path});
          const QRegularExpression present(QStringLiteral(" %1\\t").arg(stage));
          if (!entry.contains(present)) {
            execute({QStringLiteral("rm"), QStringLiteral("--force"), QStringLiteral("--"), path});
            continue;
          }
          execute({QStringLiteral("checkout"), action == RepositoryAction::resolveOurs
              ? QStringLiteral("--ours") : QStringLiteral("--theirs"), QStringLiteral("--"), path});
        }
        execute({QStringLiteral("add"), QStringLiteral("--all"), QStringLiteral("--"), path});
      }
      break;
    }
    case RepositoryAction::stash:
    case RepositoryAction::discardFiles: {
      requireIdle(root);
      if (action == RepositoryAction::discardFiles && paths.isEmpty()) throw ProcessError(QStringLiteral("Select changes to discard."));
      const auto selected = action == RepositoryAction::discardFiles ? expandedChangedPaths(root, paths) : QStringList{};
      // A tracked file replaced by a directory may contain ignored data that
      // stash -u cannot save but its reset step can delete. Refuse before writing.
      const auto indexEntries = execute({QStringLiteral("ls-files"), QStringLiteral("--stage"), QStringLiteral("-z")});
      const auto headEntries = runGitOrEmpty(root, {QStringLiteral("ls-tree"), QStringLiteral("-r"), QStringLiteral("-z"), QStringLiteral("HEAD")});
      for (const auto& entry : (indexEntries + headEntries).split(QChar{0}, Qt::SkipEmptyParts)) {
        if (entry.startsWith(QStringLiteral("160000 "))) continue;
        const auto tab = entry.indexOf(u'\t');
        if (tab < 0) continue;
        const QFileInfo info(root + u'/' + entry.mid(tab + 1));
        if (info.isDir() && !info.isSymLink())
          throw ProcessError(QStringLiteral("A tracked file was replaced by a directory. Move that directory to a safe location before stashing or discarding; it may contain ignored files that Git cannot preserve."));
      }
      const auto before = runGitOrEmpty(root, {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("refs/stash")});
      // Do not propagate --literal-pathspecs into stash: its internal clean
      // generates magic pathspecs. No user-controlled path is passed here.
      static_cast<void>(runGit(root, {QStringLiteral("stash"), QStringLiteral("push"), QStringLiteral("--include-untracked"),
          QStringLiteral("--message"), action == RepositoryAction::discardFiles
              ? QStringLiteral("Relay: recovery snapshot before discarding selected changes (%1)").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
              : (target.isEmpty() ? QStringLiteral("Relay: work in progress") : target)}, environment, false, false));
      if (action == RepositoryAction::discardFiles) {
        const auto backup = execute({QStringLiteral("rev-parse"), QStringLiteral("refs/stash")});
        if (backup == before) throw ProcessError(QStringLiteral("No recovery snapshot was created; changes were not discarded."));
        // Restore the exact original index and working tree before changing
        // selected paths. The retained stash also contains untracked files.
        static_cast<void>(runGit(root, {QStringLiteral("stash"), QStringLiteral("apply"), QStringLiteral("--index"), backup}, {}, false, false));
        QStringList indexArgs{QStringLiteral("ls-files"), QStringLiteral("-z"), QStringLiteral("--")};
        indexArgs.append(selected);
        auto tracked = execute(indexArgs).split(QChar{0}, Qt::SkipEmptyParts);
        QStringList headArgs{QStringLiteral("ls-tree"), QStringLiteral("-r"), QStringLiteral("--name-only"),
                            QStringLiteral("-z"), QStringLiteral("HEAD"), QStringLiteral("--")};
        headArgs.append(selected);
        tracked.append(execute(headArgs).split(QChar{0}, Qt::SkipEmptyParts));
        tracked.removeDuplicates();
        if (!tracked.isEmpty()) {
          QStringList restore{QStringLiteral("restore"), QStringLiteral("--source=HEAD"), QStringLiteral("--staged"),
                              QStringLiteral("--worktree"), QStringLiteral("--")};
          restore.append(tracked);
          execute(restore);
        }
        QStringList clean{QStringLiteral("clean"), QStringLiteral("--force"), QStringLiteral("--")};
        clean.append(selected);
        execute(clean);
      }
      break;
    }
    case RepositoryAction::applyStash:
    case RepositoryAction::dropStash: {
      requireIdle(root, action == RepositoryAction::applyStash);
      Repository state;
      state.path = root;
      readOperationState(state);
      qsizetype index = -1;
      for (qsizetype i = 0; i < state.stashes.size(); ++i) if (state.stashes.at(i).hash == target) { index = i; break; }
      if (index < 0) throw ProcessError(QStringLiteral("That stash no longer exists. Refresh and choose it again."));
      // Apply keeps the stash as a recovery copy, even after a successful apply.
      QStringList arguments{QStringLiteral("stash"), action == RepositoryAction::applyStash
          ? QStringLiteral("apply") : QStringLiteral("drop")};
      if (action == RepositoryAction::applyStash) arguments.append(QStringLiteral("--index"));
      arguments.append(QStringLiteral("stash@{%1}").arg(index));
      static_cast<void>(runGit(root, arguments, {}, false, false));
      break;
    }
    case RepositoryAction::revertCommit:
    case RepositoryAction::cherryPick: {
      requireIdle(root, true);
      const auto hash = assertCommitInRepository(root, target);
      const auto detail = readCommitDetail(root, hash);
      QStringList arguments{action == RepositoryAction::revertCommit ? QStringLiteral("revert") : QStringLiteral("cherry-pick"),
                            QStringLiteral("--no-edit")};
      if (detail.isMerge) arguments.append({QStringLiteral("--mainline"), QStringLiteral("1")});
      arguments.append(hash);
      execute(arguments, environment);
      break;
    }
    case RepositoryAction::undoCommit: {
      requireIdle(root, true);
      const auto hash = execute({QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
      if (!target.isEmpty() && hash != target) throw ProcessError(QStringLiteral("The latest commit changed. Refresh before undoing it."));
      if (!execute({QStringLiteral("for-each-ref"), QStringLiteral("--format=%(refname)"),
          QStringLiteral("--contains=") + hash, QStringLiteral("refs/remotes/")}).isEmpty())
        throw ProcessError(QStringLiteral("This commit is already on a remote branch. Revert it instead."));
      const auto parents = execute({QStringLiteral("show"), QStringLiteral("--no-patch"), QStringLiteral("--format=%P"), hash}).split(u' ', Qt::SkipEmptyParts);
      if (parents.size() > 1) throw ProcessError(QStringLiteral("Undo is not available for a merge commit. Use Revert for this commit."));
      if (execute({QStringLiteral("branch"), QStringLiteral("--show-current")}).isEmpty())
        throw ProcessError(QStringLiteral("Switch to a branch before undoing a commit."));
      if (parents.isEmpty()) execute({QStringLiteral("update-ref"), QStringLiteral("-d"), QStringLiteral("HEAD"), hash});
      else execute({QStringLiteral("reset"), QStringLiteral("--mixed"), parents.first()});
      break;
    }
  }
}
}  // namespace relay
