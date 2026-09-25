#include "relay/forge_service.hpp"
#include "relay/credential_store.hpp"
#include <QJsonArray>
#include <QTest>
#include <QUrlQuery>
#include <QScopeGuard>
#include <QUuid>
#include <stdexcept>

using namespace relay;
class ForgeServiceTest final : public QObject {
  Q_OBJECT
 private slots:
  void usesProviderSpecificPersonalAccessTokenScheme() {
    QCOMPARE(ForgeService::authorizationHeader(ForgeKind::gitea, QStringLiteral("fixture-token")), QByteArrayLiteral("token fixture-token"));
    QCOMPARE(ForgeService::authorizationHeader(ForgeKind::gitlab, QStringLiteral("fixture-token")), QByteArrayLiteral("Bearer fixture-token"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, ForgeService::authorizationHeader(ForgeKind::gitea, QStringLiteral("token\r\nInjected: x")));
  }
  void validatesServerAndTokenBeforeNetwork() {
    QCOMPARE(ForgeService::normalizeServerUrl(QStringLiteral("https://GIT.example.com:443/gitea/")), QStringLiteral("https://git.example.com/gitea"));
    for (const auto& url : {"http://git.example.com", "https://token@git.example.com", "https://git.example.com/?a=1", "https://git.example.com/#x"})
      QVERIFY_THROWS_EXCEPTION(std::runtime_error, ForgeService::normalizeServerUrl(QString::fromLatin1(url)));
    bool called{};
    ForgeService service([&](const QUrl&, const QString&) { called = true; return ForgeService::Response{}; });
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, service.profile(ForgeKind::gitea, QStringLiteral("https://git.example.com"), QStringLiteral("token\r\nInjected: x")));
    QVERIFY(!called);
  }
  void profileIdentityIsIsolatedByHostAndProvider() {
    ForgeService service([](const QUrl&, const QString&) {
      return ForgeService::Response{200, QJsonDocument(QJsonObject{{QStringLiteral("id"), 42}, {QStringLiteral("login"), QStringLiteral("alice")}, {QStringLiteral("username"), QStringLiteral("alice")}, {QStringLiteral("name"), QStringLiteral("Alice")}}), {}, {}};
    });
    const auto one = service.profile(ForgeKind::gitea, QStringLiteral("https://git.example.com/"), QStringLiteral("secret"));
    QCOMPARE(one.id, service.profile(ForgeKind::gitea, QStringLiteral("https://git.example.com"), QStringLiteral("secret")).id);
    QVERIFY(one.id != service.profile(ForgeKind::gitea, QStringLiteral("https://other.example.com"), QStringLiteral("secret")).id);
    QVERIFY(one.id != service.profile(ForgeKind::gitlab, QStringLiteral("https://git.example.com"), QStringLiteral("secret")).id);
    const auto json = forgeAccountToJson(one);
    QVERIFY(!QJsonDocument(json).toJson().contains("secret"));
    QCOMPARE(forgeAccountFromJson(json).id, one.id);
    auto bound = json;
    bound.insert(QStringLiteral("credentialId"), one.id + QStringLiteral(":storehash:fixture-id"));
    QCOMPARE(forgeAccountFromJson(bound).credentialId, one.id + QStringLiteral(":storehash:fixture-id"));
    auto changedServer = bound;
    changedServer.insert(QStringLiteral("serverUrl"), QStringLiteral("https://attacker.example.com"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, forgeAccountFromJson(changedServer));
    // Updating the public account ID to match a new host still cannot reuse
    // the original account's vault reference.
    const auto other = service.profile(ForgeKind::gitea, QStringLiteral("https://attacker.example.com"), QStringLiteral("secret"));
    changedServer.insert(QStringLiteral("id"), other.id);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, forgeAccountFromJson(changedServer));
    auto changedUser = bound;
    changedUser.insert(QStringLiteral("userId"), QStringLiteral("99"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, forgeAccountFromJson(changedUser));
    auto changedKind = bound;
    changedKind.insert(QStringLiteral("kind"), QStringLiteral("gitlab"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, forgeAccountFromJson(changedKind));
    auto invalid = json; invalid.insert(QStringLiteral("credentialId"), QStringLiteral("../secret"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, forgeAccountFromJson(invalid));
  }
  void listsAllPagesEvenWhenServerCapsPageSizeAndRetainsReadOnlyArchived() {
    int calls{};
    ForgeService service([&](const QUrl& url, const QString&) {
      ++calls;
      if (url.host() != QStringLiteral("git.example.com")) throw std::runtime_error("unexpected request origin");
      const auto page = QUrlQuery(url).queryItemValue(QStringLiteral("page")).toInt();
      QJsonArray entries;
      if (page <= 3) entries.append(QJsonObject{{QStringLiteral("id"), page}, {QStringLiteral("name"), QStringLiteral("repo")}, {QStringLiteral("full_name"), QStringLiteral("group/repo%1").arg(page)},
          {QStringLiteral("private"), true}, {QStringLiteral("archived"), true}, {QStringLiteral("permissions"), QJsonObject{{QStringLiteral("push"), false}}}, {QStringLiteral("clone_url"), QStringLiteral("https://git.example.com/group/repo.git")}});
      return ForgeService::Response{200, QJsonDocument(entries), {}, "<https://evil.example.com/steal>; rel=\"next\""};
    });
    ForgeAccount account; account.serverUrl = QStringLiteral("https://git.example.com");
    const auto repos = service.repositories(account, QStringLiteral("secret"));
    QCOMPARE(calls, 4);
    QCOMPARE(repos.size(), 3);
    QVERIFY(repos.front().isPrivate);
    QVERIFY(repos.front().archived);
  }
  void gitlabMembershipUsesItsOwnFields() {
    QStringList paths;
    ForgeService service([&](const QUrl& url, const QString&) {
      paths.append(url.path());
      const auto query = QUrlQuery(url);
      QJsonArray entries;
      if (query.queryItemValue(QStringLiteral("page")) == QStringLiteral("1")) entries.append(QJsonObject{
        {QStringLiteral("id"), 7}, {QStringLiteral("name"), QStringLiteral("Project")}, {QStringLiteral("path_with_namespace"), QStringLiteral("team/project")}, {QStringLiteral("visibility"), QStringLiteral("private")},
        {QStringLiteral("ssh_url_to_repo"), QStringLiteral("git@git.example.com:team/project.git")}, {QStringLiteral("http_url_to_repo"), QStringLiteral("https://git.example.com/team/project.git")}});
      if (query.queryItemValue(QStringLiteral("membership")) != QStringLiteral("true")) throw std::runtime_error("missing membership");
      return ForgeService::Response{200, QJsonDocument(entries), {}, {}};
    });
    ForgeAccount account; account.kind = ForgeKind::gitlab; account.serverUrl = QStringLiteral("https://git.example.com/subpath");
    const auto repos = service.repositories(account, QStringLiteral("secret"));
    QCOMPARE(paths.front(), QStringLiteral("/subpath/api/v4/projects"));
    QCOMPARE(repos.front().fullName, QStringLiteral("team/project"));
    QCOMPARE(repos.front().sshUrl, QStringLiteral("git@git.example.com:team/project.git"));
    QVERIFY(repos.front().isPrivate);
  }
  void filtersUnsafeCloneUrlsButAllowsSeparateSshHost() {
    ForgeService service([](const QUrl& url, const QString&) {
      QJsonArray entries;
      if (QUrlQuery(url).queryItemValue(QStringLiteral("page")) == QStringLiteral("1")) {
        int id = 0;
        for (const auto& remote : QStringList{QStringLiteral("ext::evil"), QStringLiteral("/local/repo"),
            QStringLiteral("ssh://git:password@host/repo"), QStringLiteral("git@host:repo\ncommand"),
            QStringLiteral("ssh://git@ssh.example.com:2222/team/repo.git")}) {
          entries.append(QJsonObject{{QStringLiteral("id"), ++id}, {QStringLiteral("full_name"), QStringLiteral("team/repo%1").arg(id)},
            {QStringLiteral("ssh_url"), remote}, {QStringLiteral("clone_url"), QStringLiteral("https://token@host/repo")}});
        }
      }
      return ForgeService::Response{200, QJsonDocument(entries), {}, {}};
    });
    ForgeAccount account; account.serverUrl = QStringLiteral("https://git.example.com");
    const auto repos = service.repositories(account, QStringLiteral("secret"));
    QCOMPARE(repos.size(), 5);
    for (int i = 0; i < 4; ++i) QVERIFY(repos.at(i).sshUrl.isEmpty());
    QCOMPARE(repos.back().sshUrl, QStringLiteral("ssh://git@ssh.example.com:2222/team/repo.git"));
    for (const auto& repo : repos) QVERIFY(repo.httpsUrl.isEmpty());
  }
  void rejectsRepeatedPagesAndDoesNotLeakServerErrors() {
    ForgeAccount account; account.serverUrl = QStringLiteral("https://git.example.com");
    ForgeService repeat([](const QUrl&, const QString&) {
      return ForgeService::Response{200, QJsonDocument(QJsonArray{QJsonObject{{QStringLiteral("id"), 1}, {QStringLiteral("full_name"), QStringLiteral("a/b")}}}), {}, {}};
    });
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, repeat.repositories(account, QStringLiteral("secret")));
    ForgeService denied([](const QUrl&, const QString&) {
      return ForgeService::Response{403, QJsonDocument(QJsonObject{{QStringLiteral("message"), QStringLiteral("secret")}}), {}, {}};
    });
    try { denied.repositories(account, QStringLiteral("secret")); QFAIL("must reject"); }
    catch (const std::exception& error) { QVERIFY(!QString::fromUtf8(error.what()).contains(QStringLiteral("secret"))); }
  }
  void operatingSystemVaultRoundTrip() {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    // A fresh opaque name confines every operation to this disposable fixture.
    // Never enumerate existing credentials or read a user account's token.
    const auto id = QStringLiteral("test-forge-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    CredentialStore vault;
    const auto cleanup = qScopeGuard([&] {
      try { vault.remove(id); } catch (...) { /* Preserve the original test failure. */ }
    });
    QVERIFY(vault.read(id).isEmpty());
    const auto original = QStringLiteral("fixture-only-token-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto updated = QStringLiteral("updated-fixture-only-token-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    vault.write(id, original);
    QCOMPARE(vault.read(id), original);
    vault.write(id, updated);
    QCOMPARE(vault.read(id), updated);
    vault.remove(id);
    QVERIFY(vault.read(id).isEmpty());
    // Removal is idempotent, including after an interrupted disconnect cleanup.
    vault.remove(id);
#else
    QSKIP("The operating system vault is implemented for the macOS and Windows production targets.");
#endif
  }
  void vaultRejectsUnsafeReferenceBeforeAccess() {
    CredentialStore vault;
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, vault.read(QStringLiteral("../foreign")));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, vault.remove(QStringLiteral("")));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, vault.write(QStringLiteral("safe"), {}));
  }
};
QTEST_APPLESS_MAIN(ForgeServiceTest)
#include "tst_forge_service.moc"
