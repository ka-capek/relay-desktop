#include "relay/github_auth.hpp"

#include <QTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <stdexcept>

class GitHubAuthTest final : public QObject {
  Q_OBJECT

 private slots:
  void bundledCliIsResolvedWithoutAnExternalPackagingFlag() {
    QTemporaryDir root;
    const auto resources = root.filePath(QStringLiteral("resources"));
    QVERIFY(QDir().mkpath(resources + QStringLiteral("/gh")));
#ifdef Q_OS_WIN
    const auto executable = resources + QStringLiteral("/gh/gh.exe");
#else
    const auto executable = resources + QStringLiteral("/gh/gh");
#endif
    QFile file(executable);
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture"); file.close();
    relay::GitHubAuth auth({root.path(), resources, root.filePath(QStringLiteral("data")), false});
    QCOMPARE(auth.executable(), executable);
  }

  void invalidAndExpiredAccountListsCannotBecomeLogout() {
    QVERIFY_EXCEPTION_THROWN(relay::GitHubAuth::accountsFromStatus(QByteArrayLiteral("not json")), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(relay::GitHubAuth::accountsFromStatus(QByteArrayLiteral("{}")), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(relay::GitHubAuth::accountsFromStatus(QByteArrayLiteral("{\"hosts\":{\"github.com\":[{\"login\":\"expired\",\"state\":\"failure\"}]}}")), std::runtime_error);
    QVERIFY(relay::GitHubAuth::accountsFromStatus(QByteArrayLiteral("{\"hosts\":{}}")).isEmpty());
  }

  void parsesSingleAndMultipleAccounts() {
    const auto single = relay::GitHubAuth::accountsFromStatus(R"({"hosts":{"github.com":{"login":"one","active":true}}})");
    QCOMPARE(single.size(), 1);
    QCOMPARE(single.front().handle, QStringLiteral("one"));
    QVERIFY(single.front().active);

    const auto multiple = relay::GitHubAuth::accountsFromStatus(R"({"hosts":{"github.com":[{"login":"one","active":false},{"login":"two","active":true}]}})");
    QCOMPARE(multiple.size(), 2);
    QCOMPARE(multiple.back().handle, QStringLiteral("two"));
  }

  void findsFragmentedLoginCode() {
    const auto progress = relay::GitHubAuth::loginProgressFromOutput(
        QStringLiteral("Copy code ABCD-1234"), QStringLiteral("1234"));
    QCOMPARE(progress.code, QStringLiteral("ABCD-1234"));
    QCOMPARE(progress.verificationUrl, QStringLiteral("https://github.com/login/device"));
  }

  void scrubsInheritedTokens() {
    qputenv("GH_TOKEN", QByteArrayLiteral("secret"));
    qputenv("GITHUB_TOKEN", QByteArrayLiteral("secret"));
    relay::GitHubAuth auth({QStringLiteral("/source"), QStringLiteral("/resources"), QStringLiteral("/data"), false});
    const auto environment = auth.environment();
    QVERIFY(!environment.contains(QStringLiteral("GH_TOKEN")));
    QVERIFY(!environment.contains(QStringLiteral("GITHUB_TOKEN")));
    QCOMPARE(environment.value(QStringLiteral("GH_CONFIG_DIR")), QStringLiteral("/data/github-cli"));
  }
};

QTEST_APPLESS_MAIN(GitHubAuthTest)
#include "tst_github_auth.moc"
