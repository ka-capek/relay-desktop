#include "relay/ssh_service.hpp"

#include <QTest>

class SshServiceTest final : public QObject {
  Q_OBJECT

 private slots:
  void transportUsesTheUserChosenInTheProfile() {
    relay::SshProfile profile;
    profile.host = QStringLiteral("example.test");
    profile.user = QStringLiteral("deploy");
    relay::SshService service;
    QVERIFY(service.commandFor(profile).contains(QStringLiteral("-l 'deploy'")));
  }

  void parsesRemoteForms() {
    const auto explicitRemote = relay::SshService::parseRemote(QStringLiteral("ssh://git@example.com:2222/team/repo.git"));
    QVERIFY(explicitRemote);
    QCOMPARE(explicitRemote->host, QStringLiteral("example.com"));
    QCOMPARE(explicitRemote->port, std::optional<quint16>(2222));
    const auto scp = relay::SshService::parseRemote(QStringLiteral("git@example.com:team/repo.git"));
    QVERIFY(scp);
    QCOMPARE(scp->path, QStringLiteral("team/repo.git"));
    QVERIFY(!relay::SshService::parseRemote(QStringLiteral("C:\\work\\repo")));
  }

  void keepsGithubSeparate() {
    QVERIFY(relay::SshService::isGitHubRemote(QStringLiteral("https://github.com/a/b.git")));
    QVERIFY(relay::SshService::isGitHubRemote(QStringLiteral("git@github.com:a/b.git")));
    QVERIFY(!relay::SshService::isGitHubRemote(QStringLiteral("git@gitlab.com:a/b.git")));
  }

  void quotesShellValues() {
    QCOMPARE(relay::SshService::shellQuote(QStringLiteral("a'b")), QStringLiteral("'a'\\''b'"));
  }

  void describesFailuresWithoutKeyPaths() {
    const auto result = relay::SshService::describeResult(QStringLiteral("git.example"), 255,
                                                           QStringLiteral("Permission denied (publickey)."));
    QVERIFY(!result.ok);
    QVERIFY(result.message.contains(QStringLiteral("git.example")));
    QVERIFY(!result.message.contains(QStringLiteral("id_ed25519")));
  }
};

QTEST_APPLESS_MAIN(SshServiceTest)
#include "tst_ssh_service.moc"
