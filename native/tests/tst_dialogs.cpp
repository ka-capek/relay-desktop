#include "relay/dialogs.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>

namespace {

relay::Account account(QString id, QString handle, QString email) {
  relay::Account value;
  value.id = std::move(id);
  value.name = handle;
  value.handle = std::move(handle);
  value.email = std::move(email);
  value.status = QStringLiteral("Signed in");
  return value;
}

relay::GitHubRepository repository(QString id, QString fullName, QString cloneUrl,
                                   bool isPrivate = false) {
  relay::GitHubRepository value;
  value.id = std::move(id);
  value.fullName = std::move(fullName);
  value.name = value.fullName.section(QChar{u'/'}, -1);
  value.owner = value.fullName.section(QChar{u'/'}, 0, 0);
  value.cloneUrl = std::move(cloneUrl);
  value.isPrivate = isPrivate;
  return value;
}

relay::SshProfile sshProfile() {
  relay::SshProfile value;
  value.id = QStringLiteral("ssh-work");
  value.label = QStringLiteral("Work GitLab");
  value.host = QStringLiteral("gitlab.example.com");
  value.user = QStringLiteral("git");
  value.port = 2222;
  value.identityFile = QStringLiteral("/keys/work_ed25519");
  value.identitiesOnly = true;
  return value;
}

}  // namespace

class DialogsTest final : public QObject {
  Q_OBJECT

 private slots:
  void settingsExposeAndReturnEditablePreferences() {
    relay::SettingsDialog dialog({true, 12});
    auto* refresh = dialog.findChild<QCheckBox*>(QStringLiteral("refreshOnFocus"));
    auto* size = dialog.findChild<QSpinBox*>(QStringLiteral("diffFontSize"));
    QVERIFY(refresh && size);
    QVERIFY(refresh->isChecked());
    QCOMPARE(size->value(), 12);
    refresh->setChecked(false);
    size->setValue(18);
    QVERIFY(!dialog.preferences().refreshOnFocus);
    QCOMPARE(dialog.preferences().diffFontSize, 18);
    QSignalSpy accounts(&dialog, &relay::SettingsDialog::manageAccountsRequested);
    auto* manage = dialog.findChild<QPushButton*>(QStringLiteral("settingsManageAccounts"));
    QVERIFY(manage);
    manage->click();
    QCOMPARE(accounts.size(), 1);
  }

  void switchingCloneAccountClearsThePreviousSelectionImmediately() {
    relay::CloneDialog dialog;
    dialog.setAccounts({account(QStringLiteral("one"), QStringLiteral("alice"),
                                QStringLiteral("alice@example.com")),
                        account(QStringLiteral("two"), QStringLiteral("bob"),
                                QStringLiteral("bob@example.com"))}, QStringLiteral("one"));
    dialog.setGithubRepositories({repository(QStringLiteral("alpha"),
        QStringLiteral("alice/alpha"), QStringLiteral("https://github.com/alice/alpha.git"))});
    dialog.setParentPath(QStringLiteral("/repos"));
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("githubRepositoryList"));
    auto* accounts = dialog.findChild<QComboBox*>(QStringLiteral("cloneAccount"));
    auto* submit = dialog.findChild<QPushButton*>(QStringLiteral("cloneSubmit"));
    QVERIFY(list && accounts && submit);
    list->setCurrentRow(0);
    QVERIFY(submit->isEnabled());
    bool clearedBeforeRequest = false;
    connect(&dialog, &relay::CloneDialog::accountChanged, &dialog, [&](const QString&) {
      clearedBeforeRequest = list->count() == 0 && !dialog.isRequestValid();
    });
    accounts->setCurrentIndex(1);
    QVERIFY(clearedBeforeRequest);
    QVERIFY(!submit->isEnabled());
    QCOMPARE(dialog.selectedAccountId(), QStringLiteral("two"));
    QVERIFY(dialog.request().githubRepositoryId.isEmpty());
    dialog.setGithubRepositories({repository(QStringLiteral("beta"),
        QStringLiteral("bob/beta"), QStringLiteral("https://github.com/bob/beta.git"))});
    list->setCurrentRow(0);
    QVERIFY(submit->isEnabled());
    QCOMPARE(dialog.request().remoteUrl, QStringLiteral("https://github.com/bob/beta.git"));
  }

  void cloneDialogFiltersWritableRepositoriesAndReturnsTypedRequest() {
    relay::CloneDialog dialog;
    dialog.setAccounts({account(QStringLiteral("one"), QStringLiteral("alice"),
                                QStringLiteral("alice@example.com"))},
                       QStringLiteral("one"));
    dialog.setGithubRepositories({
        repository(QStringLiteral("alpha"), QStringLiteral("alice/alpha"),
                   QStringLiteral("https://github.com/alice/alpha.git")),
        repository(QStringLiteral("beta"), QStringLiteral("alice/beta"),
                   QStringLiteral("https://github.com/alice/beta.git"), true),
    });
    dialog.setParentPath(QStringLiteral("/repos"));
    dialog.show();

    auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("githubRepositorySearch"));
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("githubRepositoryList"));
    auto* submit = dialog.findChild<QPushButton*>(QStringLiteral("cloneSubmit"));
    QVERIFY(search != nullptr);
    QVERIFY(list != nullptr);
    QVERIFY(submit != nullptr);
    QTRY_VERIFY(search->hasFocus());
    QTest::keyClicks(search, QStringLiteral("beta"));
    QCOMPARE(list->count(), 1);
    list->setCurrentRow(0);
    QTRY_VERIFY(submit->isEnabled());
    QVERIFY(submit->isDefault());

    QSignalSpy accepted{&dialog, &QDialog::accepted};
    QTest::mouseClick(submit, Qt::LeftButton);
    QCOMPARE(accepted.count(), 1);
    const relay::CloneRequest request = dialog.request();
    QCOMPARE(static_cast<int>(request.source), static_cast<int>(relay::CloneSource::github));
    QCOMPARE(request.githubRepositoryId, QStringLiteral("beta"));
    QCOMPARE(request.remoteUrl, QStringLiteral("https://github.com/alice/beta.git"));
    QCOMPARE(request.repositoryName, QStringLiteral("beta"));
    QCOMPARE(request.parentPath, QStringLiteral("/repos"));
    QCOMPARE(request.accountId, QStringLiteral("one"));
  }

  void cloneDialogSupportsArbitrarySshUrlAndIdentity() {
    relay::CloneDialog dialog;
    dialog.setAccounts({account(QStringLiteral("one"), QStringLiteral("alice"),
                                QStringLiteral("alice@example.com"))});
    dialog.setSshProfiles({sshProfile()});
    relay::CloneRequest input;
    input.source = relay::CloneSource::url;
    input.remoteUrl = QStringLiteral("git@gitlab.example.com:team/project.git");
    input.parentPath = QStringLiteral("/work");
    input.repositoryName = QStringLiteral("project");
    input.accountId = QStringLiteral("one");
    input.sshProfileId = QStringLiteral("ssh-work");
    dialog.setRequest(input);

    QVERIFY(dialog.isRequestValid());
    const relay::CloneRequest output = dialog.request();
    QCOMPARE(static_cast<int>(output.source), static_cast<int>(relay::CloneSource::url));
    QCOMPARE(output.sshProfileId, QStringLiteral("ssh-work"));
    QCOMPARE(output.remoteUrl, input.remoteUrl);
    auto* ssh = dialog.findChild<QComboBox*>(QStringLiteral("cloneSshProfile"));
    QVERIFY(ssh != nullptr);
    QVERIFY(ssh->isEnabled());
  }

  void cloneDialogRejectsIncompleteRequest() {
    relay::CloneDialog dialog;
    dialog.setAccounts({});
    dialog.setSource(relay::CloneSource::url);
    auto* submit = dialog.findChild<QPushButton*>(QStringLiteral("cloneSubmit"));
    QVERIFY(submit != nullptr);
    QVERIFY(!dialog.isRequestValid());
    QVERIFY(!submit->isEnabled());
  }

  void commitEmailDialogHandlesChoiceFreeTextAndValidation() {
    relay::CommitEmailDialog dialog;
    dialog.setAccount(account(QStringLiteral("one"), QStringLiteral("alice"),
                              QStringLiteral("1+alice@users.noreply.github.com")), true);
    dialog.setChoices({
        {QStringLiteral("1+alice@users.noreply.github.com"), QStringLiteral("GitHub noreply"),
         false, true},
        {QStringLiteral("alice@example.com"), QStringLiteral("Primary"), true, false},
    });
    dialog.show();
    auto* edit = dialog.findChild<QLineEdit*>(QStringLiteral("commitEmail"));
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("emailChoices"));
    auto* save = dialog.findChild<QPushButton*>(QStringLiteral("saveCommitEmail"));
    QVERIFY(edit != nullptr);
    QVERIFY(list != nullptr);
    QVERIFY(save != nullptr);
    QTRY_VERIFY(edit->hasFocus());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(1)).center());
    QCOMPARE(dialog.email(), QStringLiteral("alice@example.com"));

    edit->setText(QStringLiteral("not-an-email"));
    QVERIFY(!dialog.isEmailValid());
    QVERIFY(!save->isEnabled());
    edit->setText(QStringLiteral("custom@work.test"));
    QVERIFY(dialog.isEmailValid());
    QVERIFY(save->isEnabled());
    QVERIFY(save->isDefault());
    QSignalSpy accepted{&dialog, &QDialog::accepted};
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(accepted.count(), 1);
    QCOMPARE(dialog.email(), QStringLiteral("custom@work.test"));
  }

  void blankCommitEmailIsValidNoreplyRequest() {
    relay::CommitEmailDialog dialog;
    dialog.setEmail(QStringLiteral("   "));
    QVERIFY(dialog.isEmailValid());
    QCOMPARE(dialog.email(), QString{});
  }

  void accountManagementEmitsTypedAccountActions() {
    relay::AccountManagementDialog dialog;
    dialog.setAccounts({account(QStringLiteral("one"), QStringLiteral("alice"),
                                QStringLiteral("alice@example.com")),
                        account(QStringLiteral("two"), QStringLiteral("bob"),
                                QStringLiteral("bob@example.com"))});
    dialog.show();
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("managedAccounts"));
    auto* edit = dialog.findChild<QPushButton*>(QStringLiteral("editAccountEmail"));
    auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("removeAccount"));
    auto* add = dialog.findChild<QPushButton*>(QStringLiteral("addAccount"));
    QVERIFY(list != nullptr);
    QVERIFY(edit != nullptr);
    QVERIFY(remove != nullptr);
    QVERIFY(add != nullptr);
    list->setCurrentRow(1);
    QCOMPARE(dialog.selectedAccountId(), QStringLiteral("two"));

    QSignalSpy edited{&dialog, &relay::AccountManagementDialog::editEmailRequested};
    QSignalSpy removed{&dialog, &relay::AccountManagementDialog::removeAccountRequested};
    QSignalSpy added{&dialog, &relay::AccountManagementDialog::addAccountRequested};
    QTest::mouseClick(edit, Qt::LeftButton);
    QTest::mouseClick(remove, Qt::LeftButton);
    QTest::mouseClick(add, Qt::LeftButton);
    QCOMPARE(edited.count(), 1);
    QCOMPARE(edited.first().first().toString(), QStringLiteral("two"));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.first().first().toString(), QStringLiteral("two"));
    QCOMPARE(added.count(), 1);
  }

  void sshProfileDialogRoundTripsAndTestsProfile() {
    relay::SshProfileDialog dialog;
    const relay::SshProfile input = sshProfile();
    dialog.setProfile(input);
    dialog.show();
    const relay::SshProfile output = dialog.profile();
    QCOMPARE(output.id, input.id);
    QCOMPARE(output.label, input.label);
    QCOMPARE(output.host, input.host);
    QCOMPARE(output.user, input.user);
    QCOMPARE(output.port, input.port);
    QCOMPARE(output.identityFile, input.identityFile);
    QCOMPARE(output.identitiesOnly, input.identitiesOnly);
    QVERIFY(dialog.isProfileValid());

    auto* testButton = dialog.findChild<QPushButton*>(QStringLiteral("testSshProfile"));
    auto* saveButton = dialog.findChild<QPushButton*>(QStringLiteral("saveSshProfile"));
    QVERIFY(testButton != nullptr);
    QVERIFY(saveButton != nullptr);
    QVERIFY(saveButton->isDefault());
    dialog.setTesting(true);
    QVERIFY(!testButton->isEnabled());
    QVERIFY(!saveButton->isEnabled());
    dialog.setTesting(false);
    QVERIFY(testButton->isEnabled());
    QVERIFY(saveButton->isEnabled());
    QSignalSpy requested{&dialog, &relay::SshProfileDialog::testRequested};
    QTest::mouseClick(testButton, Qt::LeftButton);
    QCOMPARE(requested.count(), 1);
    QCOMPARE(qvariant_cast<relay::SshProfile>(requested.first().first()).host,
             QStringLiteral("gitlab.example.com"));

    dialog.setTestResult({true, QStringLiteral("Host accepted the key.")});
    auto* result = dialog.findChild<QLabel*>(QStringLiteral("sshTestResult"));
    QVERIFY(result != nullptr);
    QVERIFY(result->isVisible());
    QCOMPARE(result->text(), QStringLiteral("Host accepted the key."));
  }

  void sshProfileValidationRejectsInvalidHostAndPort() {
    relay::SshProfileDialog dialog;
    auto* host = dialog.findChild<QLineEdit*>(QStringLiteral("sshHost"));
    auto* port = dialog.findChild<QLineEdit*>(QStringLiteral("sshPort"));
    auto* save = dialog.findChild<QPushButton*>(QStringLiteral("saveSshProfile"));
    QVERIFY(host != nullptr);
    QVERIFY(port != nullptr);
    QVERIFY(save != nullptr);
    host->setText(QStringLiteral("bad host/name"));
    port->setText(QStringLiteral("22"));
    QVERIFY(!dialog.isProfileValid());
    QVERIFY(!save->isEnabled());
    host->setText(QStringLiteral("gitlab.example.com"));
    port->setText(QStringLiteral("65535"));
    QVERIFY(dialog.isProfileValid());
    QVERIFY(save->isEnabled());
  }
};

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  }
  QApplication application{argc, argv};
  DialogsTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "tst_dialogs.moc"
