#include "relay/process_runner.hpp"
#include "relay/relay_controller.hpp"
#include "relay/relay_store.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

void writeFile(const QString& path, const QByteArray& contents) {
  QFile file(path);
  QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
           qPrintable(QStringLiteral("Could not write %1: %2").arg(path, file.errorString())));
  QCOMPARE(file.write(contents), contents.size());
}

void runGit(const QString& root, QStringList arguments) {
  arguments.prepend(root);
  arguments.prepend(QStringLiteral("-C"));
  relay::ProcessRequest request{QStringLiteral("git"), arguments};
  static_cast<void>(relay::ProcessRunner::run(request));
}

void initRepository(const QString& root) {
  runGit(root, {QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"),
                QStringLiteral("main"), QStringLiteral(".")});
}

relay::RepositorySummary summary(const QString& path, const QString& name) {
  const auto now = QDateTime::fromString(QStringLiteral("2026-08-26T10:00:00Z"), Qt::ISODate);
  return {path, name, QStringLiteral("owner"), QStringLiteral("main"), 0, now, now,
          std::nullopt, std::nullopt};
}

relay::RelayControllerConfig controllerConfig(const QTemporaryDir& root) {
  relay::RelayControllerConfig config;
  config.storeFile = root.filePath(QStringLiteral("profile/relay-data.json"));
  config.sourceRoot = root.path();
  config.resourcesRoot = root.filePath(QStringLiteral("resources"));
  config.synchronizeAccountsOnStart = false;
  return config;
}

void seedState(const relay::RelayControllerConfig& config, const relay::AppState& state) {
  relay::RelayStore(config.storeFile).write(relay::mergeAppStateIntoJson(state));
}

}  // namespace

class RelayControllerTest final : public QObject {
  Q_OBJECT

 private slots:
  void startupLoadsStateButNeverRestoresRepository() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto repositoryPath = root.filePath(QStringLiteral("remembered"));
    QVERIFY(QDir().mkpath(repositoryPath));
    auto state = relay::AppState{};
    state.repositories = {summary(repositoryPath, QStringLiteral("remembered"))};
    state.manualOrder = {repositoryPath};
    const auto config = controllerConfig(root);
    seedState(config, state);

    relay::RelayController controller(config);
    QSignalSpy stateChanged(&controller, &relay::RelayController::stateChanged);
    QSignalSpy closed(&controller, &relay::RelayController::repositoryClosed);
    controller.start();

    QCOMPARE(stateChanged.size(), 1);
    QCOMPARE(closed.size(), 1);
    QVERIFY(controller.currentRepository() == nullptr);
    QCOMPARE(controller.state().repositories.size(), 1);
    QCOMPARE(controller.state().repositories.constFirst().path, repositoryPath);
  }

  void removingOpenRepositoryOnlyRemovesRelayMetadata() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto repositoryPath = root.filePath(QStringLiteral("repository"));
    QVERIFY(QDir().mkpath(repositoryPath));
    initRepository(repositoryPath);
    const auto keptFile = QDir(repositoryPath).filePath(QStringLiteral("keep.txt"));
    writeFile(keptFile, QByteArrayLiteral("must remain\n"));

    relay::AppState state;
    state.repositories = {summary(repositoryPath, QStringLiteral("repository"))};
    state.manualOrder = {repositoryPath};
    state.accounts = {{QStringLiteral("github-1"), 1, {}, QStringLiteral("User"),
                       QStringLiteral("user"), QStringLiteral("1+user@users.noreply.github.com"),
                       QStringLiteral("U"), QStringLiteral("coral"), QStringLiteral("User"),
                       {}, {}, QStringLiteral("github-cli"), QStringLiteral("credential store"),
                       true}};
    state.activeAccountId = QStringLiteral("github-1");
    state.repositoryAccounts.insert(repositoryPath, QStringLiteral("github-1"));
    state.sshProfiles = {{QStringLiteral("ssh-work"), QStringLiteral("Work"),
                          QStringLiteral("git.example.com"), QStringLiteral("git"), std::nullopt,
                          {}, true}};
    state.repositorySshProfiles.insert(repositoryPath, QStringLiteral("ssh-work"));
    const auto config = controllerConfig(root);
    seedState(config, state);

    relay::RelayController controller(config);
    controller.start();
    QSignalSpy opened(&controller, &relay::RelayController::currentRepositoryChanged);
    controller.openRepository(repositoryPath);
    QVERIFY(opened.wait(5000));
    QVERIFY(controller.currentRepository() != nullptr);

    QSignalSpy closed(&controller, &relay::RelayController::repositoryClosed);
    controller.removeRepository(repositoryPath);
    QCOMPARE(closed.size(), 1);
    QVERIFY(controller.currentRepository() == nullptr);
    QVERIFY(controller.state().repositories.isEmpty());
    QVERIFY(!controller.state().repositoryAccounts.contains(repositoryPath));
    QVERIFY(controller.state().manualOrder.isEmpty());
    // Matches Relay 0.5.0: removing the shortcut leaves the SSH association in
    // metadata so re-adding the same path can retain its transport identity.
    QCOMPARE(controller.state().repositorySshProfiles.value(repositoryPath),
             QStringLiteral("ssh-work"));

    QVERIFY(QFile::exists(keptFile));
    QFile file(keptFile);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("must remain\n"));
    QVERIFY(QFileInfo::exists(QDir(repositoryPath).filePath(QStringLiteral(".git"))));

    const auto stored = relay::appStateFromJson(relay::RelayStore(config.storeFile).read());
    QVERIFY(stored.repositories.isEmpty());
    QVERIFY(!stored.repositoryAccounts.contains(repositoryPath));
  }

  void staleRepositoryOpenCannotReplaceNewerSelection() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto slow = root.filePath(QStringLiteral("slow"));
    const auto fast = root.filePath(QStringLiteral("fast"));
    QVERIFY(QDir().mkpath(slow));
    QVERIFY(QDir().mkpath(fast));
    initRepository(slow);
    initRepository(fast);

    QSemaphore slowStarted;
    QSemaphore releaseSlow;
    auto config = controllerConfig(root);
    config.operationGate = [&](const QString& operation, const QString& key) {
      if (operation == QStringLiteral("open-repository") && key == slow) {
        slowStarted.release();
        releaseSlow.acquire();
      }
    };

    relay::RelayController controller(config);
    controller.start();
    QSignalSpy repositories(&controller, &relay::RelayController::currentRepositoryChanged);
    QSignalSpy busy(&controller, &relay::RelayController::busyChanged);
    controller.openRepository(slow);
    QVERIFY(slowStarted.tryAcquire(1, 5000));
    controller.openRepository(fast);
    QVERIFY(repositories.wait(5000));
    QCOMPARE(repositories.size(), 1);
    QVERIFY(controller.currentRepository() != nullptr);
    QCOMPARE(controller.currentRepository()->path, QFileInfo(fast).canonicalFilePath());

    releaseSlow.release();
    QTRY_VERIFY_WITH_TIMEOUT(busy.size() >= 4, 5000);
    QCOMPARE(repositories.size(), 1);
    QCOMPARE(controller.currentRepository()->path, QFileInfo(fast).canonicalFilePath());
  }

  void staleFileDiffCannotReplaceNewerFile() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto repositoryPath = root.filePath(QStringLiteral("repository"));
    QVERIFY(QDir().mkpath(repositoryPath));
    initRepository(repositoryPath);
    writeFile(QDir(repositoryPath).filePath(QStringLiteral("slow.txt")),
              QByteArrayLiteral("slow\n"));
    writeFile(QDir(repositoryPath).filePath(QStringLiteral("fast.txt")),
              QByteArrayLiteral("fast\n"));

    QSemaphore slowStarted;
    QSemaphore releaseSlow;
    auto config = controllerConfig(root);
    config.operationGate = [&](const QString& operation, const QString& key) {
      if (operation == QStringLiteral("file-diff") && key == QStringLiteral("slow.txt")) {
        slowStarted.release();
        releaseSlow.acquire();
      }
    };

    relay::RelayController controller(config);
    controller.start();
    QSignalSpy opened(&controller, &relay::RelayController::currentRepositoryChanged);
    controller.openRepository(repositoryPath);
    QVERIFY(opened.wait(5000));

    QSignalSpy diffs(&controller, &relay::RelayController::fileDiffReady);
    QSignalSpy busy(&controller, &relay::RelayController::busyChanged);
    controller.requestFileDiff(QStringLiteral("slow.txt"));
    QVERIFY(slowStarted.tryAcquire(1, 5000));
    controller.requestFileDiff(QStringLiteral("fast.txt"));
    QVERIFY(diffs.wait(5000));
    QCOMPARE(diffs.size(), 1);
    QCOMPARE(diffs.constFirst().at(1).toString(), QStringLiteral("fast.txt"));
    QVERIFY(diffs.constFirst().at(2).toString().contains(QStringLiteral("+fast")));

    releaseSlow.release();
    QTRY_VERIFY_WITH_TIMEOUT(busy.size() >= 4, 5000);
    QCOMPARE(diffs.size(), 1);
  }

  void persistsOrderingAndBindingsWithValidation() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto first = root.filePath(QStringLiteral("first"));
    const auto second = root.filePath(QStringLiteral("second"));
    relay::AppState state;
    state.repositories = {summary(first, QStringLiteral("first")),
                          summary(second, QStringLiteral("second"))};
    state.manualOrder = {first, second};
    state.accounts = {{QStringLiteral("github-1"), 1, {}, QStringLiteral("User"),
                       QStringLiteral("user"), QStringLiteral("1+user@users.noreply.github.com"),
                       QStringLiteral("U"), QStringLiteral("coral"), QStringLiteral("User"),
                       {}, {}, QStringLiteral("github-cli"), QStringLiteral("credential store"),
                       true}};
    state.activeAccountId = QStringLiteral("github-1");
    const auto config = controllerConfig(root);
    seedState(config, state);

    relay::RelayController controller(config);
    controller.start();
    QSignalSpy failures(&controller, &relay::RelayController::operationFailed);
    controller.setRepositoryOrder(relay::RepositoryOrderMode::latest,
                                  relay::SortDirection::descending);
    controller.setManualOrder({second, QStringLiteral("/unknown"), second});
    controller.setRepositoryAccount(first, QStringLiteral("github-1"));
    controller.setRepositoryAccount(second, QStringLiteral("missing"));
    QCOMPARE(failures.size(), 1);

    relay::SshProfile profile{QStringLiteral("ssh-work"), QStringLiteral("Work"),
                              QStringLiteral("git.example.com"), {}, std::nullopt, {}, true};
    controller.saveSshProfile(profile);
    controller.setRepositorySshProfile(first, profile.id);
    QCOMPARE(controller.state().repositorySshProfiles.value(first), profile.id);
    controller.removeSshProfile(profile.id);

    QCOMPARE(controller.state().repositoryOrder.mode, relay::RepositoryOrderMode::latest);
    QCOMPARE(controller.state().repositoryOrder.direction,
             relay::SortDirection::descending);
    QCOMPARE(controller.state().manualOrder, QStringList({second, first}));
    QCOMPARE(controller.state().repositoryAccounts.value(first), QStringLiteral("github-1"));
    QVERIFY(!controller.state().repositoryAccounts.contains(second));
    QVERIFY(controller.state().sshProfiles.isEmpty());
    QVERIFY(!controller.state().repositorySshProfiles.contains(first));

    const auto stored = relay::appStateFromJson(relay::RelayStore(config.storeFile).read());
    QCOMPARE(stored.manualOrder, QStringList({second, first}));
    QCOMPARE(stored.repositoryAccounts.value(first), QStringLiteral("github-1"));
  }
};

QTEST_GUILESS_MAIN(RelayControllerTest)
#include "tst_relay_controller.moc"
