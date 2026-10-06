#include "relay/git_service.hpp"
#include "relay/process_runner.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

namespace {

void writeFile(const QString& path, const QByteArray& contents) {
  QFile file(path);
  QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
           qPrintable(QStringLiteral("Could not write %1: %2").arg(path, file.errorString())));
  QCOMPARE(file.write(contents), contents.size());
}

QProcessEnvironment gitIdentity() {
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("GIT_AUTHOR_NAME"), QStringLiteral("Ada Lovelace"));
  environment.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), QStringLiteral("ada@example.com"));
  environment.insert(QStringLiteral("GIT_COMMITTER_NAME"), QStringLiteral("Grace Hopper"));
  environment.insert(QStringLiteral("GIT_COMMITTER_EMAIL"), QStringLiteral("grace@example.com"));
  return environment;
}

QString runGit(const QString& workingDirectory, QStringList arguments,
               const QProcessEnvironment& environment = gitIdentity()) {
  arguments.prepend(workingDirectory);
  arguments.prepend(QStringLiteral("-C"));
  relay::ProcessRequest request{QStringLiteral("git"), arguments};
  request.environment = environment;
  return QString::fromUtf8(relay::ProcessRunner::run(request).standardOutput).trimmed();
}

void initRepository(const QString& root) {
  runGit(root, {QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"),
                QStringLiteral("main"), QStringLiteral(".")});
}

void commitAll(const QString& root, const QString& message) {
  runGit(root, {QStringLiteral("add"), QStringLiteral(".")});
  runGit(root, {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), message});
}

void populateHistoryFixture(const QString& root) {
  initRepository(root);
  writeFile(QDir(root).filePath(QStringLiteral("root.txt")), QByteArrayLiteral("root\n"));
  runGit(root, {QStringLiteral("add"), QStringLiteral(".")});
  runGit(root, {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"),
                QStringLiteral("Root commit"), QStringLiteral("-m"),
                QStringLiteral("Body of the root commit.")});

  writeFile(QDir(root).filePath(QStringLiteral("gone.txt")), QByteArrayLiteral("temporary\n"));
  commitAll(root, QStringLiteral("Add a file that will be removed"));
  runGit(root, {QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("-b"),
                QStringLiteral("side")});
  writeFile(QDir(root).filePath(QStringLiteral("side.txt")), QByteArrayLiteral("side\n"));
  commitAll(root, QStringLiteral("Side branch work"));
  runGit(root, {QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("main")});
  writeFile(QDir(root).filePath(QStringLiteral("main.txt")), QByteArrayLiteral("main\n"));
  commitAll(root, QStringLiteral("Main only change"));
  runGit(root, {QStringLiteral("merge"), QStringLiteral("-q"), QStringLiteral("--no-ff"),
                QStringLiteral("side"), QStringLiteral("-m"),
                QStringLiteral("Merge side into main")});
  runGit(root, {QStringLiteral("tag"), QStringLiteral("v1.0.0")});
  runGit(root, {QStringLiteral("rm"), QStringLiteral("-q"), QStringLiteral("gone.txt")});
  runGit(root, {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"),
                QStringLiteral("Remove the temporary file")});
}

const relay::HistoryCommit& commitNamed(const QList<relay::HistoryCommit>& commits,
                                        const QString& title) {
  const auto iterator = std::find_if(commits.cbegin(), commits.cend(),
                                     [&title](const auto& commit) {
                                       return commit.title == title;
                                     });
  if (iterator == commits.cend()) qFatal("Expected history commit was not found");
  return *iterator;
}

const relay::ChangedFile& fileNamed(const QList<relay::ChangedFile>& files,
                                    const QString& path) {
  const auto iterator =
      std::find_if(files.cbegin(), files.cend(), [&path](const auto& file) {
        return file.path == path;
      });
  if (iterator == files.cend()) qFatal("Expected changed file was not found");
  return *iterator;
}

}  // namespace

class GitServiceTest final : public QObject {
  Q_OBJECT

 private slots:
  void previewsImageBeforeAndAfterWithoutTextConversion() {
    QTemporaryDir root;
    initRepository(root.path());
    QImage before(8, 8, QImage::Format_ARGB32);
    before.fill(Qt::red);
    QVERIFY(before.save(root.path() + QStringLiteral("/image.png")));
    commitAll(root.path(), QStringLiteral("red"));
    const auto hash = runGit(root.path(), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    QImage after(10, 6, QImage::Format_ARGB32);
    after.fill(Qt::blue);
    QVERIFY(after.save(root.path() + QStringLiteral("/image.png")));
    relay::GitService service;
    const auto preview = service.readFilePreview(root.path(), QStringLiteral("image.png"));
    QCOMPARE(preview.before.size(), QSize(8, 8));
    QCOMPARE(preview.after.size(), QSize(10, 6));
    QCOMPARE(preview.before.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(preview.after.pixelColor(0, 0), QColor(Qt::blue));
    const auto historical = service.readFilePreview(root.path(), QStringLiteral("image.png"), hash);
    QVERIFY(historical.before.isNull());
    QCOMPARE(historical.after.pixelColor(0, 0), QColor(Qt::red));
    writeFile(root.path() + QStringLiteral("/image.png"), QByteArrayLiteral("not an image\n"));
    const auto unavailable = service.readFilePreview(root.path(), QStringLiteral("image.png"));
    QVERIFY(unavailable.before.isNull());
    QVERIFY(unavailable.after.isNull());
    QVERIFY(unavailable.diff.startsWith(QStringLiteral("Image preview unavailable")));
  }

  void untrackedSymlinkNeverPreviewsTargetContents() {
#ifndef Q_OS_WIN
    QTemporaryDir root;
    QTemporaryDir outside;
    initRepository(root.path());
    writeFile(outside.path() + QStringLiteral("/private.txt"), QByteArrayLiteral("PRIVATE TARGET CONTENT\n"));
    QVERIFY(QFile::link(outside.path() + QStringLiteral("/private.txt"), root.path() + QStringLiteral("/link")));
    relay::GitService service;
    const auto state = service.readRepository(root.path());
    QCOMPARE(state.files.size(), 1);
    const auto preview = service.getFileDiff(root.path(), QStringLiteral("link"));
    QVERIFY(preview.startsWith(QStringLiteral("Symbolic link")));
    QVERIFY(!preview.contains(QStringLiteral("PRIVATE TARGET CONTENT")));
#endif
  }

  void previewsPreserveTrailingSpacesAndDoNotInventAnExtraLine() {
    QTemporaryDir root;
    initRepository(root.path());
    const auto file = root.filePath(QStringLiteral("text.txt"));
    writeFile(file, QByteArrayLiteral("before\n"));
    commitAll(root.path(), QStringLiteral("Initial"));
    writeFile(file, QByteArrayLiteral("after   \n"));
    relay::GitService service;
    QVERIFY(service.getFileDiff(root.path(), QStringLiteral("text.txt")).endsWith(QStringLiteral("+after   \n")));
    writeFile(root.filePath(QStringLiteral("new.txt")), QByteArrayLiteral("one\n"));
    const auto diff = service.getFileDiff(root.path(), QStringLiteral("new.txt"));
    QVERIFY(diff.contains(QStringLiteral("@@ -0,0 +1,1 @@")));
    QVERIFY(!diff.endsWith(QStringLiteral("\n+")));
  }

  void resolvesThePushUrlIndependentlyOfTheFetchUrl() {
    QTemporaryDir root;
    initRepository(root.path());
    runGit(root.path(), {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"),
                        QStringLiteral("https://example.test/team/repo.git")});
    runGit(root.path(), {QStringLiteral("remote"), QStringLiteral("set-url"), QStringLiteral("--push"),
                        QStringLiteral("origin"), QStringLiteral("https://github.com/team/repo.git")});
    relay::GitService service;
    QCOMPARE(service.originRemoteUrl(root.path()), QStringLiteral("https://example.test/team/repo.git"));
    QCOMPARE(service.originRemoteUrl(root.path(), true), QStringLiteral("https://github.com/team/repo.git"));
    runGit(root.path(), {QStringLiteral("remote"), QStringLiteral("set-url"), QStringLiteral("--add"),
                        QStringLiteral("--push"), QStringLiteral("origin"), QStringLiteral("https://example.test/other.git")});
    QVERIFY_EXCEPTION_THROWN(service.originRemoteUrl(root.path(), true), relay::ProcessError);
  }

  void credentialHelperScopesSecretsToGithubAndTreatsHandlesAsData() {
    QTemporaryDir root;
    const auto ask = [&](const QByteArray& input) {
      QProcess process;
      auto environment = QProcessEnvironment::systemEnvironment();
      environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
      environment.insert(QStringLiteral("GIT_CONFIG_NOSYSTEM"), QStringLiteral("1"));
      environment.insert(QStringLiteral("GIT_CONFIG_GLOBAL"), root.filePath(QStringLiteral("no-config")));
      environment.insert(QStringLiteral("GIT_ASKPASS"), QString{});
      environment.insert(QStringLiteral("RELAY_GIT_TOKEN"), QStringLiteral("test-token"));
      environment.insert(QStringLiteral("RELAY_GIT_USERNAME"), QStringLiteral("$(echo injected)"));
      process.setProcessEnvironment(environment);
      process.start(QStringLiteral("git"), {QStringLiteral("-c"), QStringLiteral("credential.helper="),
          QStringLiteral("-c"), QStringLiteral("credential.helper=") + relay::GitService::githubCredentialHelper(),
          QStringLiteral("credential"), QStringLiteral("fill")});
      if (!process.waitForStarted(5000)) return QByteArray{};
      process.write(input);
      process.closeWriteChannel();
      if (!process.waitForFinished(5000)) { process.kill(); process.waitForFinished(); return QByteArray{}; }
      return process.readAllStandardOutput();
    };
    const auto github = ask(QByteArrayLiteral("protocol=https\nhost=github.com\n\n"));
    QVERIFY(github.contains("password=test-token"));
    QVERIFY(github.contains("username=$(echo injected)"));
    QVERIFY(!ask(QByteArrayLiteral("protocol=https\nhost=example.test\n\n")).contains("test-token"));
    QVERIFY(!ask(QByteArrayLiteral("protocol=http\nhost=github.com\n\n")).contains("test-token"));
    // Git passes the host in the URL's own case.
    QVERIFY(ask(QByteArrayLiteral("protocol=https\nhost=GitHub.com\n\n")).contains("password=test-token"));
    QVERIFY(!ask(QByteArrayLiteral("protocol=https\nhost=github.com.example.test\n\n")).contains("test-token"));
    QVERIFY(!ask(QByteArrayLiteral("protocol=https\nhost=notgithub.com\n\n")).contains("test-token"));
  }

  void fetchesAllOriginBranchesFromSingleBranchClone() {
    QTemporaryDir root;
    const auto seed = root.filePath(QStringLiteral("seed"));
    const auto clone = root.filePath(QStringLiteral("clone"));
    QVERIFY(QDir().mkpath(seed));
    initRepository(seed);
    writeFile(seed + QStringLiteral("/a.txt"), QByteArrayLiteral("main\n"));
    commitAll(seed, QStringLiteral("Initial"));
    runGit(seed, {QStringLiteral("branch"), QStringLiteral("remote-only")});
    runGit(root.path(), {QStringLiteral("clone"), QStringLiteral("--single-branch"), QStringLiteral("--branch"), QStringLiteral("main"), seed, clone});
    const auto refspec = runGit(clone, {QStringLiteral("config"), QStringLiteral("--get-all"), QStringLiteral("remote.origin.fetch")});
    relay::GitService service;
    QVERIFY(!service.readRepository(clone).remoteBranches.contains(QStringLiteral("origin/remote-only")));
    service.fetchOrigin(clone, {}, {}, {}, true);
    QVERIFY(service.readRepository(clone).remoteBranches.contains(QStringLiteral("origin/remote-only")));
    QCOMPARE(runGit(clone, {QStringLiteral("config"), QStringLiteral("--get-all"), QStringLiteral("remote.origin.fetch")}), refspec);
    QCOMPARE(service.readRepository(clone).branch, QStringLiteral("main"));
    runGit(seed, {QStringLiteral("branch"), QStringLiteral("-D"), QStringLiteral("remote-only")});
    runGit(clone, {QStringLiteral("config"), QStringLiteral("fetch.prune"), QStringLiteral("true")});
    service.fetchOrigin(clone, {}, {}, {}, true);
    QVERIFY(service.readRepository(clone).remoteBranches.contains(QStringLiteral("origin/remote-only")));
  }

  void browsesRemoteHistoryAndCreatesFromItWithoutChangingHead() {
    QTemporaryDir root;
    initRepository(root.path());
    writeFile(root.filePath(QStringLiteral("a.txt")), QByteArrayLiteral("main\n"));
    commitAll(root.path(), QStringLiteral("Initial"));
    const auto original = runGit(root.path(), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    runGit(root.path(), {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("remote-only")});
    writeFile(root.filePath(QStringLiteral("a.txt")), QByteArrayLiteral("remote\n"));
    commitAll(root.path(), QStringLiteral("Remote work"));
    const auto remote = runGit(root.path(), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    runGit(root.path(), {QStringLiteral("update-ref"), QStringLiteral("refs/remotes/gitea/remote-only"), remote});
    runGit(root.path(), {QStringLiteral("switch"), QStringLiteral("main")});
    runGit(root.path(), {QStringLiteral("branch"), QStringLiteral("-D"), QStringLiteral("remote-only")});
    relay::GitService service;
    const auto repository = service.readRepository(root.path());
    QVERIFY(repository.remoteBranches.contains(QStringLiteral("gitea/remote-only")));
    const auto page = service.readHistoryPage(root.path(), 0, 1, {}, false, QStringLiteral("refs/remotes/gitea/remote-only"));
    QCOMPARE(page.commits.first().fullHash, remote);
    const auto next = service.readHistoryPage(root.path(), 1, 1, page.anchor, false, QStringLiteral("refs/remotes/gitea/remote-only"));
    QCOMPARE(next.commits.first().fullHash, original);
    QCOMPARE(runGit(root.path(), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}), original);
    QCOMPARE(service.readHistoryPage(root.path()).commits.size(), 1);
    QCOMPARE(service.readHistoryPage(root.path(), 0, 50, {}, true).commits.size(), 2);
    QVERIFY_EXCEPTION_THROWN(service.readHistoryPage(root.path(), 0, 50, {}, false, QStringLiteral("--all")), relay::ProcessError);
    service.createBranch(root.path(), QStringLiteral("review"), QStringLiteral("refs/remotes/gitea/remote-only"));
    QCOMPARE(runGit(root.path(), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}), remote);
    QCOMPARE(service.readRepository(root.path()).branch, QStringLiteral("review"));
  }

  void createsBranchesAndRejectsOptionLikeNames() {
    QTemporaryDir root;
    initRepository(root.path());
    writeFile(root.filePath(QStringLiteral("a.txt")), QByteArrayLiteral("a\n"));
    commitAll(root.path(), QStringLiteral("Initial"));
    relay::GitService service;
    service.createBranch(root.path(), QStringLiteral("feature/test"));
    QCOMPARE(service.readRepository(root.path()).branch, QStringLiteral("feature/test"));
    service.switchBranch(root.path(), QStringLiteral("main"));
    QVERIFY_EXCEPTION_THROWN(service.createBranch(root.path(), QStringLiteral("--force")), relay::ProcessError);
    QVERIFY_EXCEPTION_THROWN(service.switchBranch(root.path(), QStringLiteral("--detach")), relay::ProcessError);
    QCOMPARE(service.readRepository(root.path()).branch, QStringLiteral("main"));
  }

  void pullsFastForwardAndPreservesDivergentCommits() {
    QTemporaryDir root;
    const auto upstream = root.filePath(QStringLiteral("upstream"));
    const auto local = root.filePath(QStringLiteral("local"));
    QVERIFY(QDir().mkpath(upstream));
    initRepository(upstream);
    writeFile(QDir(upstream).filePath(QStringLiteral("first.txt")), QByteArrayLiteral("first\n"));
    commitAll(upstream, QStringLiteral("Initial"));
    relay::GitService service;
    static_cast<void>(service.cloneRepository(upstream, local));
    writeFile(QDir(upstream).filePath(QStringLiteral("second.txt")), QByteArrayLiteral("second\n"));
    commitAll(upstream, QStringLiteral("Second"));
    QVERIFY(service.pullOrigin(local).isEmpty());
    QCOMPARE(runGit(local, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}),
             runGit(upstream, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}));
    writeFile(QDir(local).filePath(QStringLiteral("local.txt")), QByteArrayLiteral("local\n"));
    commitAll(local, QStringLiteral("Local"));
    const auto head = runGit(local, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    writeFile(QDir(upstream).filePath(QStringLiteral("remote.txt")), QByteArrayLiteral("remote\n"));
    commitAll(upstream, QStringLiteral("Remote"));
    QCOMPARE(service.pullOrigin(local), QStringLiteral("refs/remotes/origin/main"));
    QCOMPARE(runGit(local, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}), head);
    QVERIFY(QFileInfo::exists(QDir(local).filePath(QStringLiteral("local.txt"))));
    // Up to date with local commits ahead: nothing to pull, nothing diverged.
    runGit(local, {QStringLiteral("reset"), QStringLiteral("--hard"), QStringLiteral("origin/main")});
    writeFile(QDir(local).filePath(QStringLiteral("ahead.txt")), QByteArrayLiteral("ahead\n"));
    commitAll(local, QStringLiteral("Ahead"));
    const auto ahead = runGit(local, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    QVERIFY(service.pullOrigin(local).isEmpty());
    QCOMPARE(runGit(local, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}), ahead);
  }

  void resolvesBundledRuntimeAndEnvironment() {
    QTemporaryDir resources;
    QTemporaryDir source;
    QVERIFY(resources.isValid());
    QVERIFY(source.isValid());
#ifdef Q_OS_WIN
    const auto relativeExecutable = QStringLiteral("git/cmd/git.exe");
    const auto expectedExecPath = QStringLiteral("git/mingw64/libexec/git-core");
#else
    const auto relativeExecutable = QStringLiteral("git/bin/git");
    const auto expectedExecPath = QStringLiteral("git/libexec/git-core");
#endif
    const auto executable = QDir(resources.path()).filePath(relativeExecutable);
    QVERIFY(QDir().mkpath(QFileInfo(executable).absolutePath()));
    writeFile(executable, QByteArrayLiteral("fixture"));

    const relay::GitService service(resources.path(), source.path());
    QCOMPARE(service.gitExecutable(), executable);
    const auto environment = service.gitProcessEnvironment();
    QCOMPARE(environment.value(QStringLiteral("GIT_EXEC_PATH")),
             QDir(resources.path()).filePath(expectedExecPath));
    QCOMPARE(environment.value(QStringLiteral("GIT_CONFIG_SYSTEM")),
             QDir(resources.path()).filePath(QStringLiteral("git/etc/gitconfig")));
    QVERIFY(environment.value(QStringLiteral("PATH")).startsWith(
        QDir(resources.path()).filePath(QStringLiteral("git"))));
  }

  void parsesStatusMatrixAndUntrackedCounts() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    writeFile(QDir(root.path()).filePath(QStringLiteral("plain.txt")),
              QByteArrayLiteral("one\ntwo\n"));
    writeFile(QDir(root.path()).filePath(QStringLiteral("binary.dat")),
              QByteArray("a\0b", 3));

    const auto files = relay::GitService::parseStatus(
        QStringLiteral("R  renamed.txt\0old.txt\0 D deleted.txt\0?? plain.txt\0?? binary.dat\0"),
        QStringLiteral("3\t2\trenamed.txt\0" "0\t4\tdeleted.txt\0"), root.path());
    QCOMPARE(files.size(), 4);

    const auto& renamed = fileNamed(files, QStringLiteral("renamed.txt"));
    QVERIFY(renamed.status == relay::FileStatus::modified);
    QCOMPARE(renamed.added, 3);
    QCOMPARE(renamed.removed, 2);

    const auto& deleted = fileNamed(files, QStringLiteral("deleted.txt"));
    QVERIFY(deleted.status == relay::FileStatus::deleted);
    QCOMPARE(deleted.removed, 4);

    const auto& plain = fileNamed(files, QStringLiteral("plain.txt"));
    QVERIFY(plain.status == relay::FileStatus::added);
    // A terminating newline is not an additional line of content.
    QCOMPARE(plain.added, 2);
    QCOMPARE(fileNamed(files, QStringLiteral("binary.dat")).added, 0);
  }

  void readsDiffsAndCommitsOnlySelectedFiles() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    initRepository(root.path());
    writeFile(QDir(root.path()).filePath(QStringLiteral("selected.txt")),
              QByteArrayLiteral("selected v1\n"));
    writeFile(QDir(root.path()).filePath(QStringLiteral("other.txt")),
              QByteArrayLiteral("other v1\n"));
    commitAll(root.path(), QStringLiteral("Initial"));

    writeFile(QDir(root.path()).filePath(QStringLiteral("selected.txt")),
              QByteArrayLiteral("selected v1\nselected v2\n"));
    writeFile(QDir(root.path()).filePath(QStringLiteral("other.txt")),
              QByteArrayLiteral("other v1\nother v2\n"));
    writeFile(QDir(root.path()).filePath(QStringLiteral("new.txt")),
              QByteArrayLiteral("new\n"));
    writeFile(QDir(root.path()).filePath(QStringLiteral("binary.dat")),
              QByteArray("x\0y", 3));

    const relay::GitService service;
    const auto repository = service.readRepository(root.path());
    QCOMPARE(repository.branch, QStringLiteral("main"));
    QCOMPARE(repository.files.size(), 4);
    QVERIFY(repository.latestCommit.has_value());
    QVERIFY(repository.firstCommit.has_value());
    QVERIFY(service.getFileDiff(root.path(), QStringLiteral("selected.txt"))
                .contains(QStringLiteral("+selected v2")));
    QVERIFY(service.getFileDiff(root.path(), QStringLiteral("new.txt"))
                .startsWith(QStringLiteral("--- /dev/null\n+++ b/new.txt")));
    QCOMPARE(service.getFileDiff(root.path(), QStringLiteral("binary.dat")),
             QStringLiteral("Binary file — preview unavailable"));

    relay::Account account;
    account.name = QStringLiteral("Relay Committer");
    account.email = QStringLiteral("relay@example.com");
    service.commitFiles(root.path(), {QStringLiteral("selected.txt")},
                        QStringLiteral("Selected only"), QStringLiteral("Commit body"), account);

    const auto status = runGit(root.path(), {QStringLiteral("status"),
                                             QStringLiteral("--porcelain=v1")});
    QVERIFY(!status.contains(QStringLiteral("selected.txt")));
    QVERIFY(status.contains(QStringLiteral("other.txt")));
    QVERIFY(status.contains(QStringLiteral("new.txt")));
    const auto identity = runGit(
        root.path(),
        {QStringLiteral("show"), QStringLiteral("-s"),
         QStringLiteral("--format=%an%x1f%ae%x1f%cn%x1f%ce%x1f%B"), QStringLiteral("HEAD")});
    QVERIFY(identity.startsWith(QStringLiteral(
        "Relay Committer\x1frelay@example.com\x1fRelay Committer\x1frelay@example.com\x1fSelected only")));
    QVERIFY(identity.contains(QStringLiteral("Commit body")));

    runGit(root.path(), {QStringLiteral("branch"), QStringLiteral("side")});
    service.switchBranch(root.path(), QStringLiteral("side"));
    QCOMPARE(service.readRepositorySummary(root.path()).branch, QStringLiteral("side"));
    QVERIFY_THROWS_EXCEPTION(
        relay::ProcessError,
        service.switchBranch(root.path(), QStringLiteral("bad branch")));
  }

  void commitsRenamesAndStagedDeletionsWithoutUnselectedChanges() {
    QTemporaryDir root;
    initRepository(root.path());
    writeFile(root.filePath(QStringLiteral("old.txt")), QByteArrayLiteral("original\n"));
    writeFile(root.filePath(QStringLiteral("gone.txt")), QByteArrayLiteral("remove\n"));
    writeFile(root.filePath(QStringLiteral("other.txt")), QByteArrayLiteral("untouched\n"));
    commitAll(root.path(), QStringLiteral("Initial"));
    runGit(root.path(), {QStringLiteral("mv"), QStringLiteral("old.txt"), QStringLiteral("new.txt")});
    runGit(root.path(), {QStringLiteral("rm"), QStringLiteral("gone.txt")});
    writeFile(root.filePath(QStringLiteral("other.txt")), QByteArrayLiteral("unselected\n"));
    runGit(root.path(), {QStringLiteral("add"), QStringLiteral("other.txt")});
    relay::Account account;
    account.name = QStringLiteral("Test"); account.email = QStringLiteral("test@example.com");
    relay::GitService service;
    service.commitFiles(root.path(), {QStringLiteral("new.txt"), QStringLiteral("gone.txt")},
                        QStringLiteral("Rename and delete"), {}, account);
    const auto tree = runGit(root.path(), {QStringLiteral("ls-tree"), QStringLiteral("--name-only"), QStringLiteral("HEAD")});
    QVERIFY(tree.contains(QStringLiteral("new.txt")));
    QVERIFY(!tree.contains(QStringLiteral("old.txt")));
    QVERIFY(!tree.contains(QStringLiteral("gone.txt")));
    QCOMPARE(runGit(root.path(), {QStringLiteral("show"), QStringLiteral("HEAD:other.txt")}), QStringLiteral("untouched"));
    QCOMPARE(service.readRepository(root.path()).files.size(), 1);
    const auto detail = service.readCommitDetail(root.path(), runGit(root.path(), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}));
    QCOMPARE(fileNamed(detail.files, QStringLiteral("new.txt")).path, QStringLiteral("new.txt"));
    const auto diff = service.readCommitFileDiff(root.path(), detail.fullHash, QStringLiteral("new.txt"));
    QVERIFY(diff.contains(QStringLiteral("rename from old.txt")));
    QVERIFY(diff.contains(QStringLiteral("rename to new.txt")));
  }

  void preservesUnusualPathsInStatusHistoryAndSelection() {
    QTemporaryDir root;
    initRepository(root.path());
    QStringList names{QStringLiteral("-dash [brackets].txt"), QString::fromUtf8("žluťoučký.txt"), QStringLiteral(" space .txt")};
#ifndef Q_OS_WIN
    names.append({QStringLiteral("name -> suffix.txt"), QStringLiteral("tab\tfile.txt"), QStringLiteral("line\nfile.txt"),
                  QStringLiteral("quote\"file.txt"), QStringLiteral(":(glob)*")});
#endif
    for (const auto& name : names) writeFile(root.path() + u'/' + name, QByteArrayLiteral("initial\n"));
    commitAll(root.path(), QStringLiteral("Initial"));
    for (const auto& name : names) writeFile(root.path() + u'/' + name, QByteArrayLiteral("changed\n"));
    writeFile(root.filePath(QStringLiteral("secret.txt")), QByteArrayLiteral("do not commit\n"));
    relay::GitService service;
    const auto repo = service.readRepository(root.path());
    QCOMPARE(repo.files.size(), names.size() + 1);
    QCOMPARE(service.readRepositorySummary(root.path()).changes, names.size() + 1);
    for (const auto& name : names) {
      QCOMPARE(fileNamed(repo.files, name).path, name);
      QVERIFY(service.getFileDiff(root.path(), name).contains(QStringLiteral("+changed")));
    }
    relay::Account account;
    account.name = QStringLiteral("Test"); account.email = QStringLiteral("test@example.com");
    for (const auto& name : names) {
      service.commitFiles(root.path(), {name}, QStringLiteral("One selected file"), {}, account);
      const auto hash = runGit(root.path(), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
      const auto detail = service.readCommitDetail(root.path(), hash);
      QCOMPARE(detail.files.size(), 1);
      QCOMPARE(detail.files.constFirst().path, name);
      QVERIFY(service.readCommitFileDiff(root.path(), hash, name).contains(QStringLiteral("+changed")));
    }
    QCOMPARE(service.readRepository(root.path()).files.size(), 1);
    QCOMPARE(service.readRepository(root.path()).files.constFirst().path, QStringLiteral("secret.txt"));
  }

  void boundsUntrackedPreviews() {
    QTemporaryDir root;
    initRepository(root.path());
    QFile file(root.filePath(QStringLiteral("large.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(3 * 1024 * 1024));
    file.close();
    relay::GitService service;
    QVERIFY(service.getFileDiff(root.path(), QStringLiteral("large.txt")).contains(QStringLiteral("too large")));
  }

  void handlesRepositoryWithoutCommitsOrOrigin() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    initRepository(root.path());

    const relay::GitService service;
    const auto repository = service.readRepository(root.path());
    QCOMPARE(repository.branch, QStringLiteral("main"));
    QVERIFY(repository.history.isEmpty());
    QVERIFY(!repository.latestCommit.has_value());
    QVERIFY(!repository.firstCommit.has_value());
    QCOMPARE(repository.ahead, 0);
    QCOMPARE(repository.behind, 0);
    QVERIFY(!repository.hasUpstream);

    const auto summary = service.readRepositorySummary(root.path());
    QVERIFY(!summary.latestCommit.has_value());
    QVERIFY(!summary.firstCommit.has_value());
    const auto history = service.readHistoryPage(root.path());
    QVERIFY(history.commits.isEmpty());
    QVERIFY(history.head.isEmpty());
    QVERIFY(history.anchor.isEmpty());
    QVERIFY(history.endOfHistory);

    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.fetchOrigin(root.path()));
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, service.pushOrigin(root.path()));
  }

  void oversizedDiffIsAPreviewMessageNotAnError() {
    QTemporaryDir root;
    initRepository(root.path());
    const auto path = QDir(root.path()).filePath(QStringLiteral("large.txt"));
    writeFile(path, QByteArrayLiteral("small\n"));
    commitAll(root.path(), QStringLiteral("Small file"));
    QByteArray large;
    const QByteArray line(79, 'x');
    while (large.size() < 25 * 1024 * 1024) large += line + '\n';
    writeFile(path, large);
    relay::GitService service;
    QCOMPARE(service.getFileDiff(root.path(), QStringLiteral("large.txt")),
             QStringLiteral("Diff is too large to preview (limit: 20 MiB)."));
    commitAll(root.path(), QStringLiteral("Large file"));
    const auto head = runGit(root.path(), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    QCOMPARE(service.readCommitFileDiff(root.path(), head, QStringLiteral("large.txt")),
             QStringLiteral("Diff is too large to preview (limit: 20 MiB)."));
  }

  void signsCommitsWithTheAccountSshKey() {
    const auto keygen = QStandardPaths::findExecutable(QStringLiteral("ssh-keygen"));
    if (keygen.isEmpty()) QSKIP("ssh-keygen is not available.");
    QTemporaryDir root;
    // The developer's own Git configuration may already sign every commit.
    const auto previousGlobal = qgetenv("GIT_CONFIG_GLOBAL");
    const auto previousNoSystem = qgetenv("GIT_CONFIG_NOSYSTEM");
    qputenv("GIT_CONFIG_GLOBAL", root.filePath(QStringLiteral("empty-gitconfig")).toUtf8());
    qputenv("GIT_CONFIG_NOSYSTEM", "1");
    const auto restoreConfig = qScopeGuard([&] {
      if (previousGlobal.isNull()) qunsetenv("GIT_CONFIG_GLOBAL"); else qputenv("GIT_CONFIG_GLOBAL", previousGlobal);
      if (previousNoSystem.isNull()) qunsetenv("GIT_CONFIG_NOSYSTEM"); else qputenv("GIT_CONFIG_NOSYSTEM", previousNoSystem);
    });
    const auto repository = root.filePath(QStringLiteral("repository"));
    QVERIFY(QDir().mkpath(repository));
    initRepository(repository);
    const auto key = root.filePath(QStringLiteral("signing"));
    relay::ProcessRequest generate{keygen, {QStringLiteral("-q"), QStringLiteral("-t"), QStringLiteral("ed25519"),
                                            QStringLiteral("-N"), QString{}, QStringLiteral("-f"), key}};
    static_cast<void>(relay::ProcessRunner::run(generate));
    QVERIFY(QFileInfo::exists(key));

    relay::Account account;
    account.name = QStringLiteral("Signer");
    account.email = QStringLiteral("signer@example.test");
    relay::GitService service;
    writeFile(QDir(repository).filePath(QStringLiteral("unsigned.txt")), QByteArrayLiteral("plain\n"));
    service.commitFiles(repository, {QStringLiteral("unsigned.txt")}, QStringLiteral("Unsigned"), {}, account);
    QVERIFY(!service.readCommitDetail(repository, runGit(repository, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")})).isSigned);

    account.signingKey = key;
    writeFile(QDir(repository).filePath(QStringLiteral("signed.txt")), QByteArrayLiteral("signed\n"));
    service.commitFiles(repository, {QStringLiteral("signed.txt")}, QStringLiteral("Signed"), {}, account);
    auto head = runGit(repository, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    QVERIFY(service.readCommitDetail(repository, head).isSigned);
    QVERIFY(runGit(repository, {QStringLiteral("cat-file"), QStringLiteral("commit"), head}).contains(QStringLiteral("BEGIN SSH SIGNATURE")));
    // Commit-producing repository actions sign the same way.
    service.performAction(repository, relay::RepositoryAction::amendMessage, QStringLiteral("Signed again"), {head}, account);
    head = runGit(repository, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    QVERIFY(service.readCommitDetail(repository, head).isSigned);

    // Configuration already passed through GIT_CONFIG_* is kept, not replaced.
    const auto previousCount = qgetenv("GIT_CONFIG_COUNT");
    const auto previousKey = qgetenv("GIT_CONFIG_KEY_0");
    const auto previousValue = qgetenv("GIT_CONFIG_VALUE_0");
    qputenv("GIT_CONFIG_COUNT", "1");
    qputenv("GIT_CONFIG_KEY_0", "relay.test");
    qputenv("GIT_CONFIG_VALUE_0", "kept");
    const auto restore = qScopeGuard([&] {
      if (previousCount.isNull()) qunsetenv("GIT_CONFIG_COUNT"); else qputenv("GIT_CONFIG_COUNT", previousCount);
      if (previousKey.isNull()) qunsetenv("GIT_CONFIG_KEY_0"); else qputenv("GIT_CONFIG_KEY_0", previousKey);
      if (previousValue.isNull()) qunsetenv("GIT_CONFIG_VALUE_0"); else qputenv("GIT_CONFIG_VALUE_0", previousValue);
    });
    QProcessEnvironment environment;
    relay::GitService::addSigningConfiguration(environment, account);
    QCOMPARE(environment.value(QStringLiteral("GIT_CONFIG_COUNT")), QStringLiteral("4"));
    QVERIFY(!environment.contains(QStringLiteral("GIT_CONFIG_KEY_0")));
    QCOMPARE(environment.value(QStringLiteral("GIT_CONFIG_KEY_3")), QStringLiteral("commit.gpgsign"));
  }

  void searchesWholeHistoryByMessageAuthorAndHash() {
    QTemporaryDir root;
    initRepository(root.path());
    const auto commitAs = [&](const QString& name, const QString& email, const QString& subject, const QString& body) {
      auto environment = gitIdentity();
      environment.insert(QStringLiteral("GIT_AUTHOR_NAME"), name);
      environment.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), email);
      runGit(root.path(), {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-q"),
                           QStringLiteral("-m"), subject, QStringLiteral("-m"), body}, environment);
      return runGit(root.path(), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    };
    const auto first = commitAs(QStringLiteral("Ada"), QStringLiteral("ada@example.com"), QStringLiteral("Initial"), QStringLiteral("Nothing special"));
    const auto body = commitAs(QStringLiteral("Ada"), QStringLiteral("ada@example.com"), QStringLiteral("Tidy"), QStringLiteral("Fixes the FROBNICATOR"));
    const auto byLinus = commitAs(QStringLiteral("Linus"), QStringLiteral("linus@example.com"), QStringLiteral("Merge work"), QStringLiteral("x"));
    for (int index = 0; index < 5; ++index)
      commitAs(QStringLiteral("Ada"), QStringLiteral("ada@example.com"), QStringLiteral("Repeat %1").arg(index), QStringLiteral("repeat"));

    relay::GitService service;
    const auto hashes = [](const relay::GitService::SearchResult& result) {
      QStringList list;
      for (const auto& commit : result.commits) list.append(commit.fullHash);
      return list;
    };
    // Message body, case-insensitive.
    QCOMPARE(hashes(service.searchHistory(root.path(), QStringLiteral("frobnicator"))), QStringList{body});
    // Author name and email; Git would AND --grep with --author.
    QCOMPARE(hashes(service.searchHistory(root.path(), QStringLiteral("linus"))), QStringList{byLinus});
    QCOMPARE(hashes(service.searchHistory(root.path(), QStringLiteral("linus@example"))), QStringList{byLinus});
    // A hash prefix, and characters that would be regular expressions.
    QCOMPARE(hashes(service.searchHistory(root.path(), first.left(10))), QStringList{first});
    QVERIFY(service.searchHistory(root.path(), QStringLiteral("Repeat .*")).commits.isEmpty());
    // Newest first, truncated at the limit.
    const auto limited = service.searchHistory(root.path(), QStringLiteral("repeat"), false, {}, 3);
    QCOMPARE(limited.commits.size(), 3);
    QVERIFY(limited.truncated);
    QCOMPARE(limited.commits.first().title, QStringLiteral("Repeat 4"));
    QVERIFY(!service.searchHistory(root.path(), QStringLiteral("repeat")).truncated);
    QVERIFY(service.searchHistory(root.path(), QStringLiteral("   ")).commits.isEmpty());
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError,
        static_cast<void>(service.searchHistory(root.path(), QStringLiteral("x"), false, QStringLiteral("--all"))));
  }

  void pagesAcrossCachedHistoryWindowsWithoutGapsOrRepeats() {
    QTemporaryDir root;
    initRepository(root.path());
    // 2,100 commits crosses the date-order window of 2,000.
    QByteArray stream;
    for (int index = 1; index <= 2100; ++index) {
      const auto message = QByteArray("commit ") + QByteArray::number(index);
      stream += "commit refs/heads/main\nmark :" + QByteArray::number(index) +
                "\ncommitter Fixture <fixture@example.test> " + QByteArray::number(1700000000 + index) +
                " +0000\ndata " + QByteArray::number(message.size()) + "\n" + message + "\n";
      if (index > 1) stream += "from :" + QByteArray::number(index - 1) + "\n";
      stream += "\n";
    }
    relay::ProcessRequest import{QStringLiteral("git"), {QStringLiteral("-C"), root.path(), QStringLiteral("fast-import"), QStringLiteral("--quiet")}};
    import.standardInput = stream;
    static_cast<void>(relay::ProcessRunner::run(import));
    runGit(root.path(), {QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("--hard"), QStringLiteral("main")});
    const auto expected = runGit(root.path(), {QStringLiteral("rev-list"), QStringLiteral("HEAD")}).split(u'\n');
    QCOMPARE(expected.size(), 2100);

    relay::GitService service;
    for (const bool topological : {false, true}) {
      QStringList seen;
      auto page = service.readHistoryPage(root.path(), 0, 200, {}, false, {}, topological);
      for (const auto& commit : page.commits) seen.append(commit.fullHash);
      while (!page.endOfHistory) {
        page = service.readHistoryPage(root.path(), static_cast<int>(seen.size()), 200, page.anchor, false, {}, topological);
        QVERIFY(!page.commits.isEmpty());
        for (const auto& commit : page.commits) seen.append(commit.fullHash);
      }
      QCOMPARE(seen, expected);
    }
  }

  void pagesAnchoredHistoryAndDescribesRootMergeAndDeletion() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    populateHistoryFixture(root.path());
    const relay::GitService service;

    const auto first = service.readHistoryPage(root.path(), 0, 3);
    QCOMPARE(first.commits.size(), 3);
    QVERIFY(!first.endOfHistory);
    QVERIFY(!first.anchor.isEmpty());

    // Move HEAD after the first page. Later pages must remain anchored to the
    // original tip and neither include this commit nor lose a boundary item.
    writeFile(QDir(root.path()).filePath(QStringLiteral("moving.txt")),
              QByteArrayLiteral("new head\n"));
    commitAll(root.path(), QStringLiteral("Commit after paging began"));

    QList<relay::HistoryCommit> commits = first.commits;
    auto page = first;
    for (int iteration = 0; !page.endOfHistory && iteration < 10; ++iteration) {
      page = service.readHistoryPage(root.path(), static_cast<int>(commits.size()), 3,
                                     first.anchor);
      for (const auto& commit : page.commits) {
        const auto duplicate =
            std::any_of(commits.cbegin(), commits.cend(), [&commit](const auto& existing) {
              return existing.fullHash == commit.fullHash;
            });
        QVERIFY(!duplicate);
        commits.push_back(commit);
      }
    }
    QCOMPARE(commits.size(), 6);
    QCOMPARE(commits.constLast().title, QStringLiteral("Root commit"));
    QVERIFY(std::none_of(commits.cbegin(), commits.cend(), [](const auto& commit) {
      return commit.title == QStringLiteral("Commit after paging began");
    }));

    const auto rootDetail =
        service.readCommitDetail(root.path(), commitNamed(commits, QStringLiteral("Root commit")).fullHash);
    QVERIFY(rootDetail.isRoot);
    QVERIFY(rootDetail.parents.isEmpty());
    QCOMPARE(rootDetail.body, QStringLiteral("Body of the root commit."));
    QCOMPARE(rootDetail.author, QStringLiteral("Ada Lovelace"));
    QCOMPARE(rootDetail.committer, QStringLiteral("Grace Hopper"));
    QCOMPARE(rootDetail.files.size(), 1);
    QVERIFY(rootDetail.files.constFirst().status == relay::FileStatus::added);

    const auto mergeDetail = service.readCommitDetail(
        root.path(), commitNamed(commits, QStringLiteral("Merge side into main")).fullHash);
    QVERIFY(mergeDetail.isMerge);
    QCOMPARE(mergeDetail.parents.size(), 2);
    QCOMPARE(mergeDetail.files.size(), 1);
    QCOMPARE(mergeDetail.files.constFirst().path, QStringLiteral("side.txt"));

    const auto deletion = service.readCommitDetail(
        root.path(), commitNamed(commits, QStringLiteral("Remove the temporary file")).fullHash);
    QCOMPARE(deletion.files.size(), 1);
    QVERIFY(deletion.files.constFirst().status == relay::FileStatus::deleted);
    QCOMPARE(deletion.files.constFirst().removed, 1);
    QVERIFY(service.readCommitFileDiff(root.path(), rootDetail.fullHash, QStringLiteral("root.txt"))
                .contains(QStringLiteral("+root")));

    QVERIFY_THROWS_EXCEPTION(
        relay::ProcessError,
        static_cast<void>(
            service.readCommitDetail(root.path(), QStringLiteral("--bad"))));
    QVERIFY_THROWS_EXCEPTION(
        relay::ProcessError,
        static_cast<void>(service.readCommitDetail(root.path(), QString(40, u'0'))));
    QVERIFY_THROWS_EXCEPTION(
        relay::ProcessError,
        static_cast<void>(service.readCommitFileDiff(root.path(), first.anchor, {})));

    QTemporaryDir other;
    QVERIFY(other.isValid());
    initRepository(other.path());
    writeFile(QDir(other.path()).filePath(QStringLiteral("foreign.txt")),
              QByteArrayLiteral("foreign\n"));
    commitAll(other.path(), QStringLiteral("Only in the other repository"));
    const auto foreign = runGit(
        other.path(), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    QVERIFY_THROWS_EXCEPTION(
        relay::ProcessError,
        static_cast<void>(service.readCommitDetail(root.path(), foreign)));
  }

  void clonesFetchesAndPushesLocalTransport() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto server = QDir(root.path()).filePath(QStringLiteral("server.git"));
    runGit(root.path(), {QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("--bare"),
                         QStringLiteral("-b"), QStringLiteral("main"), server});

    const auto seed = QDir(root.path()).filePath(QStringLiteral("seed"));
    QVERIFY(QDir().mkpath(seed));
    initRepository(seed);
    writeFile(QDir(seed).filePath(QStringLiteral("file.txt")), QByteArrayLiteral("first\n"));
    commitAll(seed, QStringLiteral("Initial remote commit"));
    runGit(seed, {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"),
                  server});
    runGit(seed, {QStringLiteral("push"), QStringLiteral("-q"), QStringLiteral("origin"),
                  QStringLiteral("main")});

    const relay::GitService service;
    const auto clone = QDir(root.path()).filePath(QStringLiteral("clone"));
    const auto repository = service.cloneRepository(server, clone);
    QCOMPARE(repository.branch, QStringLiteral("main"));
    QCOMPARE(repository.history.constFirst().title, QStringLiteral("Initial remote commit"));
    service.fetchOrigin(clone);

    writeFile(QDir(clone).filePath(QStringLiteral("file.txt")),
              QByteArrayLiteral("first\nsecond\n"));
    relay::Account account;
    account.name = QStringLiteral("Push Test");
    account.email = QStringLiteral("push@example.com");
    service.commitFiles(clone, {QStringLiteral("file.txt")}, QStringLiteral("Pushed commit"),
                        {}, account);
    service.pushOrigin(clone);
    QVERIFY(runGit(server, {QStringLiteral("log"), QStringLiteral("--oneline"),
                            QStringLiteral("main")})
                .contains(QStringLiteral("Pushed commit")));

    QVERIFY(relay::GitService::isSshRemote(QStringLiteral("git@example.com:team/repo.git")));
    QVERIFY(relay::GitService::isSshRemote(QStringLiteral("ssh://git@example.com/repo.git")));
    QVERIFY(!relay::GitService::isSshRemote(QStringLiteral("https://example.com/repo.git")));
    QVERIFY(!relay::GitService::isSshRemote(QStringLiteral("C:\\repo")));
  }
};

QTEST_APPLESS_MAIN(GitServiceTest)
#include "tst_git_service.moc"
