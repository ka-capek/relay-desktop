#include "relay/main_window.hpp"
#include "relay/compare_dialog.hpp"
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
#include <QToolButton>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QSplitter>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QListView>
#include <QListWidget>
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

// Receives URLs that the window would open in the system browser.
class UrlRecorder final : public QObject {
  Q_OBJECT
 public:
  QList<QUrl> urls;
 public slots:
  void open(const QUrl& url) { urls.append(url); }
};

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
  void searchFindsCommitsBeyondTheLoadedPages() {
    QTemporaryDir temporary;
    createRepository(temporary.path());
    runGit(temporary.path(), {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"),
                              QStringLiteral("Old work"), QStringLiteral("-m"), QStringLiteral("Mentions the zephyr bug")});
    for (int index = 0; index < 60; ++index)
      runGit(temporary.path(), {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"),
                                QStringLiteral("Filler %1").arg(index)});
    relay::RelayControllerConfig config;
    config.storeFile = temporary.filePath(QStringLiteral("profile/relay-data.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.resize(1250, 820);
    window.show();
    controller.start();
    controller.openRepository(temporary.path());
    auto* history = window.findChild<QListView*>(QStringLiteral("historyList"));
    QVERIFY(history);
    window.findChild<QTabWidget*>()->setCurrentIndex(1);
    auto* model = dynamic_cast<relay::HistoryCommitListModel*>(history->model());
    QTRY_COMPARE_WITH_TIMEOUT(model->commits().size(), 50, 10000);
    QLineEdit* search = nullptr;
    for (auto* edit : window.findChildren<QLineEdit*>())
      if (edit->accessibleName() == QStringLiteral("Search commits")) search = edit;
    QVERIFY(search);
    // "zephyr" is only in the body of a commit that is not loaded yet.
    search->setText(QStringLiteral("ZEPHYR"));
    QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 1, 10000);
    QCOMPARE(model->commitAt(0)->title, QStringLiteral("Old work"));
    QVERIFY(model->showingSearchResults());
    search->clear();
    QTRY_VERIFY_WITH_TIMEOUT(!model->showingSearchResults() && model->rowCount() == 50, 10000);
  }

  void paneWidthsSurviveRestartAndReset() {
    QTemporaryDir temporary;
    relay::RelayControllerConfig config;
    config.storeFile = temporary.filePath(QStringLiteral("profile/relay-data.json"));
    config.synchronizeAccountsOnStart = false;
    QList<int> chosen;
    {
      relay::RelayController controller(config);
      relay::MainWindow window(&controller);
      window.resize(1250, 820);
      window.show();
      controller.start();
      auto* workspace = window.findChild<QSplitter*>(QStringLiteral("workspaceSplitter"));
      QVERIFY(workspace);
      const auto total = workspace->sizes().value(0) + workspace->sizes().value(1);
      workspace->setSizes({420, total - 420});
      chosen = workspace->sizes();
      window.close();
    }
    const auto stored = relay::RelayStore(config.storeFile).read()
        .value(QStringLiteral("preferences")).toObject().value(QStringLiteral("layout")).toObject();
    QVERIFY(stored.value(QStringLiteral("splitters")).toObject().contains(QStringLiteral("workspaceSplitter")));
    QVERIFY(!stored.value(QStringLiteral("window")).toString().isEmpty());
    // The offscreen platform's small virtual screen clamps a restored window,
    // which would shrink the panes; check the splitters at the same size.
    auto json = relay::RelayStore(config.storeFile).read();
    auto preferences = json.value(QStringLiteral("preferences")).toObject();
    auto layout = preferences.value(QStringLiteral("layout")).toObject();
    layout.remove(QStringLiteral("window"));
    preferences.insert(QStringLiteral("layout"), layout);
    json.insert(QStringLiteral("preferences"), preferences);
    relay::RelayStore(config.storeFile).write(json);

    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.resize(1250, 820);
    window.show();
    controller.start();
    auto* workspace = window.findChild<QSplitter*>(QStringLiteral("workspaceSplitter"));
    QTRY_COMPARE(workspace->sizes().value(0), chosen.value(0));
    // A settings change does not discard the saved layout.
    controller.setPreferences(controller.state().preferences);
    QCOMPARE(controller.state().preferences.layout, layout);

    auto* reset = window.findChild<QAction*>(QStringLiteral("resetLayoutAction"));
    QVERIFY(reset);
    reset->trigger();
    QVERIFY(workspace->sizes().value(0) < chosen.value(0));
  }

  void browsesAllRemoteBranchesWithoutCheckout() {
    QTemporaryDir temporary;
    createRepository(temporary.path());
    runGit(temporary.path(), {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("review")});
    runGit(temporary.path(), {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("Remote-only change")});
    runGit(temporary.path(), {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("gitea"), temporary.filePath(QStringLiteral("upstream.git"))});
    runGit(temporary.path(), {QStringLiteral("update-ref"), QStringLiteral("refs/remotes/gitea/review"), QStringLiteral("HEAD")});
    runGit(temporary.path(), {QStringLiteral("switch"), QStringLiteral("main")});
    runGit(temporary.path(), {QStringLiteral("branch"), QStringLiteral("-D"), QStringLiteral("review")});
    relay::RelayControllerConfig config;
    config.storeFile = temporary.filePath(QStringLiteral("profile/relay-data.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.resize(1250, 820);
    window.show();
    controller.start();
    controller.openRepository(temporary.path());
    auto* branches = window.findChild<QComboBox*>(QStringLiteral("historyBranch"));
    auto* current = window.findChild<QComboBox*>(QStringLiteral("branchPicker"));
    auto* history = window.findChild<QListView*>(QStringLiteral("historyList"));
    QVERIFY(branches && current && history);
    QTRY_VERIFY_WITH_TIMEOUT(branches->findData(QStringLiteral("refs/remotes/gitea/review")) >= 0, 10000);
    QVERIFY(current->findData(QStringLiteral("refs/remotes/gitea/review")) >= 0);
    branches->setCurrentIndex(branches->findData(QStringLiteral("refs/remotes/gitea/review")));
    auto* model = dynamic_cast<relay::HistoryCommitListModel*>(history->model());
    QVERIFY(model);
    QCOMPARE(history->selectionMode(), QAbstractItemView::ExtendedSelection);
    QTRY_COMPARE_WITH_TIMEOUT(model->commits().size(), 2, 10000);
    QCOMPARE(model->commits().first().title, QStringLiteral("Remote-only change"));
    QCOMPARE(controller.currentRepository()->branch, QStringLiteral("main"));
    QCOMPARE(current->currentText(), QStringLiteral("main"));
    window.findChild<QTabWidget*>()->setCurrentIndex(1);
    const auto screenshots = qEnvironmentVariable("RELAY_SCREENSHOT_DIR");
    if (!screenshots.isEmpty()) {
      QDir().mkpath(screenshots);
      QVERIFY(window.grab().save(screenshots + QStringLiteral("/remote-branch-history.png")));
    }
    auto* checkout = window.findChild<QPushButton*>(QStringLiteral("checkoutHistoryBranch"));
    QTRY_VERIFY(checkout->isEnabled());
    checkout->click();
    QTRY_COMPARE_WITH_TIMEOUT(controller.currentRepository()->branch, QStringLiteral("review"), 10000);
    QCOMPARE(current->currentText(), QStringLiteral("review"));
  }

  void branchPickerChecksOutOriginOnlyBranch() {
    QTemporaryDir temporary;
    const auto seed = temporary.filePath(QStringLiteral("seed"));
    const auto remote = temporary.filePath(QStringLiteral("remote.git"));
    const auto local = temporary.filePath(QStringLiteral("local"));
    QVERIFY(QDir{}.mkpath(seed));
    createRepository(seed);
    runGit(temporary.path(), {QStringLiteral("clone"), QStringLiteral("--bare"), seed, remote});
    runGit(temporary.path(), {QStringLiteral("clone"), remote, local});
    // Pushed after the clone: the local repository has no ref for it yet.
    runGit(seed, {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), remote});
    runGit(seed, {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("feature/origin-only")});
    runGit(seed, {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("Origin only")});
    runGit(seed, {QStringLiteral("push"), QStringLiteral("origin"), QStringLiteral("feature/origin-only")});
    relay::RelayControllerConfig config;
    config.storeFile = temporary.filePath(QStringLiteral("state.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.show();
    controller.start();
    controller.openRepository(local);
    auto* current = window.findChild<QComboBox*>(QStringLiteral("branchPicker"));
    QVERIFY(current);
    const auto ref = QStringLiteral("refs/remotes/origin/feature/origin-only");
    const auto fetchItem = QStringLiteral("relay:fetch-origin-branches");
    QTRY_VERIFY_WITH_TIMEOUT(current->findData(fetchItem) >= 0, 10000);
    QTRY_VERIFY(current->isEnabled());
    QCOMPARE(current->findData(ref), -1);
    current->setCurrentIndex(current->findData(fetchItem));
    QTRY_VERIFY_WITH_TIMEOUT(current->findData(ref) >= 0, 10000);
    QCOMPARE(current->currentText(), QStringLiteral("main"));
    QCOMPARE(controller.currentRepository()->branch, QStringLiteral("main"));
    QTRY_VERIFY(current->isEnabled());
    current->setCurrentIndex(current->findData(ref));
    QTRY_COMPARE_WITH_TIMEOUT(controller.currentRepository()->branch, QStringLiteral("feature/origin-only"), 10000);
    QTRY_COMPARE(current->currentText(), QStringLiteral("feature/origin-only"));
    QProcess upstream;
    upstream.setWorkingDirectory(local);
    upstream.start(QStringLiteral("git"), {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("@{upstream}")});
    QVERIFY(upstream.waitForFinished(10000));
    QCOMPARE(QString::fromUtf8(upstream.readAllStandardOutput()).trimmed(), QStringLiteral("origin/feature/origin-only"));
  }

  void forcePushReplacesOriginAfterConfirmation() {
    QTemporaryDir temporary;
    const auto seed = temporary.filePath(QStringLiteral("seed"));
    const auto remote = temporary.filePath(QStringLiteral("remote.git"));
    const auto local = temporary.filePath(QStringLiteral("local"));
    QVERIFY(QDir{}.mkpath(seed));
    createRepository(seed);
    runGit(temporary.path(), {QStringLiteral("clone"), QStringLiteral("--bare"), seed, remote});
    runGit(temporary.path(), {QStringLiteral("clone"), remote, local});
    runGit(local, {QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Relay Test")});
    runGit(local, {QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("relay@example.test")});
    runGit(local, {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("Published")});
    runGit(local, {QStringLiteral("push"), QStringLiteral("origin"), QStringLiteral("main")});
    runGit(local, {QStringLiteral("commit"), QStringLiteral("--amend"), QStringLiteral("--allow-empty"),
                   QStringLiteral("-m"), QStringLiteral("Rewritten")});
    const auto originSubject = [&remote] {
      QProcess git;
      git.start(QStringLiteral("git"), {QStringLiteral("-C"), remote, QStringLiteral("log"), QStringLiteral("-1"),
                                        QStringLiteral("--format=%s"), QStringLiteral("main")});
      git.waitForFinished(10000);
      return QString::fromUtf8(git.readAllStandardOutput()).trimmed();
    };
    relay::RelayControllerConfig config;
    config.storeFile = temporary.filePath(QStringLiteral("state.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.show();
    controller.start();
    controller.openRepository(local);
    auto* force = window.findChild<QAction*>(QStringLiteral("forcePushAction"));
    QVERIFY(force);
    QTRY_VERIFY_WITH_TIMEOUT(force->isEnabled(), 10000);

    // Cancel is the default and changes nothing.
    QTimer::singleShot(0, &window, [&window] {
      if (auto* box = window.findChild<QMessageBox*>(QStringLiteral("forcePushDialog"))) box->reject();
    });
    force->trigger();
    QCOMPARE(originSubject(), QStringLiteral("Published"));

    QTimer::singleShot(0, &window, [&window] {
      auto* box = window.findChild<QMessageBox*>(QStringLiteral("forcePushDialog"));
      QVERIFY(box);
      QVERIFY(box->informativeText().contains(QStringLiteral("origin/main")));
      if (const auto output = qEnvironmentVariable("RELAY_SCREENSHOT_DIR"); !output.isEmpty()) {
        QDir().mkpath(output);
        box->grab().save(QDir(output).filePath(QStringLiteral("force-push.png")));
      }
      box->findChild<QPushButton*>(QStringLiteral("forcePushButton"))->click();
    });
    force->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(originSubject(), QStringLiteral("Rewritten"), 10000);
  }

  void deletesBranchOnOriginAfterConfirmation() {
    QTemporaryDir temporary;
    const auto seed = temporary.filePath(QStringLiteral("seed"));
    const auto remote = temporary.filePath(QStringLiteral("remote.git"));
    const auto local = temporary.filePath(QStringLiteral("local"));
    QVERIFY(QDir{}.mkpath(seed));
    createRepository(seed);
    runGit(seed, {QStringLiteral("branch"), QStringLiteral("finished")});
    runGit(temporary.path(), {QStringLiteral("clone"), QStringLiteral("--bare"), seed, remote});
    runGit(temporary.path(), {QStringLiteral("clone"), remote, local});
    const auto originHas = [&remote](const QString& branch) {
      QProcess git;
      git.start(QStringLiteral("git"), {QStringLiteral("-C"), remote, QStringLiteral("for-each-ref"), QStringLiteral("refs/heads/") + branch});
      git.waitForFinished(10000);
      return !git.readAllStandardOutput().trimmed().isEmpty();
    };
    relay::RelayControllerConfig config;
    config.storeFile = temporary.filePath(QStringLiteral("state.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.show();
    controller.start();
    controller.openRepository(local);
    auto* remove = window.findChild<QAction*>(QStringLiteral("deleteOriginBranchAction"));
    QVERIFY(remove);
    QTRY_VERIFY_WITH_TIMEOUT(remove->isEnabled(), 10000);

    bool confirmed = false;
    QTimer::singleShot(0, &window, [&] {
      auto* chooser = window.findChild<QInputDialog*>();
      QVERIFY(chooser);
      QVERIFY(chooser->comboBoxItems().contains(QStringLiteral("finished")));
      QTimer::singleShot(0, &window, [&] {
        auto* box = window.findChild<QMessageBox*>(QStringLiteral("deleteOriginBranchDialog"));
        QVERIFY(box);
        QVERIFY(box->text().contains(QStringLiteral("finished")));
        if (const auto output = qEnvironmentVariable("RELAY_SCREENSHOT_DIR"); !output.isEmpty()) {
          QDir().mkpath(output);
          box->grab().save(QDir(output).filePath(QStringLiteral("delete-origin-branch.png")));
        }
        box->findChild<QPushButton*>(QStringLiteral("deleteOriginBranchButton"))->click();
        confirmed = true;
      });
      chooser->setTextValue(QStringLiteral("finished"));
      chooser->accept();
    });
    remove->trigger();
    QVERIFY(confirmed);
    QTRY_VERIFY_WITH_TIMEOUT(!originHas(QStringLiteral("finished")), 10000);
    QVERIFY(originHas(QStringLiteral("main")));
    QTRY_VERIFY(!controller.currentRepository()->remoteBranches.contains(QStringLiteral("origin/finished")));
  }

  void opensPullRequestPageForPublishedGitHubBranch() {
    QTemporaryDir temporary;
    createRepository(temporary.path());
    runGit(temporary.path(), {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("feature/pr")});
    runGit(temporary.path(), {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"),
                              QStringLiteral("https://github.com/octo/relay.git")});
    relay::RelayControllerConfig config;
    config.storeFile = temporary.filePath(QStringLiteral("profile/state.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.show();
    controller.start();
    controller.openRepository(temporary.path());
    auto* action = window.findChild<QAction*>(QStringLiteral("pullRequestAction"));
    QVERIFY(action);
    QTRY_VERIFY_WITH_TIMEOUT(action->isEnabled(), 10000);
    UrlRecorder recorder;
    QDesktopServices::setUrlHandler(QStringLiteral("https"), &recorder, "open");
    QSignalSpy failures(&controller, &relay::RelayController::operationFailed);

    // An unpublished branch has nothing on GitHub to compare yet.
    action->trigger();
    QTRY_COMPARE(failures.size(), 1);
    QVERIFY(failures.first().at(1).toString().contains(QStringLiteral("Push feature/pr")));
    QVERIFY(recorder.urls.isEmpty());

    // Pushes go to a local bare repository; the fetch URL stays on GitHub.
    const auto bare = temporary.filePath(QStringLiteral("pushed.git"));
    runGit(temporary.path(), {QStringLiteral("init"), QStringLiteral("--bare"), bare});
    runGit(temporary.path(), {QStringLiteral("remote"), QStringLiteral("set-url"), QStringLiteral("--push"), QStringLiteral("origin"), bare});
    runGit(temporary.path(), {QStringLiteral("push"), QStringLiteral("--set-upstream"), QStringLiteral("origin"), QStringLiteral("feature/pr")});
    runGit(temporary.path(), {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("Unpushed")});
    controller.refreshRepository();
    QTRY_VERIFY_WITH_TIMEOUT(controller.currentRepository()->ahead == 1, 10000);
    QTRY_VERIFY(action->isEnabled());
    bool prompted = false;
    QTimer::singleShot(0, &window, [&] {
      auto* box = window.findChild<QMessageBox*>(QStringLiteral("pullRequestUnpushedDialog"));
      QVERIFY(box);
      prompted = true;
      box->defaultButton()->click();
    });
    action->trigger();
    QVERIFY(prompted);
    QTRY_COMPARE_WITH_TIMEOUT(recorder.urls.size(), 1, 10000);
    QCOMPARE(controller.currentRepository()->ahead, 0);
    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
    QCOMPARE(recorder.urls.first(), QUrl(QStringLiteral("https://github.com/octo/relay/compare/feature/pr?expand=1")));
  }

  void squashesUnpublishedCommitsFromTheMenu() {
    QTemporaryDir temporary;
    createRepository(temporary.path());
    runGit(temporary.path(), {QStringLiteral("update-ref"), QStringLiteral("refs/remotes/origin/main"), QStringLiteral("HEAD")});
    runGit(temporary.path(), {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("First")});
    runGit(temporary.path(), {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("Second")});
    QTemporaryDir profile;
    relay::RelayControllerConfig config;
    config.storeFile = profile.filePath(QStringLiteral("state.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.show();
    controller.start();
    controller.openRepository(temporary.path());
    auto* action = window.findChild<QAction*>(QStringLiteral("rewriteCommitsAction"));
    QVERIFY(action);
    QTRY_VERIFY_WITH_TIMEOUT(action->isEnabled(), 10000);
    // The dialog is modal and opens once the commits have been read.
    bool shown = false;
    QTimer poll;
    connect(&poll, &QTimer::timeout, &window, [&] {
      auto* dialog = window.findChild<relay::CommitRewriteDialog*>();
      if (!dialog || !dialog->isVisible()) return;
      poll.stop();
      auto* list = dialog->findChild<QListWidget*>(QStringLiteral("rewriteList"));
      QCOMPARE(list->count(), 2);
      list->item(0)->setCheckState(Qt::Checked);
      if (const auto output = qEnvironmentVariable("RELAY_SCREENSHOT_DIR"); !output.isEmpty()) {
        QDir().mkpath(output);
        dialog->grab().save(QDir(output).filePath(QStringLiteral("rewrite-commits.png")));
      }
      shown = true;
      dialog->findChild<QPushButton*>(QStringLiteral("rewriteAccept"))->click();
    });
    poll.start(50);
    action->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(shown, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.currentRepository()->history.value(0).title, QStringLiteral("First"), 10000);
    QCOMPARE(controller.currentRepository()->history.value(1).title, QStringLiteral("Initial commit"));
  }

  void comparesBranchesAndMergesFromTheDialog() {
    QTemporaryDir temporary;
    createRepository(temporary.path());
    runGit(temporary.path(), {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("feature")});
    QFile file(temporary.filePath(QStringLiteral("feature.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("feature\n");
    file.close();
    runGit(temporary.path(), {QStringLiteral("add"), QStringLiteral("feature.txt")});
    runGit(temporary.path(), {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("Feature work")});
    runGit(temporary.path(), {QStringLiteral("switch"), QStringLiteral("main")});
    QTemporaryDir profile;
    relay::RelayControllerConfig config;
    config.storeFile = profile.filePath(QStringLiteral("state.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.show();
    controller.start();
    controller.openRepository(temporary.path());
    auto* action = window.findChild<QAction*>(QStringLiteral("compareBranchesAction"));
    QVERIFY(action);
    QTRY_VERIFY_WITH_TIMEOUT(action->isEnabled(), 10000);
    action->trigger();
    auto* dialog = window.findChild<relay::CompareDialog*>();
    QVERIFY(dialog && dialog->isVisible());
    auto* base = dialog->findChild<QComboBox*>(QStringLiteral("compareBase"));
    auto* head = dialog->findChild<QComboBox*>(QStringLiteral("compareHead"));
    QCOMPARE(base->currentData().toString(), QStringLiteral("refs/heads/main"));
    QCOMPARE(head->currentData().toString(), QStringLiteral("refs/heads/feature"));
    auto* tabs = dialog->findChild<QTabWidget*>();
    QTRY_COMPARE_WITH_TIMEOUT(tabs->tabText(0), QStringLiteral("1 only in feature"), 10000);
    QCOMPARE(tabs->tabText(1), QStringLiteral("0 only in main"));
    auto* diff = static_cast<relay::DiffView*>(dialog->findChild<QWidget*>(QStringLiteral("compareDiff")));
    QVERIFY(diff);
    QTRY_VERIFY_WITH_TIMEOUT(diff->diffModel()->rowCount() > 0, 10000);
    tabs->setCurrentIndex(2);
    if (const auto output = qEnvironmentVariable("RELAY_SCREENSHOT_DIR"); !output.isEmpty()) {
      QDir().mkpath(output);
      dialog->grab().save(QDir(output).filePath(QStringLiteral("compare-branches.png")));
    }
    auto* merge = dialog->findChild<QPushButton*>(QStringLiteral("compareMerge"));
    QVERIFY(merge->isVisible());
    QTimer::singleShot(0, &window, [&dialog] {
      if (auto* box = dialog->findChild<QMessageBox*>()) box->button(QMessageBox::Ok)->click();
    });
    merge->click();
    QTRY_COMPARE_WITH_TIMEOUT(controller.currentRepository()->history.value(0).title, QStringLiteral("Feature work"), 10000);
    // The open dialog follows the repository: nothing is left to merge.
    QTRY_COMPARE_WITH_TIMEOUT(tabs->tabText(0), QStringLiteral("0 only in feature"), 10000);
    QVERIFY(!merge->isVisible());
  }

  void commitsWithARecentCoAuthor() {
    QTemporaryDir temporary;
    createRepository(temporary.path());
    runGit(temporary.path(), {QStringLiteral("-c"), QStringLiteral("user.name=Ada Lovelace"), QStringLiteral("-c"),
                              QStringLiteral("user.email=ada@example.com"), QStringLiteral("commit"), QStringLiteral("--allow-empty"),
                              QStringLiteral("-m"), QStringLiteral("Ada's work")});
    QFile readme(temporary.filePath(QStringLiteral("README.md")));
    QVERIFY(readme.open(QIODevice::Append));
    readme.write("more\n");
    readme.close();
    QTemporaryDir profile;
    relay::RelayControllerConfig config;
    config.storeFile = profile.filePath(QStringLiteral("state.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.show();
    controller.start();
    controller.openRepository(temporary.path());
    auto* coAuthors = window.findChild<QLineEdit*>(QStringLiteral("commitCoAuthors"));
    auto* recent = window.findChild<QToolButton*>(QStringLiteral("coAuthorButton"));
    auto* summary = window.findChild<QLineEdit*>(QStringLiteral("commitSummary"));
    auto* commit = window.findChild<QPushButton*>(QStringLiteral("commitButton"));
    QVERIFY(coAuthors && recent && summary && commit);
    QTRY_VERIFY_WITH_TIMEOUT(controller.currentRepository() && !controller.currentRepository()->files.isEmpty(), 10000);
    emit recent->menu()->aboutToShow();
    const auto actions = recent->menu()->actions();
    const auto ada = std::find_if(actions.cbegin(), actions.cend(), [](QAction* action) { return action->text() == QStringLiteral("Ada Lovelace <ada@example.com>"); });
    QVERIFY(ada != actions.cend());
    (*ada)->trigger();
    QCOMPARE(coAuthors->text(), QStringLiteral("Ada Lovelace <ada@example.com>"));
    summary->setText(QStringLiteral("Pair on README"));
    QTRY_VERIFY(commit->isEnabled());
    if (const auto output = qEnvironmentVariable("RELAY_SCREENSHOT_DIR"); !output.isEmpty()) {
      QDir().mkpath(output);
      window.grab().save(QDir(output).filePath(QStringLiteral("commit-co-authors.png")));
    }
    commit->click();
    QTRY_COMPARE_WITH_TIMEOUT(controller.currentRepository()->history.value(0).title, QStringLiteral("Pair on README"), 10000);
    QVERIFY(coAuthors->text().isEmpty());
    QProcess git;
    git.start(QStringLiteral("git"), {QStringLiteral("-C"), temporary.path(), QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%B")});
    QVERIFY(git.waitForFinished(10000));
    QVERIFY(QString::fromUtf8(git.readAllStandardOutput()).contains(QStringLiteral("Co-authored-by: Ada Lovelace <ada@example.com>")));
  }

  void pushesAndDeletesTagsOnOrigin() {
    QTemporaryDir temporary;
    const auto seed = temporary.filePath(QStringLiteral("seed"));
    const auto remote = temporary.filePath(QStringLiteral("remote.git"));
    const auto local = temporary.filePath(QStringLiteral("local"));
    QVERIFY(QDir{}.mkpath(seed));
    createRepository(seed);
    runGit(temporary.path(), {QStringLiteral("clone"), QStringLiteral("--bare"), seed, remote});
    runGit(temporary.path(), {QStringLiteral("clone"), remote, local});
    runGit(local, {QStringLiteral("tag"), QStringLiteral("v2.0")});
    const auto originTags = [&remote] {
      QProcess git;
      git.start(QStringLiteral("git"), {QStringLiteral("-C"), remote, QStringLiteral("tag"), QStringLiteral("--list")});
      git.waitForFinished(10000);
      return QString::fromUtf8(git.readAllStandardOutput()).trimmed();
    };
    QTemporaryDir profile;
    relay::RelayControllerConfig config;
    config.storeFile = profile.filePath(QStringLiteral("state.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.show();
    controller.start();
    controller.openRepository(local);
    auto* push = window.findChild<QAction*>(QStringLiteral("pushTagAction"));
    auto* remove = window.findChild<QAction*>(QStringLiteral("deleteOriginTagAction"));
    QVERIFY(push && remove);
    QTRY_VERIFY_WITH_TIMEOUT(push->isEnabled() && remove->isEnabled(), 10000);

    // Answers each modal dialog as it appears.
    QStringList answered;
    QTimer poll;
    connect(&poll, &QTimer::timeout, &window, [&] {
      if (auto* chooser = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
        QVERIFY(chooser->comboBoxItems().contains(QStringLiteral("v2.0")));
        chooser->setTextValue(QStringLiteral("v2.0"));
        answered.append(chooser->windowTitle());
        chooser->accept();
      } else if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                 box && box->objectName() == QStringLiteral("deleteOriginTagDialog")) {
        answered.append(box->objectName());
        box->findChild<QPushButton*>(QStringLiteral("deleteOriginTagButton"))->click();
      }
    });
    poll.start(30);
    push->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(originTags(), QStringLiteral("v2.0"), 10000);
    QTRY_VERIFY(remove->isEnabled());
    remove->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(originTags().isEmpty(), 10000);
    QCOMPARE(answered, (QStringList{QStringLiteral("Push tag to origin"), QStringLiteral("Delete tag on origin"), QStringLiteral("deleteOriginTagDialog")}));
    QVERIFY(controller.currentRepository()->tags.contains(QStringLiteral("v2.0")));
  }

  void rebasesPublishedCommitsThenOffersForcePush() {
    QTemporaryDir temporary;
    const auto seed = temporary.filePath(QStringLiteral("seed"));
    const auto remote = temporary.filePath(QStringLiteral("remote.git"));
    const auto local = temporary.filePath(QStringLiteral("local"));
    QVERIFY(QDir{}.mkpath(seed));
    createRepository(seed);
    runGit(temporary.path(), {QStringLiteral("clone"), QStringLiteral("--bare"), seed, remote});
    runGit(temporary.path(), {QStringLiteral("clone"), remote, local});
    runGit(local, {QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Relay Test")});
    runGit(local, {QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("relay@example.test")});
    runGit(local, {QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("feature")});
    runGit(local, {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("Feature")});
    runGit(local, {QStringLiteral("push"), QStringLiteral("-u"), QStringLiteral("origin"), QStringLiteral("feature")});
    runGit(local, {QStringLiteral("switch"), QStringLiteral("main")});
    runGit(local, {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("Main moved")});
    runGit(local, {QStringLiteral("switch"), QStringLiteral("feature")});
    const auto originFeature = [&remote] {
      QProcess git;
      git.start(QStringLiteral("git"), {QStringLiteral("-C"), remote, QStringLiteral("log"), QStringLiteral("--format=%s"), QStringLiteral("feature")});
      git.waitForFinished(10000);
      return QString::fromUtf8(git.readAllStandardOutput()).trimmed();
    };
    QTemporaryDir profile;
    relay::RelayControllerConfig config;
    config.storeFile = profile.filePath(QStringLiteral("state.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.show();
    controller.start();
    controller.openRepository(local);
    auto* rebase = window.findChild<QAction*>(QStringLiteral("rebaseBranchAction"));
    QVERIFY(rebase);
    QTRY_VERIFY_WITH_TIMEOUT(rebase->isEnabled(), 10000);
    QStringList answered;
    QTimer poll;
    connect(&poll, &QTimer::timeout, &window, [&] {
      auto* modal = QApplication::activeModalWidget();
      if (auto* chooser = qobject_cast<QInputDialog*>(modal)) {
        chooser->setTextValue(QStringLiteral("main"));
        answered.append(QStringLiteral("chooser"));
        chooser->accept();
      } else if (auto* box = qobject_cast<QMessageBox*>(modal); box && box->objectName() == QStringLiteral("rebasePublishedDialog")) {
        answered.append(box->objectName());
        box->findChild<QPushButton*>(QStringLiteral("rebasePublishedButton"))->click();
      } else if (box && box->objectName() == QStringLiteral("forcePushDialog")) {
        answered.append(box->objectName());
        box->findChild<QPushButton*>(QStringLiteral("forcePushButton"))->click();
      }
    });
    poll.start(30);
    rebase->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(originFeature(), QStringLiteral("Feature\nMain moved\nInitial commit"), 15000);
    QCOMPARE(answered, (QStringList{QStringLiteral("chooser"), QStringLiteral("rebasePublishedDialog"), QStringLiteral("forcePushDialog")}));
  }

  void divergedPullRebasesWithoutAskingTwice() {
    QTemporaryDir temporary;
    const auto seed = temporary.filePath(QStringLiteral("seed"));
    const auto remote = temporary.filePath(QStringLiteral("remote.git"));
    const auto local = temporary.filePath(QStringLiteral("local"));
    QVERIFY(QDir{}.mkpath(seed));
    createRepository(seed);
    runGit(temporary.path(), {QStringLiteral("clone"), QStringLiteral("--bare"), seed, remote});
    runGit(temporary.path(), {QStringLiteral("clone"), remote, local});
    runGit(local, {QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Relay Test")});
    runGit(local, {QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("relay@example.test")});
    runGit(seed, {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), remote});
    runGit(seed, {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("Theirs")});
    runGit(seed, {QStringLiteral("push"), QStringLiteral("origin"), QStringLiteral("main")});
    runGit(local, {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("Mine")});
    QTemporaryDir profile;
    relay::RelayControllerConfig config;
    config.storeFile = profile.filePath(QStringLiteral("state.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.show();
    controller.start();
    controller.openRepository(local);
    auto* pull = window.findChild<QAction*>(QStringLiteral("pullAction"));
    QTRY_VERIFY_WITH_TIMEOUT(pull->isEnabled(), 10000);
    QStringList answered;
    QTimer poll;
    connect(&poll, &QTimer::timeout, &window, [&] {
      auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      if (!box) return;
      answered.append(box->objectName());
      if (auto* rebase = box->findChild<QPushButton*>(QStringLiteral("pullRebaseButton"))) rebase->click();
      else box->reject();
    });
    poll.start(30);
    pull->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(controller.currentRepository()->history.value(1).title, QStringLiteral("Theirs"), 15000);
    QCOMPARE(controller.currentRepository()->history.value(0).title, QStringLiteral("Mine"));
    QCOMPARE(answered, QStringList{QStringLiteral("pullDivergedDialog")});
  }

  void toolbarPullFetchesUnknownRemoteChanges() {
    QTemporaryDir temporary;
    const auto seed = temporary.filePath(QStringLiteral("seed"));
    const auto remote = temporary.filePath(QStringLiteral("remote.git"));
    const auto local = temporary.filePath(QStringLiteral("local"));
    QVERIFY(QDir{}.mkpath(seed));
    createRepository(seed);
    runGit(temporary.path(), {QStringLiteral("clone"), QStringLiteral("--bare"), seed, remote});
    runGit(temporary.path(), {QStringLiteral("clone"), remote, local});
    runGit(seed, {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), remote});
    runGit(seed, {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("Remote update")});
    runGit(seed, {QStringLiteral("push"), QStringLiteral("origin"), QStringLiteral("main")});
    relay::RelayControllerConfig config;
    config.storeFile = temporary.filePath(QStringLiteral("state.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.show();
    controller.start();
    controller.openRepository(local);
    QTRY_VERIFY_WITH_TIMEOUT(controller.currentRepository(), 10000);
    QCOMPARE(controller.currentRepository()->behind, 0);
    const auto sync = window.findChild<QToolButton*>(QStringLiteral("syncButton"));
    const auto pull = window.findChild<QAction*>(QStringLiteral("pullAction"));
    QVERIFY(sync && sync->menu() && pull);
    QVERIFY(sync->menu()->actions().contains(pull));
    QVERIFY(pull->isEnabled());
    pull->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(controller.currentRepository()->history.value(0).title,
                              QStringLiteral("Remote update"), 10000);
    controller.closeRepository();
    QVERIFY(!pull->isEnabled());
    QVERIFY(!sync->isEnabled());
  }

  void historyDiffCanBeResizedVerticallyAndHorizontally() {
    QTemporaryDir temporary;
    const auto local = temporary.filePath(QStringLiteral("local"));
    QVERIFY(QDir{}.mkpath(local));
    createRepository(local);
    relay::RelayControllerConfig config;
    config.storeFile = temporary.filePath(QStringLiteral("state.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.resize(1400, 950);
    window.show();
    controller.start();
    controller.openRepository(local);
    QTRY_VERIFY_WITH_TIMEOUT(controller.currentRepository(), 10000);
    window.findChild<QTabWidget*>(QStringLiteral("contentTabs"))->setCurrentIndex(1);
    const auto vertical = window.findChild<QSplitter*>(QStringLiteral("historyDetailSplitter"));
    const auto horizontal = window.findChild<QSplitter*>(QStringLiteral("historySplitter"));
    QVERIFY(vertical && horizontal);
    QTRY_VERIFY(vertical->isVisible());
    const auto history = window.findChild<QListView*>(QStringLiteral("historyList"));
    QTRY_VERIFY_WITH_TIMEOUT(history->model()->rowCount() > 0, 10000);
    history->setCurrentIndex(history->model()->index(0, 0));
    const auto files = window.findChild<QListView*>(QStringLiteral("commitFileList"));
    QTRY_VERIFY_WITH_TIMEOUT(files->model()->rowCount() > 0, 10000);
    const auto diff = dynamic_cast<relay::DiffView*>(vertical->widget(1));
    QVERIFY(diff);
    QTRY_VERIFY_WITH_TIMEOUT(diff->model()->rowCount() > 0, 10000);
    QCoreApplication::processEvents();
    const int before = vertical->widget(1)->height();
    auto* handle = vertical->handle(1);
    const auto center = handle->rect().center();
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, center);
    QTest::mouseMove(handle, center + QPoint(0, -100));
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, center + QPoint(0, -100));
    QTRY_VERIFY(vertical->widget(1)->height() > before + 40);
    const int width = horizontal->widget(1)->width();
    horizontal->setSizes({horizontal->sizes().at(0) + 80, width - 80});
    QTRY_VERIFY(horizontal->widget(1)->width() < width - 30);
    const auto screenshots = qEnvironmentVariable("RELAY_SCREENSHOT_DIR");
    if (!screenshots.isEmpty()) {
      QDir().mkpath(screenshots);
      QVERIFY(window.grab().save(screenshots + QStringLiteral("/resized-history-diff.png")));
    }
  }

  void sshIdentityCanBeChosenWithoutAGitHubAccount_data() {
    QTest::addColumn<QString>("host");
    QTest::newRow("other-host") << QStringLiteral("git.example.com");
    QTest::newRow("github") << QStringLiteral("github.com");
  }

  void sshIdentityCanBeChosenWithoutAGitHubAccount() {
    QFETCH(QString, host);
    QTemporaryDir temporary;
    const auto local = temporary.filePath(QStringLiteral("local"));
    QVERIFY(QDir{}.mkpath(local));
    createRepository(local);
    runGit(local, {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"),
                   QStringLiteral("git@%1:team/project.git").arg(host)});
    relay::RelayControllerConfig config;
    config.storeFile = temporary.filePath(QStringLiteral("state.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.show();
    controller.start();
    controller.saveSshProfile({QStringLiteral("work"), QStringLiteral("Work key"),
        host, QStringLiteral("git"), std::nullopt, {}, true});
    controller.saveSshProfile({QStringLiteral("other"), QStringLiteral("Other host"),
        QStringLiteral("other.example.com"), QStringLiteral("git"), std::nullopt, {}, true});
    controller.openRepository(local);
    QTRY_VERIFY_WITH_TIMEOUT(controller.currentRepository(), 10000);
    const auto button = window.findChild<QToolButton*>(QStringLiteral("accountButton"));
    QTRY_VERIFY(window.findChild<QToolButton*>(QStringLiteral("syncButton"))->isEnabled());
    button->menu()->popup(button->mapToGlobal(QPoint(0, button->height())));
    auto actions = window.findChildren<QAction*>(QStringLiteral("sshProfileAction"));
    QCOMPARE(actions.size(), 2);
    for (auto* action : actions) {
      if (action->data().toString() == QStringLiteral("other")) QVERIFY(!action->isEnabled());
    }
    for (auto* action : actions) {
      if (action->data().toString() == QStringLiteral("work")) {
        QVERIFY(action->isEnabled());
        action->trigger();
        break;
      }
    }
    QCOMPARE(controller.resolvedSshProfileId(local), QStringLiteral("work"));
    QCOMPARE(relay::RelayStore(config.storeFile).read().value(QStringLiteral("repositorySshProfiles")).toObject().value(QFileInfo(local).canonicalFilePath()).toString(), QStringLiteral("work"));
    button->menu()->hide();
    QVERIFY(button->text().contains(QStringLiteral("Work key")));
    QVERIFY(controller.state().accounts.isEmpty());
    window.findChild<QAction*>(QStringLiteral("useSshAgentAction"))->trigger();
    QVERIFY(controller.resolvedSshProfileId(local).isEmpty());
    controller.closeRepository();
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("useSshAgentAction"))->isEnabled());
  }

  void missingToolsBannerCanBeSelectedAndClearsOnRecovery() {
    QTemporaryDir profile;
    relay::RelayControllerConfig config;
    config.storeFile = profile.filePath(QStringLiteral("relay-data.json"));
    config.synchronizeAccountsOnStart = false;
    relay::RelayController controller(config);
    relay::MainWindow window(&controller);
    window.resize(820, 650);
    window.show();
    controller.start();
    const auto banner = window.findChild<QWidget*>(QStringLiteral("runtimeBanner"));
    const auto message = window.findChild<QLabel*>(QStringLiteral("runtimeMessage"));
    const auto retry = window.findChild<QPushButton*>(QStringLiteral("runtimeRetry"));
    QVERIFY(banner && message && retry);
    QVERIFY(!banner->isVisible());
    controller.runtimeIssuesChanged({QStringLiteral("GitHub CLI is unavailable. Install GitHub CLI with: winget install --id GitHub.cli -e")});
    QVERIFY(banner->isVisible());
    QVERIFY(message->textInteractionFlags().testFlag(Qt::TextSelectableByKeyboard));
    controller.busyChanged(QStringLiteral("runtime-check"), true);
    QVERIFY(!retry->isEnabled());
    controller.busyChanged(QStringLiteral("runtime-check"), false);
    QVERIFY(retry->isEnabled());
    QCoreApplication::processEvents();
    QVERIFY(message->geometry().right() < retry->geometry().left());
    const auto screenshots = qEnvironmentVariable("RELAY_SCREENSHOT_DIR");
    if (!screenshots.isEmpty()) {
      QDir().mkpath(screenshots);
      QVERIFY(window.grab().save(screenshots + QStringLiteral("/runtime-setup.png")));
    }
    controller.runtimeIssuesChanged({});
    QVERIFY(!banner->isVisible());
    QVERIFY(!controller.currentRepository());
  }

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
        if (edit->accessibleName() == QStringLiteral("Search commits")) return edit;
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
