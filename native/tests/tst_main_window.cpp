#include "relay/main_window.hpp"
#include "relay/dialogs.hpp"
#include "relay/relay_application.hpp"
#include "relay/relay_controller.hpp"
#include "relay/relay_store.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include "relay/theme.hpp"
#include "relay/diff_view.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QSignalSpy>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include "relay/list_models.hpp"
#include <QAction>
#include <QClipboard>
#include <QLineEdit>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFontMetrics>
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
  void themedDiffAndSettingsScreenshots() {
    for (const auto& id : relay::theme::presetIds()) {
      relay::theme::configure(id);
      relay::theme::apply(*qApp);
      QVERIFY(!QPixmap(QStringLiteral(":/relay/chevron-light.svg")).isNull());
      QVERIFY(!QPixmap(QStringLiteral(":/relay/chevron-dark.svg")).isNull());
      relay::DiffView view;
      view.resize(850, 320);
      view.setDiff(QStringLiteral("@@ -1,3 +1,3 @@\n context\n-old value\n+new value\n context\n"));
      view.show();
      QTest::qWait(20);
      relay::Preferences preferences;
      preferences.themeId = id;
      relay::SettingsDialog dialog(preferences);
      auto* tabs = dialog.findChild<QTabWidget*>();
      dialog.show();
      QTest::qWait(20);
      if (const auto output = qEnvironmentVariable("RELAY_SCREENSHOT_DIR"); !output.isEmpty()) {
        QDir().mkpath(output);
        QVERIFY(dialog.grab().save(QDir(output).filePath(QStringLiteral("general-%1.png").arg(id))));
      }
      tabs->setCurrentIndex(1);
      QTest::qWait(20);
      if (const auto output = qEnvironmentVariable("RELAY_SCREENSHOT_DIR"); !output.isEmpty()) {
        QDir().mkpath(output);
        QVERIFY(view.grab().save(QDir(output).filePath(QStringLiteral("diff-%1.png").arg(id))));
        QVERIFY(dialog.grab().save(QDir(output).filePath(QStringLiteral("appearance-%1.png").arg(id))));
      }
    }
    relay::theme::configure(QStringLiteral("light"));
    relay::theme::apply(*qApp);
  }

  void largeDiffFontKeepsSixDigitLineNumbersVisible() {
    relay::DiffView view;
    view.setDiff(QStringLiteral("@@ -100000 +100000 @@\n-old\n+new\n"));
    view.setCodeFontSize(24);
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(24);
    const int required = QFontMetrics(font).horizontalAdvance(QStringLiteral("100000")) + 12;
    QVERIFY(view.columnWidth(relay::DiffModel::oldLineColumn) >= required);
    QVERIFY(view.columnWidth(relay::DiffModel::newLineColumn) >= required);
  }

  void imagePreviewCanReturnToTextWithoutStaleContent() {
    relay::DiffView view;
    view.resize(900, 450);
    relay::FilePreview preview;
    preview.before = QImage(100, 80, QImage::Format_RGB32);
    preview.before.fill(Qt::red);
    preview.after = QImage(160, 120, QImage::Format_RGB32);
    preview.after.fill(Qt::blue);
    view.setPreview(preview);
    view.show();
    QTest::qWait(30);
    QCOMPARE(view.diffModel()->rowCount(), 0);
    const auto directory = qEnvironmentVariable("RELAY_SCREENSHOT_DIR");
    if (!directory.isEmpty()) {
      QVERIFY(QDir().mkpath(directory));
      QVERIFY(view.grab().save(directory + QStringLiteral("/image-preview.png")));
    }
    view.setPreview({QStringLiteral("@@ -1 +1 @@\n-before\n+after\n"), {}, {}});
    QCOMPARE(view.diffModel()->rowCount(), 3);
    QCOMPARE(view.diffModel()->lineAt(2).text, QStringLiteral("+after"));
  }

  void conflictDialogShowsWorkerErrorsAndAbortRestoresRepository() {
    QTemporaryDir root;
    QTemporaryDir store;
    createRepository(root.path());
    const auto change = [&root](const QByteArray& content) {
      QFile file(root.path() + QStringLiteral("/README.md"));
      QVERIFY(file.open(QIODevice::WriteOnly)); file.write(content); file.close();
      runGit(root.path(), {QStringLiteral("commit"), QStringLiteral("-am"), QString::fromUtf8(content).trimmed()});
    };
    runGit(root.path(), {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("topic")});
    change("topic\n");
    runGit(root.path(), {QStringLiteral("switch"), QStringLiteral("main")});
    change("main\n");
    relay::RelayControllerConfig config;
    config.storeFile = store.path() + QStringLiteral("/state.json");
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.show(); controller.start(); controller.openRepository(root.path());
    QTRY_VERIFY(controller.currentRepository());
    controller.executeRepositoryAction(relay::RepositoryAction::mergeBranch, QStringLiteral("topic"));
    QTRY_COMPARE(controller.currentRepository()->pendingOperation, QStringLiteral("merge"));
    auto* banner = window.findChild<QPushButton*>(QStringLiteral("conflictButton"));
    QVERIFY(banner->isVisible());
    QTRY_VERIFY(banner->isEnabled());
    QTimer::singleShot(15000, &window, [&window] {
      if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("conflictsDialog"))) dialog->reject();
    });
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>(QStringLiteral("conflictsDialog"));
      QVERIFY(dialog);
      auto* error = dialog->findChild<QLabel*>(QStringLiteral("conflictError"));
      QVERIFY(error);
      controller.executeRepositoryAction(relay::RepositoryAction::continueOperation);
      QTRY_VERIFY(!error->text().isEmpty());
      QVERIFY(error->text().contains(QStringLiteral("Resolve")));
      if (const auto output = qEnvironmentVariable("RELAY_SCREENSHOT_DIR"); !output.isEmpty()) {
        QDir().mkpath(output);
        dialog->grab().save(QDir(output).filePath(QStringLiteral("conflicts.png")));
      }
      dialog->reject();
    });
    banner->click();
    QTRY_VERIFY(banner->isEnabled());
    controller.executeRepositoryAction(relay::RepositoryAction::abortOperation);
    QTRY_VERIFY(controller.currentRepository()->pendingOperation.isEmpty());
    QVERIFY(!banner->isVisible());
  }

  void themeSettingsRejectInvalidOverridesBeforeAccepting() {
    relay::Preferences preferences;
    preferences.themeId = QStringLiteral("catppuccin-mocha");
    relay::SettingsDialog dialog(preferences);
    auto* preset = dialog.findChild<QComboBox*>(QStringLiteral("themePreset"));
    auto* json = dialog.findChild<QPlainTextEdit*>(QStringLiteral("themeOverrides"));
    QVERIFY(preset && json);
    QCOMPARE(preset->currentData().toString(), preferences.themeId);
    preset->setCurrentIndex(1);
    json->setPlainText(QStringLiteral("{\"accent\":\"#f288bb\"}"));
    QCOMPARE(dialog.preferences().themeId, QStringLiteral("dark"));
    QCOMPARE(dialog.preferences().customTheme.value(QStringLiteral("accent")).toString(), QStringLiteral("#f288bb"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    QVERIFY(buttons);
    json->setPlainText(QStringLiteral("{\"accent\":\"invalid\"}"));
    buttons->button(QDialogButtonBox::Save)->click();
    QCOMPARE(dialog.result(), 0);
    QVERIFY(!dialog.findChild<QLabel*>(QStringLiteral("themeError"))->text().isEmpty());
    json->setPlainText(QStringLiteral("{}"));
    buttons->button(QDialogButtonBox::Save)->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
  }

  void graphHistoryRendersAndSettingsSwitchBackToList() {
    QTemporaryDir temporary;
    QTemporaryDir store;
    createRepository(temporary.path());
    runGit(temporary.path(), {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("feature")});
    runGit(temporary.path(), {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("Feature work")});
    runGit(temporary.path(), {QStringLiteral("switch"), QStringLiteral("main")});
    runGit(temporary.path(), {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("Main work")});
    runGit(temporary.path(), {QStringLiteral("merge"), QStringLiteral("--no-ff"), QStringLiteral("feature"), QStringLiteral("-m"), QStringLiteral("Merge feature")});
    runGit(temporary.path(), {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("design"), QStringLiteral("main~2")});
    runGit(temporary.path(), {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("Design color palettes")});
    runGit(temporary.path(), {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("docs"), QStringLiteral("main~2")});
    runGit(temporary.path(), {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("Document installation")});
    runGit(temporary.path(), {QStringLiteral("switch"), QStringLiteral("main")});
    relay::RelayControllerConfig config;
    config.storeFile = store.path() + QStringLiteral("/state.json");
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.show();
    controller.start();
    auto preferences = controller.state().preferences;
    preferences.graphHistory = true;
    controller.setPreferences(preferences);
    controller.openRepository(temporary.path());
    QTRY_VERIFY(controller.currentRepository());
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("contentTabs"));
    tabs->setCurrentIndex(1);
    auto* history = window.findChild<QListView*>(QStringLiteral("historyList"));
    QTRY_COMPARE(history->model()->rowCount(), 6);
    auto* model = dynamic_cast<relay::HistoryCommitListModel*>(history->model());
    QVERIFY(model->graphRowAt(0));
    history->setCurrentIndex(model->index(0));
    QTest::qWait(100);
    if (const auto output = qEnvironmentVariable("RELAY_SCREENSHOT_DIR"); !output.isEmpty()) {
      QDir().mkpath(output);
      window.grab().save(QDir(output).filePath(QStringLiteral("graph.png")));
    }
    for (const auto& id : relay::theme::presetIds()) {
      preferences.themeId = id;
      controller.setPreferences(preferences);
      QCOMPARE(qApp->palette().color(QPalette::Text), relay::theme::colors().ink);
      QCOMPARE(controller.state().preferences.themeId, id);
      QTest::qWait(30);
      if (const auto output = qEnvironmentVariable("RELAY_SCREENSHOT_DIR"); !output.isEmpty())
        QVERIFY(window.grab().save(QDir(output).filePath(QStringLiteral("graph-%1.png").arg(id))));
    }
    auto* mode = window.findChild<QComboBox*>(QStringLiteral("historyMode"));
    QVERIFY(mode);
    QCOMPARE(mode->currentIndex(), 1);
    mode->setCurrentIndex(0);
    QVERIFY(!controller.state().preferences.graphHistory);
    QTRY_COMPARE(history->model()->rowCount(), 4);
    QVERIFY(!model->graphRowAt(0));
    preferences.themeId = QStringLiteral("light");
    preferences.graphHistory = false;
    controller.setPreferences(preferences);
  }

  void refreshPreservesFileSelectionAndSelectAllUsesOneClick() {
    QTemporaryDir temporary;
    createRepository(temporary.path());
    QFile first(temporary.path() + QStringLiteral("/a.txt"));
    QVERIFY(first.open(QIODevice::WriteOnly)); first.write("a"); first.close();
    QFile second(temporary.path() + QStringLiteral("/b.txt"));
    QVERIFY(second.open(QIODevice::WriteOnly)); second.write("b"); second.close();
    relay::RelayControllerConfig config;
    config.storeFile = temporary.path() + QStringLiteral("/state/relay.json");
    config.applicationDirectory = QCoreApplication::applicationDirPath();
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    controller.start(); controller.openRepository(temporary.path());
    auto* files = window.findChild<QListView*>(QStringLiteral("changedFileList"));
    QTRY_COMPARE(files->model()->rowCount(), 2);
    files->setCurrentIndex(files->model()->index(1, 0));
    const auto path = files->currentIndex().data(relay::ChangedFileListModel::pathRole);
    QSignalSpy refreshed(&controller, &relay::RelayController::currentRepositoryChanged);
    controller.refreshRepository();
    QTRY_COMPARE(refreshed.count(), 1);
    QCOMPARE(files->currentIndex().data(relay::ChangedFileListModel::pathRole), path);
    auto* check = window.findChild<QCheckBox*>();
    QVERIFY(check);
    check->setCheckState(Qt::Unchecked);
    check->click();
    QCOMPARE(check->checkState(), Qt::Checked);
    QCOMPARE(files->model()->index(0, 0).data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
    QCOMPARE(files->model()->index(1, 0).data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
    auto* repos = window.findChild<QListView*>(QStringLiteral("repositoryList"));
    QVERIFY(repos->currentIndex().isValid());
    for (auto* label : window.findChildren<QLabel*>()) QCOMPARE(label->textFormat(), Qt::PlainText);
  }

  void repositorySettingsKeepsLegacyAliasBindingAndDistinguishesFollowActive() {
#ifndef Q_OS_UNIX
    QSKIP("Directory symlink fixture requires Unix.");
#else
    QTemporaryDir root;
    const auto path = root.filePath(QStringLiteral("repository"));
    const auto alias = root.filePath(QStringLiteral("alias"));
    QVERIFY(QDir().mkpath(path));
    createRepository(path);
    QVERIFY(QFile::link(path, alias));
    relay::Account personal;
    personal.id = QStringLiteral("github-1"); personal.githubId = 1;
    personal.handle = QStringLiteral("personal"); personal.authSource = QStringLiteral("github-cli");
    auto work = personal;
    work.id = QStringLiteral("github-2"); work.githubId = 2; work.handle = QStringLiteral("work");
    relay::AppState state;
    state.accounts = {personal, work}; state.activeAccountId = personal.id;
    state.repositoryAccounts.insert(alias, work.id);
    relay::RelayControllerConfig config;
    config.storeFile = root.filePath(QStringLiteral("profile/relay-data.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayStore(config.storeFile).write(relay::mergeAppStateIntoJson(state));
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    controller.start();
    window.show();
    controller.openRepository(path);
    auto* settings = window.findChild<QPushButton*>(QStringLiteral("repositorySettingsButton"));
    QVERIFY(settings);
    QTRY_VERIFY(settings->isEnabled());
    const auto selectedAccountOnSave = [&] {
      QString selected = QStringLiteral("dialog not visited");
      QTimer::singleShot(0, &window, [&] {
        auto* dialog = window.findChild<QDialog*>(QStringLiteral("repositorySettingsDialog"));
        if (!dialog) return;
        auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("repositoryAccountCombo"));
        if (combo) selected = combo->currentData().toString();
        dialog->accept();
      });
      settings->click();
      return selected;
    };
    QCOMPARE(selectedAccountOnSave(), work.id);
    QCOMPARE(controller.boundAccountId(path), work.id);
    controller.setRepositoryAccount(alias, {});
    QCOMPARE(selectedAccountOnSave(), QString{});
    QCOMPARE(controller.boundAccountId(path), QString{});
    QCOMPARE(controller.resolvedAccountId(path), personal.id);
#endif
  }

  void settingsMenuPersistsAndEditMenuTargetsFocusedInput() {
    QTemporaryDir root;
    relay::RelayControllerConfig config;
    config.storeFile = root.filePath(QStringLiteral("relay-data.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    controller.start();
    window.show();
    auto* settings = window.findChild<QAction*>(QStringLiteral("settingsAction"));
    QVERIFY(settings);
    bool visited = false;
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<relay::SettingsDialog*>();
      if (!dialog) return;
      auto* size = dialog->findChild<QSpinBox*>(QStringLiteral("diffFontSize"));
      if (size) { size->setValue(18); visited = true; }
      if (const auto output = qEnvironmentVariable("RELAY_SCREENSHOT_DIR"); !output.isEmpty()) {
        QDir().mkpath(output);
        dialog->grab().save(QDir(output).filePath(QStringLiteral("settings.png")));
      }
      dialog->accept();
    });
    settings->trigger();
    QVERIFY(visited);
    QCOMPARE(controller.state().preferences.diffFontSize, 18);
    auto* input = window.findChild<QLineEdit*>(QStringLiteral("repositoryFilter"));
    QVERIFY(input);
    input->setText(QStringLiteral("editable"));
    window.activateWindow();
    QApplication::setActiveWindow(&window);
    QApplication::processEvents();
    input->setFocus();
    input->selectAll();
    QTRY_VERIFY(input->hasFocus());
    auto* copy = window.findChild<QAction*>(QStringLiteral("copyAction"));
    auto* cut = window.findChild<QAction*>(QStringLiteral("cutAction"));
    auto* paste = window.findChild<QAction*>(QStringLiteral("pasteAction"));
    QVERIFY(copy && cut && paste);
    copy->trigger();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("editable"));
    cut->trigger();
    QVERIFY(input->text().isEmpty());
    paste->trigger();
    QCOMPARE(input->text(), QStringLiteral("editable"));
  }

  void historyChangesWhenSwitchingBranchesInTheSameRepository() {
    QTemporaryDir root;
    createRepository(root.path());
    relay::RelayControllerConfig config;
    config.storeFile = root.filePath(QStringLiteral("profile/relay-data.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    controller.start();
    controller.setPreferences({false, 12});
    window.show();
    controller.openRepository(root.path());
    QTRY_VERIFY_WITH_TIMEOUT(controller.currentRepository() != nullptr, 5000);
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("contentTabs"));
    auto* history = window.findChild<QListView*>(QStringLiteral("historyList"));
    QVERIFY(tabs && history);
    tabs->setCurrentIndex(1);
    QTRY_COMPARE_WITH_TIMEOUT(history->model()->rowCount(), 1, 5000);
    runGit(root.path(), {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("side")});
    QFile file(root.filePath(QStringLiteral("side.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("side\n"); file.close();
    runGit(root.path(), {QStringLiteral("add"), QStringLiteral("side.txt")});
    runGit(root.path(), {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("Side change")});
    controller.refreshRepository();
    QTRY_COMPARE_WITH_TIMEOUT(history->model()->rowCount(), 2, 5000);
    controller.switchBranch(QStringLiteral("main"));
    QTRY_COMPARE_WITH_TIMEOUT(history->model()->rowCount(), 1, 5000);
    QCOMPARE(controller.currentRepository()->branch, QStringLiteral("main"));
    history->setCurrentIndex(history->model()->index(0, 0));
    auto* commitFiles = window.findChild<QListView*>(QStringLiteral("commitFileList"));
    QTRY_VERIFY(commitFiles->currentIndex().isValid());
    auto* search = [&]() -> QLineEdit* {
      for (auto* edit : window.findChildren<QLineEdit*>())
        if (edit->accessibleName() == QStringLiteral("Search loaded commits")) return edit;
      return nullptr;
    }();
    QVERIFY(search);
    QTest::qWait(100);
    QVERIFY(history->isVisible());
    QVERIFY(history->height() > 100);
    QVERIFY(commitFiles->height() > 30);
    QVERIFY(commitFiles->isVisible());
    if (const auto output = qEnvironmentVariable("RELAY_SCREENSHOT_DIR"); !output.isEmpty()) {
      QDir().mkpath(output);
      window.grab().save(QDir(output).filePath(QStringLiteral("history.png")));
    }
    search->setText(QStringLiteral("nonmatching-filter"));
    QCOMPARE(commitFiles->model()->rowCount(), 0);
    for (auto* button : window.findChildren<QPushButton*>()) {
      if (button->text() == QStringLiteral("Copy hash") || button->text() == QStringLiteral("Open on GitHub"))
        QVERIFY(!button->isEnabled());
    }
    controller.busyChanged(QStringLiteral("commit"), true);
    for (auto* edit : window.findChildren<QLineEdit*>())
      if (edit->accessibleName() == QStringLiteral("Commit summary")) QVERIFY(!edit->isEnabled());
    QVERIFY(!window.findChild<QPlainTextEdit*>()->isEnabled());
    controller.busyChanged(QStringLiteral("commit"), false);
    QVERIFY(window.findChild<QPlainTextEdit*>()->isEnabled());
    controller.closeRepository();
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("createBranchAction"))->isEnabled());
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("pullAction"))->isEnabled());
  }

  void operationFailureRemainsVisibleAfterBusyEnds() {
    QTemporaryDir temporary;
    relay::RelayControllerConfig config;
    config.storeFile = temporary.path() + QStringLiteral("/state.json");
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    controller.busyChanged(QStringLiteral("fetch"), true);
    controller.operationFailed(QStringLiteral("fetch"), QStringLiteral("Fetch failed: offline"));
    controller.busyChanged(QStringLiteral("fetch"), false);
    QCOMPARE(window.statusBar()->currentMessage(), QStringLiteral("Fetch failed: offline"));
    controller.busyChanged(QStringLiteral("refresh"), true);
    QCOMPARE(window.statusBar()->currentMessage(), QStringLiteral("Fetch failed: offline"));
    controller.busyChanged(QStringLiteral("refresh"), false);
    QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("No repository open"), 6000);
    QVERIFY(window.statusBar()->styleSheet().isEmpty());
  }

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
