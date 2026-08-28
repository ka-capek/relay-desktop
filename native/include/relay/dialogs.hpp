#pragma once

#include "relay/domain.hpp"

#include <QDialog>
#include <QList>
#include <QString>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTabWidget;

namespace relay {

enum class CloneSource { github, url };

struct CloneRequest {
  CloneSource source{CloneSource::github};
  QString remoteUrl;
  QString parentPath;
  QString repositoryName;
  QString accountId;
  QString sshProfileId;
  QString githubRepositoryId;
};

class CloneDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit CloneDialog(QWidget* parent = nullptr);

  void setAccounts(QList<Account> accounts, const QString& activeAccountId = {});
  void setSshProfiles(QList<SshProfile> profiles);
  void setGithubRepositories(QList<GitHubRepository> repositories,
                             qsizetype hiddenCount = 0,
                             const QString& error = {});
  void setRequest(const CloneRequest& request);
  void setSource(CloneSource source);
  void setParentPath(const QString& parentPath);

  [[nodiscard]] CloneRequest request() const;
  [[nodiscard]] CloneSource source() const noexcept;
  [[nodiscard]] QString selectedAccountId() const;
  [[nodiscard]] QString selectedSshProfileId() const;
  [[nodiscard]] bool isRequestValid() const;

 signals:
  void accountChanged(const QString& accountId);
  void refreshGithubRepositoriesRequested(const QString& accountId);

 private:
  void rebuildGithubRepositoryList();
  void chooseParentDirectory();
  void updateFromSelectedRepository();
  void updateValidation();
  void validateAndAccept();

  QList<Account> accounts_;
  QList<SshProfile> sshProfiles_;
  QList<GitHubRepository> githubRepositories_;
  qsizetype hiddenRepositoryCount_{};
  QString githubRepositoryError_;

  QComboBox* accountCombo_{};
  QTabWidget* sourceTabs_{};
  QLineEdit* githubSearch_{};
  QListWidget* githubList_{};
  QLabel* githubState_{};
  QLineEdit* urlEdit_{};
  QLineEdit* nameEdit_{};
  QLineEdit* parentEdit_{};
  QComboBox* sshCombo_{};
  QLabel* sshHint_{};
  QLabel* validationLabel_{};
  QPushButton* cloneButton_{};
};

class CommitEmailDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit CommitEmailDialog(QWidget* parent = nullptr);

  void setAccount(Account account, bool newlyConnected = false);
  void setChoices(QList<EmailChoice> choices);
  void setEmail(const QString& email);

  [[nodiscard]] const Account& account() const noexcept;
  [[nodiscard]] QString email() const;
  [[nodiscard]] bool isEmailValid() const;

 private:
  void rebuildChoices();
  void validateAndAccept();
  void updateValidation();

  Account account_;
  bool newlyConnected_{};
  QList<EmailChoice> choices_;
  QLabel* titleLabel_{};
  QLabel* descriptionLabel_{};
  QListWidget* choicesList_{};
  QLineEdit* emailEdit_{};
  QLabel* validationLabel_{};
  QPushButton* saveButton_{};
};

class AccountManagementDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit AccountManagementDialog(QWidget* parent = nullptr);

  void setAccounts(QList<Account> accounts);
  [[nodiscard]] const QList<Account>& accounts() const noexcept;
  [[nodiscard]] QString selectedAccountId() const;

 signals:
  void addAccountRequested();
  void editEmailRequested(const QString& accountId);
  void removeAccountRequested(const QString& accountId);

 private:
  void rebuildAccounts();
  void updateActions();

  QList<Account> accounts_;
  QListWidget* accountsList_{};
  QPushButton* editEmailButton_{};
  QPushButton* removeButton_{};
  QPushButton* addButton_{};
};

class SshProfileDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit SshProfileDialog(QWidget* parent = nullptr);

  void setProfile(const SshProfile& profile);
  [[nodiscard]] SshProfile profile() const;
  [[nodiscard]] bool isProfileValid() const;
  void setTesting(bool testing);
  void setTestResult(const SshTestResult& result);
  void clearTestResult();

 signals:
  void testRequested(const relay::SshProfile& profile);

 private:
  void chooseIdentityFile();
  void updateValidation();
  void validateAndAccept();

  QString profileId_;
  QLineEdit* labelEdit_{};
  QLineEdit* hostEdit_{};
  QLineEdit* userEdit_{};
  QLineEdit* portEdit_{};
  QLineEdit* identityFileEdit_{};
  QCheckBox* identitiesOnlyCheck_{};
  QLabel* validationLabel_{};
  QLabel* testResultLabel_{};
  QPushButton* testButton_{};
  QPushButton* saveButton_{};
  bool testing_{};
};

}  // namespace relay

Q_DECLARE_METATYPE(relay::CloneRequest)
Q_DECLARE_METATYPE(relay::SshProfile)
