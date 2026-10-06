#include "relay/credential_store.hpp"
#include "relay/forge_dialog.hpp"
#include "relay/forge_service.hpp"
#include "relay/relay_controller.hpp"
#include "relay/relay_store.hpp"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLineEdit>
#include <QListView>
#include <QMutex>
#include <QPushButton>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>
#include <atomic>
#include <stdexcept>

namespace {
class MemoryVault final : public relay::CredentialStore {
 public:
  void write(const QString& id, const QString& token) override { QMutexLocker lock(&mutex); values[id] = token; ++writes; }
  QString read(const QString& id) override { QMutexLocker lock(&mutex); return values.value(id); }
  void remove(const QString& id) override {
    if (failRemoval) throw std::runtime_error("fixture vault busy");
    QMutexLocker lock(&mutex); values.remove(id);
  }
  std::atomic<int> writes{};
  std::atomic<bool> failRemoval{};
  int count() { QMutexLocker lock(&mutex); return static_cast<int>(values.size()); }
 private:
  QMutex mutex;
  QHash<QString, QString> values;
};
std::shared_ptr<relay::ForgeService> fixtureService() {
  return std::make_shared<relay::ForgeService>([](const QUrl& url, const QString& token) {
    if (token != QStringLiteral("fixture-secret")) return relay::ForgeService::Response{401, {}, {}, {}};
    if (url.path().endsWith(QStringLiteral("/user")))
      return relay::ForgeService::Response{200, QJsonDocument(QJsonObject{
          {QStringLiteral("id"), 12}, {QStringLiteral("login"), QStringLiteral("karel")}}), {}, {}};
    QJsonArray entries;
    if (QUrlQuery(url).queryItemValue(QStringLiteral("page")) == QStringLiteral("1")) {
      entries.append(QJsonObject{{QStringLiteral("id"), 1}, {QStringLiteral("name"), QStringLiteral("private-repo")},
        {QStringLiteral("full_name"), QStringLiteral("team/private-repo")}, {QStringLiteral("private"), true},
        {QStringLiteral("ssh_url"), QStringLiteral("git@git.example.test:team/private-repo.git")},
        {QStringLiteral("html_url"), QStringLiteral("https://git.example.test/team/private-repo")}});
    }
    return relay::ForgeService::Response{200, QJsonDocument(entries), {}, {}};
  });
}
relay::RelayControllerConfig config(const QTemporaryDir& root, const std::shared_ptr<MemoryVault>& vault) {
  relay::RelayControllerConfig config;
  config.storeFile = root.filePath(QStringLiteral("profile/state.json"));
  config.synchronizeAccountsOnStart = false;
  config.forgeService = fixtureService();
  config.credentialStore = vault;
  return config;
}
}
class ForgeIntegrationTest final : public QObject {
  Q_OBJECT
 private slots:
  void connectBrowseRestartAndDisconnectKeepsTokensOutOfPublicState() {
    QTemporaryDir root;
    const auto vault = std::make_shared<MemoryVault>();
    const auto settings = config(root, vault);
    relay::RelayController controller(settings);
    controller.start();
    relay::ForgeDialog dialog(&controller);
    dialog.show();
    dialog.findChild<QLineEdit*>(QStringLiteral("forgeServer"))->setText(QStringLiteral("https://git.example.test"));
    auto* token = dialog.findChild<QLineEdit*>(QStringLiteral("forgeToken"));
    QCOMPARE(token->echoMode(), QLineEdit::Password);
    token->setText(QStringLiteral("fixture-secret"));
    QTest::mouseClick(dialog.findChild<QPushButton*>(QStringLiteral("forgeConnect")), Qt::LeftButton);
    QVERIFY(token->text().isEmpty());
    QTRY_COMPARE(controller.state().forgeAccounts.size(), 1);
    auto* repositories = dialog.findChild<QListView*>(QStringLiteral("forgeRepositories"));
    QTRY_COMPARE(repositories->model()->rowCount(), 1);
    QVERIFY(repositories->model()->index(0, 0).data().toString().contains(QStringLiteral("private-repo")));
    QFile state(settings.storeFile);
    QVERIFY(state.open(QIODevice::ReadOnly));
    const auto json = state.readAll();
    state.close(); // Windows readers otherwise prevent the later atomic store replacement.
    QVERIFY(!json.contains("fixture-secret"));
    QVERIFY(!json.contains("apiToken"));
    const auto account = controller.state().forgeAccounts.front();
    QCOMPARE(vault->read(account.credentialId), QStringLiteral("fixture-secret"));
    const auto screenshotDir = qEnvironmentVariable("RELAY_SCREENSHOT_DIR");
    if (!screenshotDir.isEmpty()) {
      QDir().mkpath(screenshotDir);
      QVERIFY(dialog.grab().save(screenshotDir + QStringLiteral("/forge-repositories.png")));
    }
    relay::RelayController restarted(settings);
    restarted.start();
    QCOMPARE(restarted.state().forgeAccounts.size(), 1);
    QSignalSpy loaded(&restarted, &relay::RelayController::forgeRepositoriesReady);
    restarted.requestForgeRepositories(account.id);
    QTRY_COMPARE(loaded.size(), 1);
    repositories->setCurrentIndex(repositories->model()->index(0, 0));
    auto* clone = dialog.findChild<QPushButton*>(QStringLiteral("forgeClone"));
    QVERIFY(clone->isEnabled());
    QTest::mouseClick(clone, Qt::LeftButton);
    QCOMPARE(dialog.cloneUrl(), QStringLiteral("git@git.example.test:team/private-repo.git"));
    restarted.removeForgeAccount(account.id);
    QVERIFY(restarted.state().forgeAccounts.isEmpty());
    QTRY_COMPARE(vault->count(), 0);
  }

  void failedMetadataSaveRollsBackOnlyNewCredential() {
    QTemporaryDir root;
    const auto vault = std::make_shared<MemoryVault>();
    const auto settings = config(root, vault);
    relay::RelayController controller(settings);
    controller.start();
    QSignalSpy connected(&controller, &relay::RelayController::forgeAccountConnected);
    controller.connectForgeAccount(relay::ForgeKind::gitea, QStringLiteral("https://git.example.test"), QStringLiteral("fixture-secret"));
    QTRY_COMPARE(connected.size(), 1);
    const auto original = controller.state().forgeAccounts.front();
    QVERIFY(QFile::remove(settings.storeFile));
    QVERIFY(QDir().mkdir(settings.storeFile));
    QSignalSpy errors(&controller, &relay::RelayController::operationFailed);
    controller.connectForgeAccount(relay::ForgeKind::gitea, QStringLiteral("https://git.example.test"), QStringLiteral("fixture-secret"));
    QTRY_VERIFY(!errors.isEmpty());
    QTRY_COMPARE(vault->count(), 1);
    QCOMPARE(controller.state().forgeAccounts.front().credentialId, original.credentialId);
    QCOMPARE(vault->read(original.credentialId), QStringLiteral("fixture-secret"));
    QCOMPARE(connected.size(), 1);
  }

  void disconnectCleanupCanBeRetriedAfterVaultFailure() {
    QTemporaryDir root;
    const auto vault = std::make_shared<MemoryVault>();
    const auto settings = config(root, vault);
    relay::RelayController controller(settings);
    controller.start();
    controller.connectForgeAccount(relay::ForgeKind::gitea, QStringLiteral("https://git.example.test"), QStringLiteral("fixture-secret"));
    QTRY_COMPARE(controller.state().forgeAccounts.size(), 1);
    const auto account = controller.state().forgeAccounts.front();
    vault->failRemoval = true;
    QSignalSpy errors(&controller, &relay::RelayController::operationFailed);
    controller.removeForgeAccount(account.id);
    QTRY_VERIFY(!errors.isEmpty());
    QVERIFY(controller.state().forgeAccounts.isEmpty());
    QCOMPARE(controller.state().forgeCredentialCleanup, QStringList{account.credentialId});
    QCOMPARE(vault->count(), 1);
    vault->failRemoval = false;
    controller.retryForgeCredentialCleanup();
    QTRY_VERIFY(controller.state().forgeCredentialCleanup.isEmpty());
    QCOMPARE(vault->count(), 0);
  }

  void closingControllerCleansUncommittedCredential() {
    QTemporaryDir root;
    const auto vault = std::make_shared<MemoryVault>();
    auto settings = config(root, vault);
    auto gate = std::make_shared<QSemaphore>();
    auto entered = std::make_shared<QSemaphore>();
    settings.forgeService = std::make_shared<relay::ForgeService>([gate, entered](const QUrl&, const QString&) {
      entered->release();
      gate->acquire();
      return relay::ForgeService::Response{200, QJsonDocument(QJsonObject{
          {QStringLiteral("id"), 12}, {QStringLiteral("login"), QStringLiteral("karel")}}), {}, {}};
    });
    auto controller = std::make_unique<relay::RelayController>(settings);
    controller->start();
    controller->connectForgeAccount(relay::ForgeKind::gitea, QStringLiteral("https://git.example.test"), QStringLiteral("fixture-secret"));
    QTRY_VERIFY(entered->available() > 0);
    controller.reset();
    gate->release();
    QTRY_COMPARE(vault->writes.load(), 1);
    QTRY_COMPARE(vault->count(), 0);
  }

  void staleRepositoryReplyIsSuppressed() {
    QTemporaryDir root;
    const auto vault = std::make_shared<MemoryVault>();
    auto settings = config(root, vault);
    auto gate = std::make_shared<QSemaphore>();
    auto entered = std::make_shared<QSemaphore>();
    settings.forgeService = std::make_shared<relay::ForgeService>([gate, entered](const QUrl& url, const QString&) {
      if (url.path().endsWith(QStringLiteral("/user")))
        return relay::ForgeService::Response{200, QJsonDocument(QJsonObject{
            {QStringLiteral("id"), 12}, {QStringLiteral("login"), QStringLiteral("karel")}}), {}, {}};
      entered->release();
      gate->acquire();
      return relay::ForgeService::Response{200, QJsonDocument(QJsonArray{}), {}, {}};
    });
    relay::RelayController controller(settings);
    controller.start();
    controller.connectForgeAccount(relay::ForgeKind::gitea, QStringLiteral("https://git.example.test"), QStringLiteral("fixture-secret"));
    QTRY_COMPARE(controller.state().forgeAccounts.size(), 1);
    QSignalSpy loaded(&controller, &relay::RelayController::forgeRepositoriesReady);
    controller.requestForgeRepositories(controller.state().forgeAccounts.front().id);
    QTRY_VERIFY(entered->available() > 0);
    controller.cancelForgeRepositoryRequest();
    gate->release();
    QSignalSpy busy(&controller, &relay::RelayController::busyChanged);
    QTRY_VERIFY(!busy.isEmpty());
    QCOMPARE(loaded.size(), 0);
  }
};
QTEST_MAIN(ForgeIntegrationTest)
#include "tst_forge_integration.moc"
