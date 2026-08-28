#include "relay/git_service.hpp"
#include "relay/process_runner.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
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
        QStringLiteral("R  old.txt -> renamed.txt\n D deleted.txt\n?? plain.txt\n?? binary.dat"),
        QStringLiteral("3\t2\trenamed.txt\n0\t4\tdeleted.txt"), root.path());
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
    // This deliberately matches the Electron split("\n") behavior.
    QCOMPARE(plain.added, 3);
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
