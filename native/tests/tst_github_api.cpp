#include "relay/github_api.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

class GitHubApiTest final : public QObject {
  Q_OBJECT

 private slots:
  void publicationPayloadIsExplicitAndValidated() {
    const auto payload = relay::GitHubApi::newRepositoryPayload(QStringLiteral("example"), QStringLiteral("Description"), true);
    QVERIFY(payload.value(QStringLiteral("private")).toBool());
    QVERIFY(!payload.value(QStringLiteral("auto_init")).toBool());
    QCOMPARE(payload.value(QStringLiteral("name")).toString(), QStringLiteral("example"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, relay::GitHubApi::newRepositoryPayload(QStringLiteral("../wrong"), {}, false));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, relay::GitHubApi::newRepositoryPayload(QStringLiteral("valid"), QString(351, u'x'), false));
  }

  void repositoryCreationFailuresUseRelayMessages() {
    const auto handle = QStringLiteral("octo");
    const auto name = QStringLiteral("example");
    for (const auto status : {500, 502, 503}) {
      const auto message = relay::GitHubApi::repositoryCreationError(status, handle, name);
      QVERIFY(message.contains(QStringLiteral("Creation may have succeeded")));
      QVERIFY(message.contains(QStringLiteral("https://github.com/octo/example")));
    }
    QCOMPARE(relay::GitHubApi::repositoryCreationError(401, handle, name), QStringLiteral("Sign in again before publishing."));
    QVERIFY(relay::GitHubApi::repositoryCreationError(422, handle, name).contains(QStringLiteral("already exist")));
    QVERIFY(relay::GitHubApi::repositoryCreationError(404, handle, name).contains(QStringLiteral("(404)")));
    QVERIFY(!relay::GitHubApi::repositoryCreationError(404, handle, name).contains(QStringLiteral("may have succeeded")));
  }

  void alwaysOffersNoreplyAndVerifiedEmails() {
    relay::Account account;
    account.githubId = 42;
    account.handle = QStringLiteral("octo");
    const QJsonArray values{
        QJsonObject{{QStringLiteral("email"), QStringLiteral("primary@example.com")},
                    {QStringLiteral("verified"), true}, {QStringLiteral("primary"), true}},
        QJsonObject{{QStringLiteral("email"), QStringLiteral("no@example.com")},
                    {QStringLiteral("verified"), false}},
    };
    const auto choices = relay::GitHubApi::emailChoicesFromJson(account, values);
    QCOMPARE(choices.size(), 2);
    QCOMPARE(choices.front().email, QStringLiteral("42+octo@users.noreply.github.com"));
    QVERIFY(choices.back().primary);
  }

  void validatesAndResolvesEmail() {
    relay::Account account;
    account.githubId = 42;
    account.handle = QStringLiteral("octo");
    QVERIFY(relay::GitHubApi::isValidEmail(QStringLiteral("a@example.com")));
    QCOMPARE(relay::GitHubApi::resolveCommitEmail(account, {}),
             QStringLiteral("42+octo@users.noreply.github.com"));
  }
};

QTEST_APPLESS_MAIN(GitHubApiTest)
#include "tst_github_api.moc"
