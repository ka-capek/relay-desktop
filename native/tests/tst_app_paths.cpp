#include "relay/app_paths.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

namespace {

void createFile(const QString& path) {
  QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QCOMPARE(file.write("runtime"), 7);
}

}  // namespace

class AppPathsTest final : public QObject {
  Q_OBJECT

 private slots:
  void preservesElectronUserDataLocations() {
    QCOMPARE(
        relay::AppPaths::legacyUserDataDirectoryFor(
            relay::RuntimePlatform::macOSArm64, QStringLiteral("/Users/example")),
        QStringLiteral("/Users/example/Library/Application Support/relay-desktop"));
    QCOMPARE(
        relay::AppPaths::legacyUserDataDirectoryFor(
            relay::RuntimePlatform::windowsX64, QStringLiteral("C:/Users/example"),
            QStringLiteral("C:\\Users\\example\\AppData\\Roaming")),
        QStringLiteral("C:/Users/example/AppData/Roaming/relay-desktop"));
  }

  void prefersPackagedThenDevelopmentRuntimes() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto applicationDirectory = root.filePath(QStringLiteral("Relay.app/Contents/MacOS"));
    const auto sourceDirectory = root.filePath(QStringLiteral("source"));
    const auto packagedGit = root.filePath(QStringLiteral("Relay.app/Contents/Resources/git/bin/git"));
    const auto packagedCli = root.filePath(QStringLiteral("Relay.app/Contents/Resources/gh/gh"));
    const auto developmentGit = root.filePath(QStringLiteral("source/runtime/git/mac-arm64/bin/git"));
    const auto developmentCli = root.filePath(QStringLiteral("source/runtime/gh/mac-arm64/gh"));
    createFile(developmentGit);
    createFile(developmentCli);
    createFile(packagedGit);
    createFile(packagedCli);

    auto paths = relay::AppPaths::resolveRuntimes(
        applicationDirectory, sourceDirectory, relay::RuntimePlatform::macOSArm64);
    QCOMPARE(paths.git.executable, packagedGit);
    QCOMPARE(paths.githubCliExecutable, packagedCli);
    QVERIFY(paths.git.isBundled());

    QVERIFY(QFile::remove(packagedGit));
    QVERIFY(QFile::remove(packagedCli));
    paths = relay::AppPaths::resolveRuntimes(
        applicationDirectory, sourceDirectory, relay::RuntimePlatform::macOSArm64);
    QCOMPARE(paths.git.executable, developmentGit);
    QCOMPARE(paths.githubCliExecutable, developmentCli);
  }

  void buildsRelocatableGitAndScrubbedGithubEnvironments() {
    relay::GitRuntime git{
        QStringLiteral("C:/Relay/resources/git/cmd/git.exe"),
        QStringLiteral("C:/Relay/resources/git"), relay::RuntimePlatform::windowsX64};
    QProcessEnvironment inherited;
    inherited.insert(QStringLiteral("PATH"), QStringLiteral("C:/Windows/System32"));
    const auto gitEnvironment = git.environment(inherited);
    QCOMPARE(
        gitEnvironment.value(QStringLiteral("GIT_EXEC_PATH")),
        QStringLiteral("C:/Relay/resources/git/mingw64/libexec/git-core"));
    QCOMPARE(
        gitEnvironment.value(QStringLiteral("GIT_SSL_CAINFO")),
        QStringLiteral("C:/Relay/resources/git/mingw64/etc/ssl/certs/ca-bundle.crt"));
    QVERIFY(gitEnvironment.value(QStringLiteral("PATH")).startsWith(QStringLiteral("C:/Relay/resources/git/cmd;")));

    inherited.insert(QStringLiteral("GH_TOKEN"), QStringLiteral("secret"));
    inherited.insert(QStringLiteral("GITHUB_TOKEN"), QStringLiteral("secret"));
    inherited.insert(QStringLiteral("GH_HOST"), QStringLiteral("elsewhere.example"));
    const auto githubEnvironment =
        relay::githubCliEnvironment(QStringLiteral("/tmp/relay-desktop"), false, inherited);
    QVERIFY(!githubEnvironment.contains(QStringLiteral("GH_TOKEN")));
    QVERIFY(!githubEnvironment.contains(QStringLiteral("GITHUB_TOKEN")));
    QVERIFY(!githubEnvironment.contains(QStringLiteral("GH_HOST")));
    QCOMPARE(githubEnvironment.value(QStringLiteral("GH_PROMPT_DISABLED")), QStringLiteral("1"));
    QCOMPARE(
        githubEnvironment.value(QStringLiteral("GH_CONFIG_DIR")),
        QStringLiteral("/tmp/relay-desktop/github-cli"));
  }
};

QTEST_APPLESS_MAIN(AppPathsTest)
#include "tst_app_paths.moc"
