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
#include <QScopeGuard>

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
  void failedRepositorySaveCannotSwitchTheEffectiveCommitTarget() {
    QTemporaryDir root;
    const auto first = root.filePath(QStringLiteral("first"));
    const auto second = root.filePath(QStringLiteral("second"));
    QVERIFY(QDir().mkpath(first)); QVERIFY(QDir().mkpath(second));
    initRepository(first); initRepository(second);
    const auto config = controllerConfig(root);
    relay::RelayController controller(config);
    controller.start();
    QSignalSpy opened(&controller, &relay::RelayController::currentRepositoryChanged);
    controller.openRepository(first);
    QVERIFY(opened.wait(5000));
    QVERIFY(QFile::remove(config.storeFile));
    QVERIFY(QDir().mkdir(config.storeFile));
    QSignalSpy failed(&controller, &relay::RelayController::operationFailed);
    controller.openRepository(second);
    QVERIFY(failed.wait(5000));
    QCOMPARE(controller.currentRepository()->path, QFileInfo(first).canonicalFilePath());
    QCOMPARE(opened.size(), 1);
  }

  void failedPersistenceKeepsThePreviouslyVisibleIdentity() {
    QTemporaryDir root;
    const auto config = controllerConfig(root);
    relay::AppState state;
    relay::Account first;
    first.id = QStringLiteral("github-1"); first.githubId = 1;
    first.handle = QStringLiteral("personal"); first.email = QStringLiteral("personal@example.test");
    auto second = first; second.id = QStringLiteral("github-2"); second.githubId = 2;
    state.accounts = {first, second}; state.activeAccountId = first.id;
    seedState(config, state);
    relay::RelayController controller(config);
    controller.start();
    // An unreadable replacement at the temporary store path forces saving to fail.
    QVERIFY(QFile::remove(config.storeFile));
    QVERIFY(QDir().mkdir(config.storeFile));
    QSignalSpy failures(&controller, &relay::RelayController::operationFailed);
    controller.setRepositoryAccount(QStringLiteral("/fixture"), second.id);
    QCOMPARE(failures.size(), 1);
    QCOMPARE(controller.resolvedAccountId(QStringLiteral("/fixture")), first.id);
    controller.setAccountEmail(first.id, QStringLiteral("changed@example.test"));
    QCOMPARE(failures.size(), 2);
    QCOMPARE(controller.state().accounts.first().email, first.email);
  }

  void refreshCannotSupersedePendingBranchMutation() {
    QTemporaryDir root;
    initRepository(root.path());
    writeFile(root.filePath(QStringLiteral("file.txt")), "initial\n");
    runGit(root.path(), {QStringLiteral("add"), QStringLiteral(".")});
    runGit(root.path(), {QStringLiteral("-c"), QStringLiteral("user.name=Test"), QStringLiteral("-c"), QStringLiteral("user.email=test@example.test"), QStringLiteral("commit"), QStringLiteral("-qm"), QStringLiteral("Initial")});
    QSemaphore started, release;
    auto config = controllerConfig(root);
    config.operationGate = [&](const QString& operation, const QString&) {
      if (operation == QStringLiteral("create-branch")) { started.release(); release.acquire(); }
    };
    relay::RelayController controller(config);
    controller.start();
    QSignalSpy changed(&controller, &relay::RelayController::currentRepositoryChanged);
    QSignalSpy busy(&controller, &relay::RelayController::busyChanged);
    controller.openRepository(root.path());
    QVERIFY(changed.wait(5000));
    changed.clear(); busy.clear();
    const auto cleanup = qScopeGuard([&] { release.release(); });
    controller.createBranch(QStringLiteral("feature"));
    QVERIFY(started.tryAcquire(1, 5000));
    controller.refreshRepository();
    // Refresh must not start or invalidate the mutation's eventual snapshot.
    QCOMPARE(busy.size(), 1);
    release.release();
    QVERIFY(changed.wait(5000));
    QCOMPARE(controller.currentRepository()->branch, QStringLiteral("feature"));
  }

  void staleRepositoryReadFailuresAreIgnored() {
    QTemporaryDir root;
    initRepository(root.path());
    QSemaphore started, release;
    auto config = controllerConfig(root);
    config.operationGate = [&](const QString& operation, const QString& key) {
      if (operation == QStringLiteral("open-repository") && key.endsWith(QStringLiteral("missing"))) {
        started.release(); release.acquire();
        throw std::runtime_error("Obsolete repository error");
      }
    };
    relay::RelayController controller(config);
    controller.start();
    QSignalSpy changed(&controller, &relay::RelayController::currentRepositoryChanged);
    QSignalSpy failures(&controller, &relay::RelayController::operationFailed);
    QSignalSpy busy(&controller, &relay::RelayController::busyChanged);
    const auto cleanup = qScopeGuard([&] { release.release(); });
    controller.openRepository(root.filePath(QStringLiteral("missing")));
    QVERIFY(started.tryAcquire(1, 5000));
    controller.openRepository(root.path());
    QVERIFY(changed.wait(5000));
    release.release();
    QTRY_COMPARE_WITH_TIMEOUT(busy.size(), 4, 5000);
    QVERIFY(failures.isEmpty());
    QCOMPARE(controller.currentRepository()->path, QFileInfo(root.path()).canonicalFilePath());
  }

  void completedCloneKeepsAccountWhenSelectionChanges() {
    QTemporaryDir root;
    const auto source = root.filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(source));
    initRepository(source);
    const auto globalConfig = root.filePath(QStringLiteral("gitconfig"));
    runGit(root.path(), {QStringLiteral("config"), QStringLiteral("--file"), globalConfig,
        QStringLiteral("url.%1.insteadOf").arg(source), QStringLiteral("git@github.com:relay-test/source")});
    runGit(root.path(), {QStringLiteral("config"), QStringLiteral("--file"), globalConfig, QStringLiteral("protocol.file.allow"), QStringLiteral("always")});
    const auto previousGlobal = qgetenv("GIT_CONFIG_GLOBAL");
    const auto restoreEnvironment = qScopeGuard([&] {
      if (previousGlobal.isNull()) qunsetenv("GIT_CONFIG_GLOBAL");
      else qputenv("GIT_CONFIG_GLOBAL", previousGlobal);
    });
    qputenv("GIT_CONFIG_GLOBAL", globalConfig.toUtf8());
    QSemaphore started, release;
    auto config = controllerConfig(root);
    config.operationGate = [&](const QString& operation, const QString&) {
      if (operation == QStringLiteral("clone")) { started.release(); release.acquire(); }
    };
    relay::AppState state;
    relay::Account personal;
    personal.id = QStringLiteral("github-1"); personal.githubId = 1; personal.handle = QStringLiteral("personal");
    personal.authSource = QStringLiteral("github-cli");
    auto work = personal;
    work.id = QStringLiteral("github-2"); work.githubId = 2; work.handle = QStringLiteral("work");
    state.accounts = {personal, work}; state.activeAccountId = personal.id;
    seedState(config, state);
    relay::RelayController controller(config);
    controller.start();
    QSignalSpy changed(&controller, &relay::RelayController::currentRepositoryChanged);
    QSignalSpy failures(&controller, &relay::RelayController::operationFailed);
    QSignalSpy busy(&controller, &relay::RelayController::busyChanged);
    const auto cleanup = qScopeGuard([&] { release.release(); });
    controller.cloneRepository(QStringLiteral("git@github.com:relay-test/source"), root.path(), QStringLiteral("clone"), work.id);
    QVERIFY(started.tryAcquire(1, 5000));
    controller.setActiveAccount(work.id);
    QCOMPARE(failures.size(), 1);
    QCOMPARE(controller.state().activeAccountId, personal.id);
    controller.createLocalRepository(root.filePath(QStringLiteral("blocked-create")));
    QCOMPARE(failures.size(), 2);
    QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("blocked-create"))));
    failures.clear();
    controller.openRepository(source);
    QVERIFY(changed.wait(5000));
    release.release();
    QTRY_COMPARE_WITH_TIMEOUT(busy.size(), 4, 5000);
    QVERIFY2(failures.isEmpty(), failures.isEmpty() ? "" : qPrintable(failures.first().at(1).toString()));
    QCOMPARE(controller.currentRepository()->path, QFileInfo(source).canonicalFilePath());
    const auto clone = root.filePath(QStringLiteral("clone"));
    QCOMPARE(controller.resolvedAccountId(clone), work.id);
    QCOMPARE(relay::appStateFromJson(relay::RelayStore(config.storeFile).read()).repositoryAccounts.value(QFileInfo(clone).canonicalFilePath()), work.id);
  }

  void repositoryAccountAliasesResolveAndResetWithoutChangingUnknownPaths() {
#ifndef Q_OS_UNIX
    QSKIP("Directory symlink fixture requires Unix; Windows QFile::link creates shortcuts.");
#else
    QTemporaryDir root;
    const auto repository = root.filePath(QStringLiteral("repository"));
    const auto alias = root.filePath(QStringLiteral("alias"));
    QVERIFY(QDir().mkpath(repository));
    QVERIFY(QFile::link(repository, alias));
    const auto canonical = QFileInfo(repository).canonicalFilePath();
    const auto missing = root.filePath(QStringLiteral("unavailable/repository"));
    relay::Account personal;
    personal.id = QStringLiteral("github-1"); personal.githubId = 1;
    personal.handle = QStringLiteral("personal"); personal.authSource = QStringLiteral("github-cli");
    auto work = personal;
    work.id = QStringLiteral("github-2"); work.githubId = 2; work.handle = QStringLiteral("work");
    relay::AppState state;
    state.accounts = {personal, work}; state.activeAccountId = personal.id;
    state.repositoryAccounts.insert(alias, work.id);
    state.repositoryAccounts.insert(missing, work.id);
    const auto config = controllerConfig(root);
    seedState(config, state);
    relay::RelayController controller(config);
    controller.start();
    QCOMPARE(controller.resolvedAccountId(canonical), work.id);
    QCOMPARE(controller.resolvedAccountId(alias), work.id);
    // Reading legacy state does not rewrite it.
    QCOMPARE(controller.state().repositoryAccounts.value(alias), work.id);
    controller.setRepositoryAccount(canonical, {});
    QCOMPARE(controller.resolvedAccountId(alias), personal.id);
    QVERIFY(!controller.state().repositoryAccounts.contains(alias));
    QCOMPARE(controller.state().repositoryAccounts.value(missing), work.id);
    controller.setRepositoryAccount(alias, work.id);
    QCOMPARE(controller.state().repositoryAccounts.value(canonical), work.id);
    QVERIFY(!controller.state().repositoryAccounts.contains(alias));
    relay::RelayController restarted(config);
    restarted.start();
    QCOMPARE(restarted.resolvedAccountId(alias), work.id);
    restarted.setRepositoryAccount(alias, {});
    QCOMPARE(restarted.resolvedAccountId(canonical), personal.id);
    const auto stored = relay::appStateFromJson(relay::RelayStore(config.storeFile).read());
    QCOMPARE(stored.repositoryAccounts.size(), 1);
    QCOMPARE(stored.repositoryAccounts.value(missing), work.id);
#endif
  }

  void repositorySshAliasesResolveAndResetWithoutChangingUnknownPaths() {
#ifndef Q_OS_UNIX
    QSKIP("Directory symlink fixture requires Unix; Windows QFile::link creates shortcuts.");
#else
    QTemporaryDir root;
    const auto repository = root.filePath(QStringLiteral("repository"));
    const auto alias = root.filePath(QStringLiteral("alias"));
    QVERIFY(QDir().mkpath(repository));
    QVERIFY(QFile::link(repository, alias));
    const auto canonical = QFileInfo(repository).canonicalFilePath();
    const auto missing = root.filePath(QStringLiteral("unavailable/repository"));
    relay::SshProfile profile{QStringLiteral("ssh-work"), QStringLiteral("Work"),
        QStringLiteral("git.example.com"), {}, std::nullopt, {}, true};
    relay::AppState state;
    state.sshProfiles = {profile};
    state.repositorySshProfiles.insert(alias, profile.id);
    state.repositorySshProfiles.insert(missing, profile.id);
    const auto config = controllerConfig(root);
    seedState(config, state);
    relay::RelayController controller(config);
    controller.start();
    QCOMPARE(controller.resolvedSshProfileId(canonical), profile.id);
    QCOMPARE(controller.resolvedSshProfileId(alias), profile.id);
    // Reading legacy state does not rewrite it.
    QCOMPARE(controller.state().repositorySshProfiles.value(alias), profile.id);
    controller.setRepositorySshProfile(canonical, {});
    QCOMPARE(controller.resolvedSshProfileId(alias), QString{});
    QVERIFY(!controller.state().repositorySshProfiles.contains(alias));
    QCOMPARE(controller.state().repositorySshProfiles.value(missing), profile.id);
    controller.setRepositorySshProfile(alias, profile.id);
    QCOMPARE(controller.state().repositorySshProfiles.value(canonical), profile.id);
    QVERIFY(!controller.state().repositorySshProfiles.contains(alias));
    relay::RelayController restarted(config);
    restarted.start();
    QCOMPARE(restarted.resolvedSshProfileId(alias), profile.id);
    restarted.setRepositorySshProfile(alias, {});
    QCOMPARE(restarted.resolvedSshProfileId(canonical), QString{});
    const auto stored = relay::appStateFromJson(relay::RelayStore(config.storeFile).read());
    QCOMPARE(stored.repositorySshProfiles.size(), 1);
    QCOMPARE(stored.repositorySshProfiles.value(missing), profile.id);
#endif
  }

  void preferencesSurviveRestartAndValidateFontSize() {
    QTemporaryDir root;
    const auto config = controllerConfig(root);
    relay::RelayController first(config);
    first.start();
    first.setPreferences({false, 18});
    relay::RelayController second(config);
    second.start();
    QVERIFY(!second.state().preferences.refreshOnFocus);
    QCOMPARE(second.state().preferences.diffFontSize, 18);
    second.setPreferences({true, 999});
    QCOMPARE(second.state().preferences.diffFontSize, 24);
    QVERIFY(second.currentRepository() == nullptr);
  }

  void commitsUseTheBoundAccountThenFallBackToTheActiveAccount() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto path = root.filePath(QStringLiteral("repository"));
    QVERIFY(QDir().mkpath(path));
    initRepository(path);
    writeFile(QDir(path).filePath(QStringLiteral("first.txt")), QByteArrayLiteral("first\n"));
    writeFile(QDir(path).filePath(QStringLiteral("second.txt")), QByteArrayLiteral("second\n"));

    const auto config = controllerConfig(root);
    relay::Account personal;
    personal.id = QStringLiteral("github-1");
    personal.githubId = 1;
    personal.name = QStringLiteral("Personal Identity");
    personal.handle = QStringLiteral("personal");
    personal.email = QStringLiteral("personal@example.test");
    personal.authSource = QStringLiteral("github-cli");
    auto work = personal;
    work.id = QStringLiteral("github-2");
    work.githubId = 2;
    work.name = QStringLiteral("Work Identity");
    work.handle = QStringLiteral("work");
    work.email = QStringLiteral("work@example.test");
    relay::AppState state;
    state.accounts = {personal, work};
    state.activeAccountId = personal.id;
    state.repositories = {summary(path, QStringLiteral("repository"))};
    state.repositoryAccounts.insert(path, work.id);
    seedState(config, state);

    relay::RelayController controller(config);
    controller.start();
    QSignalSpy changed(&controller, &relay::RelayController::currentRepositoryChanged);
    QSignalSpy failures(&controller, &relay::RelayController::operationFailed);
    controller.openRepository(path);
    QVERIFY(changed.wait(5000));
    changed.clear();
    controller.commit({QStringLiteral("first.txt")}, QStringLiteral("Work commit"), {});
    QVERIFY(changed.wait(5000));
    QVERIFY(failures.isEmpty());
    relay::ProcessRequest log{QStringLiteral("git"),
        {QStringLiteral("-C"), path, QStringLiteral("log"), QStringLiteral("-1"),
         QStringLiteral("--format=%an|%ae|%cn|%ce")}};
    QCOMPARE(QString::fromUtf8(relay::ProcessRunner::run(log).standardOutput).trimmed(),
             QStringLiteral("Work Identity|work@example.test|Work Identity|work@example.test"));
    QCOMPARE(controller.currentRepository()->files.size(), 1);
    QCOMPARE(controller.currentRepository()->files.first().path, QStringLiteral("second.txt"));

    controller.setRepositoryAccount(path, {});
    changed.clear();
    controller.commit({QStringLiteral("second.txt")}, QStringLiteral("Personal commit"), {});
    QVERIFY(changed.wait(5000));
    QVERIFY(failures.isEmpty());
    QCOMPARE(QString::fromUtf8(relay::ProcessRunner::run(log).standardOutput).trimmed(),
             QStringLiteral("Personal Identity|personal@example.test|Personal Identity|personal@example.test"));
  }

  void supersededAccountRepositoryFailureIsIgnored() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    auto config = controllerConfig(root);
    relay::AppState state;
    relay::Account first;
    first.id = QStringLiteral("github-1");
    first.githubId = 1;
    first.handle = QStringLiteral("first");
    first.authSource = QStringLiteral("github-cli");
    auto second = first;
    second.id = QStringLiteral("github-2");
    second.githubId = 2;
    second.handle = QStringLiteral("second");
    state.accounts = {first, second};
    state.activeAccountId = first.id;
    seedState(config, state);

    QSemaphore started;
    QSemaphore release;
    config.operationGate = [&](const QString& operation, const QString& key) {
      if (operation != QStringLiteral("github-repositories")) return;
      if (key == first.handle) {
        started.release();
        release.acquire();
      }
      // No credentials or network access: fail before invoking gh.
      throw std::runtime_error(key.toStdString());
    };
    relay::RelayController controller(config);
    controller.start();
    QSignalSpy failures(&controller, &relay::RelayController::operationFailed);
    QSignalSpy busy(&controller, &relay::RelayController::busyChanged);
    controller.requestGitHubRepositories(first.id);
    const auto cleanup = qScopeGuard([&] { release.release(); });
    QVERIFY(started.tryAcquire(1, 5000));
    controller.requestGitHubRepositories(second.id);
    QTRY_COMPARE_WITH_TIMEOUT(failures.size(), 1, 5000);
    QCOMPARE(failures.first().at(1).toString(), second.handle);
    release.release();
    QTRY_COMPARE_WITH_TIMEOUT(busy.size(), 4, 5000);
    QCOMPARE(failures.size(), 1);
  }

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
