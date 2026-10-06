#include "relay/runtime_check.hpp"
#include "relay/process_runner.hpp"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>

class RuntimeCheckTest final : public QObject {
  Q_OBJECT
 private slots:
  void versions_data() {
    QTest::addColumn<bool>("git");
    QTest::addColumn<QByteArray>("output");
    QTest::addColumn<bool>("ready");
    QTest::newRow("git-minimum") << true << QByteArray("git version 2.35.0") << true;
    QTest::newRow("windows-git") << true << QByteArray("git version 2.52.0.windows.1") << true;
    QTest::newRow("old-git") << true << QByteArray("git version 2.34.9") << false;
    QTest::newRow("gh-minimum") << false << QByteArray("gh version 2.98.0 (2026-01-01)") << true;
    QTest::newRow("old-gh") << false << QByteArray("gh version 2.97.9") << false;
    QTest::newRow("wrong-tool") << false << QByteArray("git version 3.0.0") << false;
    QTest::newRow("malformed") << true << QByteArray("untrusted output") << false;
  }

  void versions() {
    QFETCH(bool, git);
    QFETCH(QByteArray, output);
    QFETCH(bool, ready);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("RELAY_TEST_VERSION"), QString::fromUtf8(output));
    const auto issue = relay::checkRuntime(git ? relay::RuntimeTool::git : relay::RuntimeTool::githubCli,
                                           QCoreApplication::applicationFilePath(), environment);
    QCOMPARE(issue.isEmpty(), ready);
    QVERIFY(!issue.contains(QStringLiteral("untrusted output")));
  }

  void missingToolIsActionableAndDoesNotLeakProcessOutput() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto missing = directory.filePath(QStringLiteral("gh.exe"));
    const auto message = relay::checkRuntime(relay::RuntimeTool::githubCli, missing,
                                             QProcessEnvironment::systemEnvironment());
    QVERIFY(message.contains(QStringLiteral("GitHub CLI")));
    QVERIFY(message.contains(QStringLiteral("Install")));
    QVERIFY(!message.contains(directory.path()));
    try {
      relay::ProcessRunner::run({missing, {QStringLiteral("auth"), QStringLiteral("status")}});
      QFAIL("Missing gh must fail");
    } catch (const relay::ProcessError& error) {
      QVERIFY(error.qMessage().contains(QStringLiteral("Install")));
      QVERIFY(!error.qMessage().contains(directory.path()));
    }
  }

  void checksRecoverAfterAnUpgrade() {
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("RELAY_TEST_VERSION"), QStringLiteral("git version 1.0.0"));
    QVERIFY(!relay::checkRuntime(relay::RuntimeTool::git, QCoreApplication::applicationFilePath(), environment).isEmpty());
    environment.insert(QStringLiteral("RELAY_TEST_VERSION"), QStringLiteral("git version 2.53.0"));
    QVERIFY(relay::checkRuntime(relay::RuntimeTool::git, QCoreApplication::applicationFilePath(), environment).isEmpty());
  }
};

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().contains(QStringLiteral("--version"))) {
    const auto output = qgetenv("RELAY_TEST_VERSION");
    std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
    return 0;
  }
  RuntimeCheckTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "tst_runtime_check.moc"
