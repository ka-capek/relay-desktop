#include "relay/repository_discovery.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

namespace {

void writeFile(const QString& path, const QByteArray& value) {
  QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QCOMPARE(file.write(value), value.size());
}

}  // namespace

class RepositoryDiscoveryTest final : public QObject {
  Q_OBJECT

 private slots:
  void recognizesDirectoriesAndLinkedWorktrees() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto ordinary = root.filePath(QStringLiteral("ordinary"));
    const auto linked = root.filePath(QStringLiteral("linked"));
    const auto invalid = root.filePath(QStringLiteral("invalid"));
    QVERIFY(QDir().mkpath(QDir(ordinary).filePath(QStringLiteral(".git"))));
    writeFile(QDir(linked).filePath(QStringLiteral(".git")), "gitdir: ../storage/worktrees/linked\n");
    writeFile(QDir(invalid).filePath(QStringLiteral(".git")), "not a git directory\n");

    QVERIFY(relay::isGitWorktree(ordinary));
    QVERIFY(relay::isGitWorktree(linked));
    QVERIFY(!relay::isGitWorktree(invalid));
  }

  void scansBreadthFirstWithoutDescendingIntoRepositoriesOrIgnoredFolders() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto ordinary = root.filePath(QStringLiteral("ordinary"));
    const auto linked = root.filePath(QStringLiteral("linked"));
    const auto parent = root.filePath(QStringLiteral("parent"));
    const auto nested = QDir(parent).filePath(QStringLiteral("nested"));
    const auto ignored = root.filePath(QStringLiteral("node_modules/dependency"));
    QVERIFY(QDir().mkpath(QDir(ordinary).filePath(QStringLiteral(".git"))));
    writeFile(QDir(linked).filePath(QStringLiteral(".git")), "gitdir: ../storage/worktrees/linked\n");
    QVERIFY(QDir().mkpath(QDir(parent).filePath(QStringLiteral(".git"))));
    QVERIFY(QDir().mkpath(QDir(nested).filePath(QStringLiteral(".git"))));
    QVERIFY(QDir().mkpath(QDir(ignored).filePath(QStringLiteral(".git"))));

    const auto found = relay::scanForRepositories(root.path());
    QCOMPARE(found.size(), 3);
    QVERIFY(found.contains(ordinary));
    QVERIFY(found.contains(linked));
    QVERIFY(found.contains(parent));
    QVERIFY(!found.contains(nested));
    QVERIFY(!found.contains(ignored));
    QCOMPARE(relay::scanForRepositories(root.path(), 1).size(), 1);
  }
};

QTEST_APPLESS_MAIN(RepositoryDiscoveryTest)
#include "tst_repository_discovery.moc"
