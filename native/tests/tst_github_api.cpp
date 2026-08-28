#include "relay/github_api.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

class GitHubApiTest final : public QObject {
  Q_OBJECT

 private slots:
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
