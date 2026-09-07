#include "relay/git_service.hpp"
#include "relay/process_runner.hpp"
#include "relay/relay_controller.hpp"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
using relay::RepositoryAction;
const relay::Account identity{.name = QStringLiteral("Relay Test"), .email = QStringLiteral("test@example.com")};
QString git(const QString& root, QStringList arguments) {
  arguments.prepend(root);
  arguments.prepend(QStringLiteral("-C"));
  relay::ProcessRequest request(QStringLiteral("git"), arguments);
  for (const auto& prefix : {QStringLiteral("GIT_AUTHOR_"), QStringLiteral("GIT_COMMITTER_")}) {
    request.environment.insert(prefix + QStringLiteral("NAME"), identity.name);
    request.environment.insert(prefix + QStringLiteral("EMAIL"), identity.email);
  }
  return QString::fromUtf8(relay::ProcessRunner::run(request).standardOutput).trimmed();
}
void write(const QString& root, const QString& name, const QByteArray& contents) {
  QFile file(root + u'/' + name);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QCOMPARE(file.write(contents), contents.size());
}
QByteArray read(const QString& root, const QString& name) {
  QFile file(root + u'/' + name);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return file.readAll();
}
void commit(const QString& root, const QString& message = QStringLiteral("change")) {
  git(root, {QStringLiteral("add"), QStringLiteral("--all")});
  git(root, {QStringLiteral("commit"), QStringLiteral("-m"), message});
}
void init(const QString& root) {
  git(root, {QStringLiteral("init"), QStringLiteral("-b"), QStringLiteral("main")});
  git(root, {QStringLiteral("config"), QStringLiteral("user.name"), identity.name});
  git(root, {QStringLiteral("config"), QStringLiteral("user.email"), identity.email});
  git(root, {QStringLiteral("config"), QStringLiteral("core.autocrlf"), QStringLiteral("false")});
  write(root, QStringLiteral("file.txt"), "base\n");
  commit(root, QStringLiteral("initial"));
}
void divergent(const QString& root) {
  init(root);
  git(root, {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("topic")});
  write(root, QStringLiteral("file.txt"), "topic\n");
  commit(root, QStringLiteral("topic"));
  git(root, {QStringLiteral("switch"), QStringLiteral("main")});
  write(root, QStringLiteral("file.txt"), "main\n");
  commit(root, QStringLiteral("main"));
}
}

class GitWorkflowsTest final : public QObject {
  Q_OBJECT
 private slots:
  void originUrlValidationProtectsCredentialsAndSeparatePushDestination() {
    QTemporaryDir dir; const auto root = dir.path(); init(root);
    relay::GitService service;
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.setOriginRemote(root, QStringLiteral("ssh://user:password@example.com/repo")));
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.setOriginRemote(root, QStringLiteral("SSH:///repo")));
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.setOriginRemote(root, QStringLiteral("https://example.com/repo?token=value")));
    service.setOriginRemote(root, QStringLiteral("https://example.com/repo"));
    git(root, {QStringLiteral("config"), QStringLiteral("remote.origin.pushurl"), QStringLiteral("https://example.net/old")});
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.setOriginRemote(root, QStringLiteral("https://example.com/new"), true));
    QCOMPARE(service.originRemoteUrl(root), QStringLiteral("https://example.com/repo"));
    QCOMPARE(service.originRemoteUrl(root, true), QStringLiteral("https://example.net/old"));
  }

  void createAndManageBranches() {
    QTemporaryDir dir;
    relay::GitService service;
    const auto root = dir.path() + QStringLiteral("/new repository");
    const auto created = service.createRepository(root);
    QVERIFY(!created.hasHead);
    QCOMPARE(created.branch, QStringLiteral("main"));
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.createRepository(root));
    write(root, QStringLiteral("file.txt"), "base\n");
    commit(root);
    service.createBranch(root, QStringLiteral("topic"));
    service.performAction(root, RepositoryAction::renameBranch, QStringLiteral("renamed"));
    QCOMPARE(service.readRepository(root).branch, QStringLiteral("renamed"));
    service.switchBranch(root, QStringLiteral("main"));
    service.performAction(root, RepositoryAction::deleteBranch, QStringLiteral("renamed"));
    QVERIFY(!service.readRepository(root).branches.contains(QStringLiteral("renamed")));
    service.createBranch(root, QStringLiteral("unmerged"));
    write(root, QStringLiteral("another.txt"), "unmerged\n"); commit(root);
    service.switchBranch(root, QStringLiteral("main"));
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.performAction(root, RepositoryAction::deleteBranch, QStringLiteral("unmerged")));
    service.performAction(root, RepositoryAction::mergeBranch, QStringLiteral("unmerged"), {}, identity);
    service.performAction(root, RepositoryAction::deleteBranch, QStringLiteral("unmerged"));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/another.txt")));
  }

  void stashAndRestoreTrackedAndUntrackedChanges() {
    QTemporaryDir dir; const auto root = dir.path(); init(root);
    relay::GitService service;
    write(root, QStringLiteral("file.txt"), "modified\n");
    write(root, QStringLiteral("untracked.txt"), "new\n");
    service.performAction(root, RepositoryAction::stash, QStringLiteral("unfinished work"));
    auto state = service.readRepository(root);
    QVERIFY(state.files.isEmpty());
    QCOMPARE(state.stashes.size(), 1);
    QVERIFY(state.stashes.first().description.contains(QStringLiteral("unfinished work")));
    const auto hash = state.stashes.first().hash;
    service.performAction(root, RepositoryAction::applyStash, hash);
    QCOMPARE(read(root, QStringLiteral("file.txt")), QByteArray("modified\n"));
    QCOMPARE(read(root, QStringLiteral("untracked.txt")), QByteArray("new\n"));
    QCOMPARE(service.readRepository(root).stashes.size(), 1);
    service.performAction(root, RepositoryAction::dropStash, hash);
    QVERIFY(service.readRepository(root).stashes.isEmpty());
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.performAction(root, RepositoryAction::dropStash, hash));
  }

  void stashRestoreRespectsConfiguredWindowsLineEndings() {
    QTemporaryDir dir; const auto root = dir.path(); init(root);
    git(root, {QStringLiteral("config"), QStringLiteral("core.autocrlf"), QStringLiteral("true")});
    write(root, QStringLiteral("file.txt"), "modified\r\n");
    relay::GitService service;
    service.performAction(root, RepositoryAction::stash);
    const auto state = service.readRepository(root);
    QCOMPARE(state.stashes.size(), 1);
    service.performAction(root, RepositoryAction::applyStash, state.stashes.first().hash);
    QCOMPARE(read(root, QStringLiteral("file.txt")), QByteArray("modified\r\n"));
  }

  void discardIsSelectiveAndRecoverable() {
    QTemporaryDir dir; const auto root = dir.path(); init(root);
    relay::GitService service;
    write(root, QStringLiteral("file.txt"), "modified\n");
    write(root, QStringLiteral("other.txt"), "keep\n");
    service.performAction(root, RepositoryAction::discardFiles, {}, {QStringLiteral("file.txt")});
    QCOMPARE(read(root, QStringLiteral("file.txt")), QByteArray("base\n"));
    QCOMPARE(read(root, QStringLiteral("other.txt")), QByteArray("keep\n"));
    const auto state = service.readRepository(root);
    QCOMPARE(state.stashes.size(), 1);
    QVERIFY(state.stashes.first().description.contains(QStringLiteral("discarding")));
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.performAction(root, RepositoryAction::discardFiles, {}, {QStringLiteral("../outside")}));
  }

  void discardRestoresStagedRenameAndDeletionWithoutTouchingOtherIndexEntries() {
    QTemporaryDir dir; const auto root = dir.path(); init(root);
    write(root, QStringLiteral("keep.txt"), "base\n"); commit(root);
    git(root, {QStringLiteral("mv"), QStringLiteral("file.txt"), QStringLiteral("renamed.txt")});
    write(root, QStringLiteral("keep.txt"), "staged\n");
    git(root, {QStringLiteral("add"), QStringLiteral("keep.txt")});
    write(root, QStringLiteral("keep.txt"), "unstaged\n");
    relay::GitService service;
    service.performAction(root, RepositoryAction::discardFiles, {}, {QStringLiteral("renamed.txt")});
    QCOMPARE(read(root, QStringLiteral("file.txt")), QByteArray("base\n"));
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/renamed.txt")));
    QCOMPARE(read(root, QStringLiteral("keep.txt")), QByteArray("unstaged\n"));
    QCOMPARE(git(root, {QStringLiteral("show"), QStringLiteral(":keep.txt")}), QStringLiteral("staged"));
    git(root, {QStringLiteral("rm"), QStringLiteral("file.txt")});
    service.performAction(root, RepositoryAction::discardFiles, {}, {QStringLiteral("file.txt")});
    QCOMPARE(read(root, QStringLiteral("file.txt")), QByteArray("base\n"));
    QCOMPARE(read(root, QStringLiteral("keep.txt")), QByteArray("unstaged\n"));
    QCOMPARE(git(root, {QStringLiteral("show"), QStringLiteral(":keep.txt")}), QStringLiteral("staged"));
  }

  void stashPreservesIndexOnlyChangesAndLiteralUntrackedNames() {
    QTemporaryDir dir; const auto root = dir.path(); init(root);
    write(root, QStringLiteral("file.txt"), "staged\n");
    git(root, {QStringLiteral("add"), QStringLiteral("file.txt")});
    write(root, QStringLiteral("file.txt"), "base\n");
#ifndef Q_OS_WIN
    const auto name = QStringLiteral(":(glob)*");
#else
    const auto name = QStringLiteral("untracked.txt");
#endif
    write(root, name, "untracked\n");
    relay::GitService service;
    service.performAction(root, RepositoryAction::stash);
    const auto state = service.readRepository(root);
    QVERIFY(state.files.isEmpty());
    service.performAction(root, RepositoryAction::applyStash, state.stashes.first().hash);
    QCOMPARE(read(root, QStringLiteral("file.txt")), QByteArray("base\n"));
    QCOMPARE(git(root, {QStringLiteral("show"), QStringLiteral(":file.txt")}), QStringLiteral("staged"));
    QCOMPARE(read(root, name), QByteArray("untracked\n"));
  }

  void stashRefusesTrackedFileReplacedByDirectoryWithIgnoredData() {
    QTemporaryDir dir; const auto root = dir.path(); init(root);
    write(root, QStringLiteral(".gitignore"), "*.secret\n");
    write(root, QStringLiteral("other.txt"), "base\n"); commit(root);
    QVERIFY(QFile::remove(root + QStringLiteral("/file.txt")));
    QVERIFY(QDir().mkdir(root + QStringLiteral("/file.txt")));
    write(root, QStringLiteral("file.txt/local.secret"), "preserve me\n");
    write(root, QStringLiteral("other.txt"), "changed\n");
    relay::GitService service;
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.performAction(root, RepositoryAction::stash));
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.performAction(root, RepositoryAction::discardFiles, {}, {QStringLiteral("other.txt")}));
    QCOMPARE(read(root, QStringLiteral("file.txt/local.secret")), QByteArray("preserve me\n"));
    QCOMPARE(read(root, QStringLiteral("other.txt")), QByteArray("changed\n"));
    QVERIFY(service.readRepository(root).stashes.isEmpty());
  }

  void graphWorksOnUnbornBranchWithExistingOtherBranches() {
    QTemporaryDir dir; const auto root = dir.path(); init(root);
    git(root, {QStringLiteral("switch"), QStringLiteral("--orphan"), QStringLiteral("empty")});
    relay::GitService service;
    QVERIFY(service.readHistoryPage(root).commits.isEmpty());
    QCOMPARE(service.readHistoryPage(root, 0, 50, {}, true).commits.size(), 1);
  }

  void graphHistoryIncludesOtherBranchesAndKeepsPagingSnapshot() {
    QTemporaryDir dir; const auto root = dir.path(); divergent(root);
    relay::GitService service;
    const auto normal = service.readHistoryPage(root);
    QCOMPARE(normal.commits.size(), 2);
    const auto graph = service.readHistoryPage(root, 0, 1, {}, true);
    QVERIFY(graph.anchor.contains(u'|'));
    service.createBranch(root, QStringLiteral("later"));
    write(root, QStringLiteral("late.txt"), "late\n"); commit(root);
    const auto rest = service.readHistoryPage(root, 1, 50, graph.anchor, true);
    QCOMPARE(rest.commits.size(), 2);
    QVERIFY(std::none_of(rest.commits.cbegin(), rest.commits.cend(), [](const auto& c) { return c.title == QStringLiteral("change"); }));
  }

  void conflictsCanBeResolvedAndContinued() {
    QTemporaryDir dir; const auto root = dir.path(); divergent(root);
    relay::GitService service;
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.performAction(root, RepositoryAction::mergeBranch, QStringLiteral("topic"), {}, identity));
    auto state = service.readRepository(root);
    QCOMPARE(state.pendingOperation, QStringLiteral("merge"));
    QCOMPARE(state.conflictedFiles, QStringList{QStringLiteral("file.txt")});
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.createBranch(root, QStringLiteral("wrong")));
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.performAction(root, RepositoryAction::continueOperation, {}, {}, identity));
    service.performAction(root, RepositoryAction::resolveTheirs, {}, {QStringLiteral("file.txt")});
    QCOMPARE(read(root, QStringLiteral("file.txt")), QByteArray("topic\n"));
    QVERIFY(service.readRepository(root).conflictedFiles.isEmpty());
    service.performAction(root, RepositoryAction::continueOperation, {}, {}, identity);
    QVERIFY(service.readRepository(root).pendingOperation.isEmpty());
    QCOMPARE(git(root, {QStringLiteral("show"), QStringLiteral("-s"), QStringLiteral("--format=%P")}).split(u' ').size(), 2);
  }

  void abortRestoresOriginalBranch() {
    QTemporaryDir dir; const auto root = dir.path(); divergent(root);
    relay::GitService service;
    const auto before = git(root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.performAction(root, RepositoryAction::mergeBranch, QStringLiteral("topic"), {}, identity));
    service.performAction(root, RepositoryAction::abortOperation);
    QCOMPARE(git(root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}), before);
    QCOMPARE(read(root, QStringLiteral("file.txt")), QByteArray("main\n"));
    QVERIFY(service.readRepository(root).files.isEmpty());
  }

  void revertAndUndoPreserveContentAndHistory() {
    QTemporaryDir dir; const auto root = dir.path(); init(root);
    relay::GitService service;
    write(root, QStringLiteral("file.txt"), "second\n"); commit(root, QStringLiteral("second"));
    auto hash = git(root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    service.performAction(root, RepositoryAction::revertCommit, hash, {}, identity);
    QCOMPARE(read(root, QStringLiteral("file.txt")), QByteArray("base\n"));
    QCOMPARE(git(root, {QStringLiteral("rev-list"), QStringLiteral("--count"), QStringLiteral("HEAD")}), QStringLiteral("3"));
    hash = git(root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    service.performAction(root, RepositoryAction::undoCommit, hash);
    QCOMPARE(read(root, QStringLiteral("file.txt")), QByteArray("base\n"));
    QCOMPARE(service.readRepository(root).files.size(), 1);
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.performAction(root, RepositoryAction::undoCommit, hash));
  }

  void amendMessageDoesNotCommitUnrelatedStagedFilesAndTagsAreLocal() {
    QTemporaryDir dir; const auto root = dir.path(); init(root);
    relay::GitService service;
    const auto hash = git(root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    write(root, QStringLiteral("unrelated.txt"), "keep staged\n");
    git(root, {QStringLiteral("add"), QStringLiteral("unrelated.txt")});
    service.performAction(root, RepositoryAction::amendMessage, QStringLiteral("Corrected title\n\nBody"), {hash}, identity);
    QCOMPARE(git(root, {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%s")}), QStringLiteral("Corrected title"));
    QVERIFY(!git(root, {QStringLiteral("ls-tree"), QStringLiteral("--name-only"), QStringLiteral("HEAD")}).contains(QStringLiteral("unrelated.txt")));
    QVERIFY(git(root, {QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--name-only")}).contains(QStringLiteral("unrelated.txt")));
    const auto amended = git(root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    service.performAction(root, RepositoryAction::createTag, QStringLiteral("v1.0.0"), {amended});
    QVERIFY(service.readRepository(root).tags.contains(QStringLiteral("v1.0.0")));
    service.performAction(root, RepositoryAction::deleteTag, QStringLiteral("v1.0.0"));
    QVERIFY(service.readRepository(root).tags.isEmpty());
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.performAction(root, RepositoryAction::amendMessage, QStringLiteral("wrong"), {hash}, identity));
  }

  void undoInitialCommitKeepsFilesAndCanCommitAgain() {
    QTemporaryDir dir; const auto root = dir.path(); init(root);
    relay::GitService service;
    service.performAction(root, RepositoryAction::undoCommit);
    auto state = service.readRepository(root);
    QVERIFY(!state.hasHead);
    QCOMPARE(read(root, QStringLiteral("file.txt")), QByteArray("base\n"));
    QCOMPARE(state.files.size(), 1);
    service.commitFiles(root, {QStringLiteral("file.txt")}, QStringLiteral("initial again"), {}, identity);
    QVERIFY(service.readRepository(root).hasHead);
  }

  void rebaseConflictsCanContinueAndPublishedCommitsAreProtected() {
    QTemporaryDir dir; const auto root = dir.path(); divergent(root);
    relay::GitService service;
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.performAction(root, RepositoryAction::rebaseBranch, QStringLiteral("topic"), {}, identity));
    QCOMPARE(service.readRepository(root).pendingOperation, QStringLiteral("rebase"));
    service.performAction(root, RepositoryAction::resolveTheirs, {}, {QStringLiteral("file.txt")});
    service.performAction(root, RepositoryAction::continueOperation, {}, {}, identity);
    QVERIFY(service.readRepository(root).pendingOperation.isEmpty());
    QCOMPARE(service.readRepository(root).branch, QStringLiteral("main"));
    QCOMPARE(read(root, QStringLiteral("file.txt")), QByteArray("main\n"));
    git(root, {QStringLiteral("update-ref"), QStringLiteral("refs/remotes/origin/main"), QStringLiteral("HEAD")});
    const auto head = git(root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.performAction(root, RepositoryAction::rebaseBranch, QStringLiteral("topic"), {}, identity));
    QCOMPARE(git(root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}), head);
  }

  void recreatedMergeConflictUsesTheRebaseSequencer() {
    QTemporaryDir dir; const auto root = dir.path(); divergent(root);
    relay::GitService service;
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, git(root, {QStringLiteral("merge"), QStringLiteral("topic")}));
    write(root, QStringLiteral("file.txt"), "resolved\n"); commit(root, QStringLiteral("resolved merge"));
    const auto original = git(root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    git(root, {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("newbase"), QStringLiteral("HEAD~2")});
    write(root, QStringLiteral("base.txt"), "new base\n"); commit(root);
    git(root, {QStringLiteral("switch"), QStringLiteral("main")});
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.performAction(root, RepositoryAction::rebaseBranch, QStringLiteral("newbase"), {}, identity));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/.git/MERGE_HEAD")));
    QCOMPARE(service.readRepository(root).pendingOperation, QStringLiteral("rebase"));
    service.performAction(root, RepositoryAction::abortOperation);
    QCOMPARE(git(root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}), original);
    QCOMPARE(service.readRepository(root).branch, QStringLiteral("main"));
    QVERIFY(service.readRepository(root).pendingOperation.isEmpty());
  }

  void refusesUndoOfKnownPublishedCommit() {
    QTemporaryDir dir; const auto root = dir.path(); init(root);
    relay::GitService service;
    write(root, QStringLiteral("file.txt"), "second\n"); commit(root);
    git(root, {QStringLiteral("update-ref"), QStringLiteral("refs/remotes/origin/main"), QStringLiteral("HEAD")});
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.performAction(root, RepositoryAction::undoCommit));
  }

  void remoteCheckoutAndCherryPick() {
    QTemporaryDir dir; const auto root = dir.path(); init(root);
    relay::GitService service;
    service.createBranch(root, QStringLiteral("topic"));
    write(root, QStringLiteral("topic.txt"), "topic\n"); commit(root);
    const auto hash = git(root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    service.switchBranch(root, QStringLiteral("main"));
    service.performAction(root, RepositoryAction::cherryPick, hash, {}, identity);
    QCOMPARE(read(root, QStringLiteral("topic.txt")), QByteArray("topic\n"));
    git(root, {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), root});
    git(root, {QStringLiteral("update-ref"), QStringLiteral("refs/remotes/origin/remote-topic"), hash});
    QVERIFY(service.readRepository(root).remoteBranches.contains(QStringLiteral("origin/remote-topic")));
    service.performAction(root, RepositoryAction::checkoutRemote, QStringLiteral("origin/remote-topic"));
    QCOMPARE(service.readRepository(root).branch, QStringLiteral("remote-topic"));
    QVERIFY(service.readRepository(root).hasUpstream);
  }

  void controllerPublishesConflictAfterGitFailure() {
    QTemporaryDir dir; const auto root = dir.path(); divergent(root);
    relay::RelayControllerConfig config;
    config.storeFile = dir.path() + QStringLiteral("/../relay-workflow-state.json");
    QTemporaryDir store;
    config.storeFile = store.path() + QStringLiteral("/state.json");
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    QSignalSpy failed(&controller, &relay::RelayController::operationFailed);
    controller.start();
    controller.openRepository(root);
    QTRY_VERIFY(controller.currentRepository());
    controller.executeRepositoryAction(RepositoryAction::mergeBranch, QStringLiteral("topic"));
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(controller.currentRepository()->pendingOperation, QStringLiteral("merge"));
    QCOMPARE(controller.currentRepository()->conflictedFiles.size(), 1);
    controller.executeRepositoryAction(RepositoryAction::abortOperation);
    QTRY_VERIFY(controller.currentRepository()->pendingOperation.isEmpty());
  }
};
QTEST_GUILESS_MAIN(GitWorkflowsTest)
#include "tst_git_workflows.moc"
