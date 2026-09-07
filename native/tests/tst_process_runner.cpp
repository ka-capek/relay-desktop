#include "relay/process_runner.hpp"

#include <QSignalSpy>
#include <QTest>

class ProcessRunnerTest final : public QObject {
  Q_OBJECT

 private slots:
  void providesInputAndClosesThePipe() {
    relay::ProcessRequest request{QStringLiteral("git"), {QStringLiteral("hash-object"), QStringLiteral("--stdin")}};
    request.standardInput = QByteArrayLiteral("hello\n");
    const auto result = relay::ProcessRunner::run(request);
    QCOMPARE(result.standardOutput.trimmed(), QByteArrayLiteral("ce013625030ba8dba906f756967f9e9ca394464a"));
  }

  void oversizedOutputFailsInsteadOfReturningTruncatedSuccess() {
#ifdef Q_OS_WIN
    relay::ProcessRequest request{QStringLiteral("cmd.exe"), {QStringLiteral("/C"), QStringLiteral("echo too-long")}};
#else
    relay::ProcessRequest request{QStringLiteral("/usr/bin/printf"), {QStringLiteral("too-long")}};
#endif
    request.maximumOutputBytes = 3;
    QVERIFY_EXCEPTION_THROWN(relay::ProcessRunner::run(request), relay::ProcessError);
  }

  void capturesOutput() {
#ifdef Q_OS_WIN
    const relay::ProcessRequest request{QStringLiteral("cmd.exe"), {QStringLiteral("/C"), QStringLiteral("echo relay")}};
#else
    const relay::ProcessRequest request{QStringLiteral("/usr/bin/printf"), {QStringLiteral("relay")}};
#endif
    const auto result = relay::ProcessRunner::run(request);
    QCOMPARE(QString::fromUtf8(result.standardOutput).trimmed(), QStringLiteral("relay"));
  }

  void cancelsAsynchronousProcesses() {
    relay::AsyncProcess process;
    QSignalSpy failed(&process, &relay::AsyncProcess::failed);
#ifdef Q_OS_WIN
    relay::ProcessRequest request{QStringLiteral("ping.exe"),
                                  {QStringLiteral("-n"), QStringLiteral("10"), QStringLiteral("127.0.0.1")}};
#else
    relay::ProcessRequest request{QStringLiteral("/bin/sleep"), {QStringLiteral("10")}};
#endif
    process.start(request);
    QTRY_VERIFY(process.isRunning());
    process.cancel();
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(failed.front().front().toString(), QStringLiteral("Operation canceled."));
  }
};

QTEST_APPLESS_MAIN(ProcessRunnerTest)
#include "tst_process_runner.moc"
