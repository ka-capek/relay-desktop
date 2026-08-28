#include "relay/main_window.hpp"
#include "relay/relay_application.hpp"
#include "relay/relay_controller.hpp"
#include "relay/theme.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QListView>
#include <QProcess>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

namespace {

void runGit(const QString& repository, const QStringList& arguments) {
  QProcess process;
  process.setWorkingDirectory(repository);
  process.start(QStringLiteral("git"), arguments);
  QVERIFY2(process.waitForFinished(10'000), "git command timed out");
  QVERIFY2(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
           process.readAllStandardError().constData());
}

void createRepository(const QString& path) {
  runGit(path, {QStringLiteral("init"), QStringLiteral("--initial-branch=main")});
  runGit(path, {QStringLiteral("config"), QStringLiteral("user.name"),
                QStringLiteral("Relay Test")});
  runGit(path, {QStringLiteral("config"), QStringLiteral("user.email"),
                QStringLiteral("relay@example.test")});
  QFile readme(path + QStringLiteral("/README.md"));
  QVERIFY(readme.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE(readme.write("# Test\n"), 7);
  readme.close();
  runGit(path, {QStringLiteral("add"), QStringLiteral("README.md")});
  runGit(path, {QStringLiteral("commit"), QStringLiteral("-m"),
                QStringLiteral("Initial commit")});
}

}  // namespace

class MainWindowTest final : public QObject {
  Q_OBJECT

 private slots:
  void shellOpensARepositoryThroughTheRealController() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString repositoryPath = temporary.path() + QStringLiteral("/sample");
    QVERIFY(QDir{}.mkpath(repositoryPath));
    createRepository(repositoryPath);

    relay::RelayControllerConfig config;
    config.storeFile = temporary.path() + QStringLiteral("/relay-data.json");
    config.applicationDirectory = QCoreApplication::applicationDirPath();
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.show();

    QCOMPARE(window.minimumWidth(), 820);
    QCOMPARE(window.minimumHeight(), 600);
    auto* repositories = window.findChild<QListView*>(QStringLiteral("repositoryList"));
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("contentTabs"));
    QVERIFY(repositories != nullptr);
    QVERIFY(tabs != nullptr);

    controller.start();
    QCOMPARE(repositories->model()->rowCount(), 0);
    QVERIFY(!tabs->isVisible());
    controller.openRepository(repositoryPath);
    QTRY_VERIFY_WITH_TIMEOUT(controller.currentRepository() != nullptr, 10'000);
    QTRY_COMPARE_WITH_TIMEOUT(repositories->model()->rowCount(), 1, 10'000);
    QTRY_VERIFY_WITH_TIMEOUT(tabs->isVisible(), 10'000);
    QCOMPARE(controller.currentRepository()->branch, QStringLiteral("main"));
    QCOMPARE(controller.currentRepository()->name, QStringLiteral("sample"));
  }
};

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  relay::RelayApplication application(argc, argv);
  relay::theme::apply(application);
  MainWindowTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "tst_main_window.moc"
