#include "relay/github_api.hpp"

#include <QJsonArray>
#include <QJsonDocument>
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

  void recognizesOnlyGitHubRemotes() {
    using relay::GitHubApi;
    for (const auto& remote : {QStringLiteral("https://github.com/octo/relay.git"), QStringLiteral("git@github.com:octo/relay.git"),
                               QStringLiteral("ssh://git@github.com/octo/relay"), QStringLiteral("https://GitHub.com/octo/relay/")}) {
      const auto parsed = GitHubApi::repositoryFromRemote(remote);
      QVERIFY2(parsed.has_value(), qPrintable(remote));
      QCOMPARE(parsed->owner.toLower(), QStringLiteral("octo"));
      QCOMPARE(parsed->name, QStringLiteral("relay"));
    }
    for (const auto& remote : {QStringLiteral("https://notgithub.com/octo/relay.git"), QStringLiteral("https://github.com.evil/octo/relay"),
                               QStringLiteral("https://user:secret@github.com/octo/relay"), QStringLiteral("http://github.com/octo/relay"),
                               QStringLiteral("https://github.com/octo/relay/extra"), QStringLiteral("https://github.com/octo/.."),
                               QStringLiteral("https://gitlab.com/octo/relay"), QString{}})
      QVERIFY2(!GitHubApi::repositoryFromRemote(remote).has_value(), qPrintable(remote));
  }

  void buildsAndValidatesPullRequestUrls() {
    using relay::GitHubApi;
    const relay::GitHubRepositoryName repository{QStringLiteral("octo"), QStringLiteral("relay")};
    QCOMPARE(GitHubApi::pullRequestCreationUrl(repository, QStringLiteral("feature/a#b c")),
             QStringLiteral("https://github.com/octo/relay/compare/feature/a%23b%20c?expand=1"));
    const auto list = [](const QString& url) {
      return QJsonDocument(QJsonArray{QJsonObject{{QStringLiteral("html_url"), url}}});
    };
    QCOMPARE(GitHubApi::pullRequestUrlFromJson(list(QStringLiteral("https://github.com/octo/relay/pull/42")), repository),
             QStringLiteral("https://github.com/octo/relay/pull/42"));
    QVERIFY(GitHubApi::pullRequestUrlFromJson(QJsonDocument(QJsonArray{}), repository).isEmpty());
    QVERIFY(GitHubApi::pullRequestUrlFromJson(list(QStringLiteral("https://github.com/other/relay/pull/42")), repository).isEmpty());
    QVERIFY(GitHubApi::pullRequestUrlFromJson(list(QStringLiteral("https://github.com/octo/relay/pull/42/../x")), repository).isEmpty());
    QVERIFY(GitHubApi::pullRequestUrlFromJson(list(QStringLiteral("javascript:alert(1)")), repository).isEmpty());
  }
};

QTEST_APPLESS_MAIN(GitHubApiTest)
#include "tst_github_api.moc"
