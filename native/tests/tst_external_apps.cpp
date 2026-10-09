#include "relay/external_apps.hpp"
#include "relay/process_runner.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class ExternalAppsTest final : public QObject {
  Q_OBJECT
 private slots:
  void resolvesTheConfiguredEditor() {
    using relay::ExternalApps;
    const QList<relay::ExternalEditor> editors{{QStringLiteral("vscode"), QStringLiteral("Visual Studio Code"), QStringLiteral("/a/Code")},
                                               {QStringLiteral("zed"), QStringLiteral("Zed"), QStringLiteral("/a/Zed")}};
    QCOMPARE(ExternalApps::resolveEditor(editors, {}, {}).id, QStringLiteral("vscode"));
    QCOMPARE(ExternalApps::resolveEditor(editors, QStringLiteral("zed"), {}).program, QStringLiteral("/a/Zed"));
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, static_cast<void>(ExternalApps::resolveEditor(editors, QStringLiteral("gone"), {})));
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, static_cast<void>(ExternalApps::resolveEditor({}, {}, {})));
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, static_cast<void>(ExternalApps::resolveEditor(editors, QStringLiteral("custom"), QStringLiteral("relative/editor"))));
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, static_cast<void>(ExternalApps::resolveEditor(editors, QStringLiteral("custom"), QStringLiteral("/does/not/exist"))));

    QTemporaryDir directory;
#if defined(Q_OS_WIN)
    const auto program = directory.filePath(QStringLiteral("my-editor.exe"));
#else
    const auto program = directory.filePath(QStringLiteral("my-editor"));
#endif
    QFile file(program);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("#!/bin/sh\n");
    file.close();
    QVERIFY(file.setPermissions(file.permissions() | QFileDevice::ExeOwner));
    const auto custom = ExternalApps::resolveEditor(editors, QStringLiteral("custom"), program);
    QCOMPARE(custom.program, program);
    const auto launch = ExternalApps::editorLaunch(custom, directory.path());
    QCOMPARE(launch.program, program);
    QCOMPARE(launch.arguments.size(), 1);
    QCOMPARE(launch.workingDirectory, directory.path());
  }

  void buildsPlatformLaunches() {
    using relay::ExternalApps;
    const auto directory = QStringLiteral("/Users/someone/My Repo; rm -rf ~");
    const auto terminal = ExternalApps::terminalLaunch(directory);
    const auto files = ExternalApps::fileManagerLaunch(directory);
    QCOMPARE(terminal.workingDirectory, directory);
    QCOMPARE(files.workingDirectory, directory);
#if defined(Q_OS_MACOS)
    QCOMPARE(terminal.program, QStringLiteral("/usr/bin/open"));
    QCOMPARE(terminal.arguments, (QStringList{QStringLiteral("-a"), QStringLiteral("Terminal"), directory}));
    QCOMPARE(files.arguments, QStringList{directory});
    const auto bundle = ExternalApps::editorLaunch({QStringLiteral("zed"), QStringLiteral("Zed"), QStringLiteral("/Applications/Zed.app")}, directory);
    QCOMPARE(bundle.arguments, (QStringList{QStringLiteral("-a"), QStringLiteral("/Applications/Zed.app"), directory}));
#endif
    // Nothing starts for a folder that does not exist.
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, ExternalApps::start(files));
    QVERIFY_THROWS_EXCEPTION(relay::ProcessError, ExternalApps::start({QStringLiteral("true"), {}, QStringLiteral("relative")}));
  }
};

QTEST_GUILESS_MAIN(ExternalAppsTest)
#include "tst_external_apps.moc"
