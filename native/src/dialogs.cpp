#include "relay/dialogs.hpp"
#include <QSpinBox>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace relay {

SettingsDialog::SettingsDialog(Preferences preferences, QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Settings"));
  setObjectName(QStringLiteral("settingsDialog"));
  resize(500, 300);
  auto* layout = new QVBoxLayout(this);
  auto* tabs = new QTabWidget(this);
  auto* general = new QWidget(tabs);
  auto* form = new QFormLayout(general);
  refreshOnFocus_ = new QCheckBox(tr("Refresh repository when Relay becomes active"), general);
  refreshOnFocus_->setObjectName(QStringLiteral("refreshOnFocus"));
  refreshOnFocus_->setChecked(preferences.refreshOnFocus);
  form->addRow(refreshOnFocus_);
  diffFontSize_ = new QSpinBox(general);
  diffFontSize_->setObjectName(QStringLiteral("diffFontSize"));
  diffFontSize_->setRange(10, 24);
  diffFontSize_->setValue(preferences.diffFontSize);
  diffFontSize_->setSuffix(tr(" px"));
  form->addRow(tr("Diff text size"), diffFontSize_);
  graphHistory_ = new QCheckBox(tr("Show branch graph in History (all branches)"), general);
  graphHistory_->setObjectName(QStringLiteral("graphHistory"));
  graphHistory_->setChecked(preferences.graphHistory);
  form->addRow(graphHistory_);
  tabs->addTab(general, tr("General"));
  auto* git = new QWidget(tabs);
  auto* gitForm = new QFormLayout(git);
  auto* identityHelp = new QLabel(tr("Used when no GitHub account is selected. Leave blank to use this repository’s Git configuration."), git);
  identityHelp->setWordWrap(true);
  gitForm->addRow(identityHelp);
  commitName_ = new QLineEdit(preferences.commitName, git);
  commitName_->setObjectName(QStringLiteral("defaultCommitName"));
  commitEmail_ = new QLineEdit(preferences.commitEmail, git);
  commitEmail_->setObjectName(QStringLiteral("defaultCommitEmail"));
  gitForm->addRow(tr("Name"), commitName_);
  gitForm->addRow(tr("Email"), commitEmail_);
  tabs->addTab(git, tr("Git"));
  auto* accounts = new QWidget(tabs);
  auto* accountLayout = new QVBoxLayout(accounts);
  auto* explanation = new QLabel(tr("Connect multiple GitHub accounts and choose their commit emails. You can assign a different account to each repository in Repository settings."), accounts);
  explanation->setWordWrap(true);
  accountLayout->addWidget(explanation);
  auto* manage = new QPushButton(tr("Manage accounts…"), accounts);
  manage->setObjectName(QStringLiteral("settingsManageAccounts"));
  accountLayout->addWidget(manage, 0, Qt::AlignLeft);
  accountLayout->addStretch();
  connect(manage, &QPushButton::clicked, this, &SettingsDialog::manageAccountsRequested);
  tabs->addTab(accounts, tr("Accounts"));
  layout->addWidget(tabs);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

Preferences SettingsDialog::preferences() const {
  return {refreshOnFocus_->isChecked(), diffFontSize_->value(), commitName_->text().trimmed(),
          commitEmail_->text().trimmed(), graphHistory_->isChecked()};
}

namespace {

constexpr int githubIdRole = Qt::UserRole;
constexpr int githubUrlRole = Qt::UserRole + 1;
constexpr int githubNameRole = Qt::UserRole + 2;
constexpr int accountIdRole = Qt::UserRole;

QLabel* heading(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text, parent);
  label->setProperty("role", QStringLiteral("title"));
  label->setWordWrap(true);
  return label;
}

QLabel* explanatoryText(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text, parent);
  label->setProperty("role", QStringLiteral("body"));
  label->setWordWrap(true);
  return label;
}

QLabel* errorLabel(QWidget* parent) {
  auto* label = new QLabel(parent);
  label->setObjectName(QStringLiteral("validationError"));
  label->setStyleSheet(QStringLiteral("color: #a04e43; font-size: 11px;"));
  label->setWordWrap(true);
  label->setAccessibleName(QObject::tr("Validation error"));
  label->hide();
  return label;
}

QString repositoryNameFromUrl(QString remoteUrl) {
  while (remoteUrl.endsWith(QChar{u'/'}) || remoteUrl.endsWith(QChar{u'\\'})) {
    remoteUrl.chop(1);
  }
  const qsizetype slash = std::max(remoteUrl.lastIndexOf(QChar{u'/'}),
                                   remoteUrl.lastIndexOf(QChar{u':'}));
  QString name = slash >= 0 ? remoteUrl.mid(slash + 1) : remoteUrl;
  if (name.endsWith(QStringLiteral(".git"), Qt::CaseInsensitive)) name.chop(4);
  return name;
}

bool isSshRemote(const QString& remote) {
  const QString value = remote.trimmed();
  static const QRegularExpression explicitSsh{QStringLiteral(R"(^ssh://)"),
                                                QRegularExpression::CaseInsensitiveOption};
  static const QRegularExpression scpLike{
      QStringLiteral(R"(^[^@\s]+@[^@:\s/\\]+:)")};
  static const QRegularExpression windowsPath{QStringLiteral(R"(^[A-Za-z]:[\\/])")};
  return explicitSsh.match(value).hasMatch() ||
         (scpLike.match(value).hasMatch() && !windowsPath.match(value).hasMatch());
}

bool isGithubRemote(const QString& remote) {
  const QString value = remote.trimmed();
  static const QRegularExpression https{QStringLiteral(R"(^https://github\.com/)"),
                                         QRegularExpression::CaseInsensitiveOption};
  static const QRegularExpression ssh{
      QStringLiteral(R"(^(?:ssh://)?(?:[^@\s]+@)?github\.com[:/])"),
      QRegularExpression::CaseInsensitiveOption};
  return https.match(value).hasMatch() || ssh.match(value).hasMatch();
}

bool validEmail(const QString& value) {
  if (value.trimmed().isEmpty()) return true;
  static const QRegularExpression expression{
      QStringLiteral(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)")};
  return expression.match(value.trimmed()).hasMatch();
}

bool validSshHost(const QString& value) {
  const QString host = value.trimmed();
  if (host.isEmpty()) return false;
  static const QRegularExpression invalid{QStringLiteral(R"([\s/\\])")};
  return !invalid.match(host).hasMatch();
}

}  // namespace

CloneDialog::CloneDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Clone a repository"));
  setModal(true);
  resize(580, 650);
  setMinimumWidth(520);
  setAccessibleName(tr("Clone a repository"));

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(27, 27, 27, 22);
  layout->setSpacing(10);
  layout->addWidget(heading(tr("Clone a repository"), this));
  layout->addWidget(explanatoryText(
      tr("Choose a writable repository from a GitHub account, or paste any HTTPS or SSH URL."),
      this));

  auto* accountLabel = new QLabel(tr("GitHub account"), this);
  accountCombo_ = new QComboBox(this);
  accountCombo_->setObjectName(QStringLiteral("cloneAccount"));
  accountCombo_->setAccessibleName(tr("GitHub account used for cloning"));
  accountLabel->setBuddy(accountCombo_);
  layout->addWidget(accountLabel);
  layout->addWidget(accountCombo_);

  sourceTabs_ = new QTabWidget(this);
  sourceTabs_->setObjectName(QStringLiteral("cloneSourceTabs"));
  sourceTabs_->setAccessibleName(tr("Clone source"));

  auto* githubPage = new QWidget(sourceTabs_);
  auto* githubLayout = new QVBoxLayout(githubPage);
  githubLayout->setContentsMargins(0, 10, 0, 0);
  githubLayout->setSpacing(7);
  auto* searchRow = new QHBoxLayout;
  githubSearch_ = new QLineEdit(githubPage);
  githubSearch_->setObjectName(QStringLiteral("githubRepositorySearch"));
  githubSearch_->setPlaceholderText(tr("Filter repositories"));
  githubSearch_->setClearButtonEnabled(true);
  githubSearch_->setAccessibleName(tr("Filter GitHub repositories"));
  auto* refreshButton = new QPushButton(tr("Refresh"), githubPage);
  refreshButton->setObjectName(QStringLiteral("refreshGithubRepositories"));
  refreshButton->setAccessibleName(tr("Refresh writable GitHub repositories"));
  searchRow->addWidget(githubSearch_, 1);
  searchRow->addWidget(refreshButton);
  githubLayout->addLayout(searchRow);
  githubList_ = new QListWidget(githubPage);
  githubList_->setObjectName(QStringLiteral("githubRepositoryList"));
  githubList_->setAccessibleName(tr("Writable GitHub repositories"));
  githubList_->setSelectionMode(QAbstractItemView::SingleSelection);
  githubLayout->addWidget(githubList_, 1);
  githubState_ = new QLabel(githubPage);
  githubState_->setObjectName(QStringLiteral("githubRepositoryState"));
  githubState_->setProperty("role", QStringLiteral("meta"));
  githubState_->setWordWrap(true);
  githubLayout->addWidget(githubState_);
  sourceTabs_->addTab(githubPage, tr("GitHub repositories"));

  auto* urlPage = new QWidget(sourceTabs_);
  auto* urlLayout = new QFormLayout(urlPage);
  urlLayout->setContentsMargins(0, 14, 0, 4);
  urlEdit_ = new QLineEdit(urlPage);
  urlEdit_->setObjectName(QStringLiteral("cloneUrl"));
  urlEdit_->setPlaceholderText(QStringLiteral("https://github.com/owner/repository.git"));
  urlEdit_->setAccessibleName(tr("Repository URL"));
  urlLayout->addRow(tr("Repository URL"), urlEdit_);
  sourceTabs_->addTab(urlPage, tr("URL"));
  layout->addWidget(sourceTabs_, 1);

  auto* fields = new QFormLayout;
  fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  nameEdit_ = new QLineEdit(this);
  nameEdit_->setObjectName(QStringLiteral("cloneRepositoryName"));
  nameEdit_->setPlaceholderText(tr("repository"));
  nameEdit_->setAccessibleName(tr("Repository name"));
  fields->addRow(tr("Repository name"), nameEdit_);

  auto* pathWidget = new QWidget(this);
  auto* pathLayout = new QHBoxLayout(pathWidget);
  pathLayout->setContentsMargins(0, 0, 0, 0);
  pathLayout->setSpacing(7);
  parentEdit_ = new QLineEdit(pathWidget);
  parentEdit_->setObjectName(QStringLiteral("cloneParentPath"));
  parentEdit_->setPlaceholderText(tr("Choose a local folder"));
  parentEdit_->setAccessibleName(tr("Folder to clone into"));
  auto* chooseParent = new QPushButton(tr("Choose…"), pathWidget);
  chooseParent->setObjectName(QStringLiteral("chooseCloneParent"));
  chooseParent->setAccessibleName(tr("Choose a folder to clone into"));
  pathLayout->addWidget(parentEdit_, 1);
  pathLayout->addWidget(chooseParent);
  fields->addRow(tr("Clone into folder"), pathWidget);

  sshCombo_ = new QComboBox(this);
  sshCombo_->setObjectName(QStringLiteral("cloneSshProfile"));
  sshCombo_->setAccessibleName(tr("SSH identity used for cloning"));
  fields->addRow(tr("SSH identity"), sshCombo_);
  layout->addLayout(fields);
  sshHint_ = new QLabel(
      tr("SSH identities apply only to non-GitHub SSH URLs. Otherwise Relay uses the selected account or your normal SSH configuration."),
      this);
  sshHint_->setProperty("role", QStringLiteral("meta"));
  sshHint_->setWordWrap(true);
  layout->addWidget(sshHint_);

  validationLabel_ = errorLabel(this);
  layout->addWidget(validationLabel_);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  buttons->setObjectName(QStringLiteral("cloneButtons"));
  cloneButton_ = buttons->addButton(tr("Clone repository"), QDialogButtonBox::AcceptRole);
  cloneButton_->setObjectName(QStringLiteral("cloneSubmit"));
  cloneButton_->setProperty("kind", QStringLiteral("primary"));
  cloneButton_->setDefault(true);
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(cloneButton_, &QPushButton::clicked, this, &CloneDialog::validateAndAccept);
  connect(sourceTabs_, &QTabWidget::currentChanged, this, [this] {
    updateValidation();
    if (source() == CloneSource::github) githubSearch_->setFocus();
    else urlEdit_->setFocus();
  });
  connect(githubSearch_, &QLineEdit::textChanged, this,
          &CloneDialog::rebuildGithubRepositoryList);
  connect(githubList_, &QListWidget::itemSelectionChanged, this, [this] {
    updateFromSelectedRepository();
    updateValidation();
  });
  connect(urlEdit_, &QLineEdit::textChanged, this, [this](const QString& url) {
    nameEdit_->setText(repositoryNameFromUrl(url));
    updateValidation();
  });
  connect(nameEdit_, &QLineEdit::textChanged, this, &CloneDialog::updateValidation);
  connect(parentEdit_, &QLineEdit::textChanged, this, &CloneDialog::updateValidation);
  connect(accountCombo_, &QComboBox::currentIndexChanged, this, [this] {
    // A repository selected under one account must not remain cloneable
    // while the other account's list is still loading.
    setGithubRepositories({});
    emit accountChanged(selectedAccountId());
    updateValidation();
  });
  connect(refreshButton, &QPushButton::clicked, this,
          [this] { emit refreshGithubRepositoriesRequested(selectedAccountId()); });
  connect(chooseParent, &QPushButton::clicked, this, &CloneDialog::chooseParentDirectory);

  setSshProfiles({});
  updateValidation();
  githubSearch_->setFocus(Qt::OtherFocusReason);
}

void CloneDialog::setAccounts(QList<Account> accounts, const QString& activeAccountId) {
  accounts_ = std::move(accounts);
  const QSignalBlocker blocker{accountCombo_};
  accountCombo_->clear();
  for (const Account& account : accounts_) {
    accountCombo_->addItem(tr("@%1 — %2").arg(account.handle, account.email), account.id);
  }
  const int activeIndex = accountCombo_->findData(activeAccountId);
  if (activeIndex >= 0) accountCombo_->setCurrentIndex(activeIndex);
  sourceTabs_->setTabEnabled(0, !accounts_.isEmpty());
  if (accounts_.isEmpty() && source() == CloneSource::github) setSource(CloneSource::url);
  updateValidation();
}

void CloneDialog::setSshProfiles(QList<SshProfile> profiles) {
  sshProfiles_ = std::move(profiles);
  const QString selected = selectedSshProfileId();
  const QSignalBlocker blocker{sshCombo_};
  sshCombo_->clear();
  sshCombo_->addItem(tr("Use my SSH agent and ~/.ssh/config"), QString{});
  for (const SshProfile& profile : sshProfiles_) {
    sshCombo_->addItem(tr("%1 — %2").arg(profile.label, profile.host), profile.id);
  }
  const int selectedIndex = sshCombo_->findData(selected);
  if (selectedIndex >= 0) sshCombo_->setCurrentIndex(selectedIndex);
}

void CloneDialog::setGithubRepositories(QList<GitHubRepository> repositories,
                                        qsizetype hiddenCount,
                                        const QString& error) {
  githubRepositories_ = std::move(repositories);
  hiddenRepositoryCount_ = hiddenCount;
  githubRepositoryError_ = error;
  rebuildGithubRepositoryList();
  updateValidation();
}

void CloneDialog::setRequest(const CloneRequest& value) {
  setSource(value.source);
  const int accountIndex = accountCombo_->findData(value.accountId);
  if (accountIndex >= 0) accountCombo_->setCurrentIndex(accountIndex);
  parentEdit_->setText(value.parentPath);
  urlEdit_->setText(value.remoteUrl);
  nameEdit_->setText(value.repositoryName);
  const int sshIndex = sshCombo_->findData(value.sshProfileId);
  if (sshIndex >= 0) sshCombo_->setCurrentIndex(sshIndex);
  if (!value.githubRepositoryId.isEmpty()) {
    for (int row = 0; row < githubList_->count(); ++row) {
      QListWidgetItem* item = githubList_->item(row);
      if (item->data(githubIdRole).toString() == value.githubRepositoryId) {
        githubList_->setCurrentItem(item);
        break;
      }
    }
  }
  updateValidation();
}

void CloneDialog::setSource(CloneSource sourceValue) {
  sourceTabs_->setCurrentIndex(sourceValue == CloneSource::github ? 0 : 1);
}

void CloneDialog::setParentPath(const QString& parentPath) {
  parentEdit_->setText(parentPath);
}

CloneRequest CloneDialog::request() const {
  CloneRequest value;
  value.source = source();
  value.parentPath = parentEdit_->text().trimmed();
  value.repositoryName = nameEdit_->text().trimmed();
  value.accountId = selectedAccountId();
  const QListWidgetItem* selected = githubList_->currentItem();
  if (value.source == CloneSource::github && selected != nullptr) {
    value.remoteUrl = selected->data(githubUrlRole).toString();
    value.githubRepositoryId = selected->data(githubIdRole).toString();
  } else {
    value.remoteUrl = urlEdit_->text().trimmed();
  }
  if (isSshRemote(value.remoteUrl) && !isGithubRemote(value.remoteUrl)) {
    value.sshProfileId = selectedSshProfileId();
  }
  return value;
}

CloneSource CloneDialog::source() const noexcept {
  return sourceTabs_->currentIndex() == 0 ? CloneSource::github : CloneSource::url;
}

QString CloneDialog::selectedAccountId() const {
  return accountCombo_->currentData().toString();
}

QString CloneDialog::selectedSshProfileId() const {
  return sshCombo_->currentData().toString();
}

bool CloneDialog::isRequestValid() const {
  const CloneRequest value = request();
  if (value.remoteUrl.isEmpty() || value.parentPath.isEmpty() || value.repositoryName.isEmpty()) {
    return false;
  }
  return value.source != CloneSource::github ||
         (!value.accountId.isEmpty() && !value.githubRepositoryId.isEmpty());
}

void CloneDialog::rebuildGithubRepositoryList() {
  const QString selectedId = githubList_->currentItem() != nullptr
                                 ? githubList_->currentItem()->data(githubIdRole).toString()
                                 : QString{};
  githubList_->clear();
  const QString filter = githubSearch_->text().trimmed();
  for (const GitHubRepository& repository : githubRepositories_) {
    if (repository.archived) continue;
    const QString searchable = repository.fullName + QChar{u' '} + repository.description;
    if (!filter.isEmpty() && !searchable.contains(filter, Qt::CaseInsensitive)) continue;
    QString label = repository.fullName;
    if (repository.isPrivate) label.append(tr("  ·  Private"));
    auto* item = new QListWidgetItem(label, githubList_);
    item->setData(githubIdRole, repository.id);
    item->setData(githubUrlRole, repository.cloneUrl);
    item->setData(githubNameRole, repository.name);
    item->setToolTip(repository.description);
    item->setData(Qt::AccessibleTextRole,
                  tr("%1, %2").arg(repository.fullName,
                                      repository.isPrivate ? tr("private") : tr("public")));
    if (repository.id == selectedId) githubList_->setCurrentItem(item);
  }

  if (!githubRepositoryError_.isEmpty()) {
    githubState_->setText(githubRepositoryError_);
    githubState_->setStyleSheet(QStringLiteral("color: #a04e43;"));
  } else if (githubList_->count() == 0) {
    githubState_->setText(filter.isEmpty() ? tr("No writable repositories are available.")
                                           : tr("No repositories match that filter."));
    githubState_->setStyleSheet({});
  } else if (hiddenRepositoryCount_ > 0) {
    githubState_->setText(tr("%1 archived or non-writable repositories are hidden.")
                              .arg(hiddenRepositoryCount_));
    githubState_->setStyleSheet({});
  } else {
    githubState_->setText(tr("%1 writable repositories").arg(githubList_->count()));
    githubState_->setStyleSheet({});
  }
}

void CloneDialog::chooseParentDirectory() {
  const QString path = QFileDialog::getExistingDirectory(
      this, tr("Choose a folder to clone into"), parentEdit_->text());
  if (!path.isEmpty()) parentEdit_->setText(path);
}

void CloneDialog::updateFromSelectedRepository() {
  const QListWidgetItem* item = githubList_->currentItem();
  if (item != nullptr) nameEdit_->setText(item->data(githubNameRole).toString());
}

void CloneDialog::updateValidation() {
  const CloneRequest value = request();
  const bool nonGithubSsh = isSshRemote(value.remoteUrl) && !isGithubRemote(value.remoteUrl);
  sshCombo_->setEnabled(nonGithubSsh);
  sshHint_->setVisible(nonGithubSsh);
  cloneButton_->setEnabled(isRequestValid());
  if (validationLabel_->isVisible() && isRequestValid()) validationLabel_->hide();
}

void CloneDialog::validateAndAccept() {
  if (!isRequestValid()) {
    validationLabel_->setText(tr("Choose a repository or enter a URL, repository name, and local folder."));
    validationLabel_->show();
    return;
  }
  accept();
}

CommitEmailDialog::CommitEmailDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Choose a commit email"));
  setModal(true);
  setMinimumWidth(440);
  setAccessibleName(tr("Commit email settings"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(27, 27, 27, 22);
  layout->setSpacing(10);
  titleLabel_ = heading(tr("Choose a commit email"), this);
  descriptionLabel_ = explanatoryText(QString{}, this);
  layout->addWidget(titleLabel_);
  layout->addWidget(descriptionLabel_);
  choicesList_ = new QListWidget(this);
  choicesList_->setObjectName(QStringLiteral("emailChoices"));
  choicesList_->setAccessibleName(tr("Addresses GitHub knows for this account"));
  choicesList_->setMaximumHeight(190);
  layout->addWidget(choicesList_);
  auto* emailLabel = new QLabel(tr("Commit email"), this);
  emailEdit_ = new QLineEdit(this);
  emailEdit_->setObjectName(QStringLiteral("commitEmail"));
  emailEdit_->setPlaceholderText(tr("Leave blank to use GitHub noreply"));
  emailEdit_->setAccessibleName(tr("Commit email"));
  emailLabel->setBuddy(emailEdit_);
  layout->addWidget(emailLabel);
  layout->addWidget(emailEdit_);
  validationLabel_ = errorLabel(this);
  layout->addWidget(validationLabel_);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  saveButton_ = buttons->addButton(tr("Save email"), QDialogButtonBox::AcceptRole);
  saveButton_->setObjectName(QStringLiteral("saveCommitEmail"));
  saveButton_->setProperty("kind", QStringLiteral("primary"));
  saveButton_->setDefault(true);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(saveButton_, &QPushButton::clicked, this, &CommitEmailDialog::validateAndAccept);
  connect(emailEdit_, &QLineEdit::textChanged, this, &CommitEmailDialog::updateValidation);
  connect(choicesList_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
    emailEdit_->setText(item->data(Qt::UserRole).toString());
  });
  connect(choicesList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    emailEdit_->setText(item->data(Qt::UserRole).toString());
  });
  updateValidation();
  emailEdit_->setFocus(Qt::OtherFocusReason);
}

void CommitEmailDialog::setAccount(Account account, bool newlyConnected) {
  account_ = std::move(account);
  newlyConnected_ = newlyConnected;
  titleLabel_->setText(newlyConnected_ ? tr("Choose a commit email") : tr("Change commit email"));
  descriptionLabel_->setText(
      tr("This email is written into new commits made with @%1. Existing commits are not changed.%2")
          .arg(account_.handle,
               newlyConnected_ ? tr(" The account is already connected; skipping keeps GitHub's noreply address.")
                               : QString{}));
  setWindowTitle(titleLabel_->text());
}

void CommitEmailDialog::setChoices(QList<EmailChoice> choices) {
  choices_ = std::move(choices);
  rebuildChoices();
}

void CommitEmailDialog::setEmail(const QString& emailValue) {
  emailEdit_->setText(emailValue);
  for (int row = 0; row < choicesList_->count(); ++row) {
    QListWidgetItem* item = choicesList_->item(row);
    if (item->data(Qt::UserRole).toString().compare(emailValue.trimmed(), Qt::CaseInsensitive) == 0) {
      choicesList_->setCurrentItem(item);
      break;
    }
  }
}

const Account& CommitEmailDialog::account() const noexcept { return account_; }

QString CommitEmailDialog::email() const { return emailEdit_->text().trimmed(); }

bool CommitEmailDialog::isEmailValid() const { return validEmail(email()); }

void CommitEmailDialog::rebuildChoices() {
  choicesList_->clear();
  for (const EmailChoice& choice : choices_) {
    QString label = choice.email + QStringLiteral(" — ") + choice.label;
    if (choice.primary) label.append(tr(" · Primary"));
    auto* item = new QListWidgetItem(label, choicesList_);
    item->setData(Qt::UserRole, choice.email);
    item->setData(Qt::AccessibleTextRole,
                  tr("%1, %2").arg(choice.email, choice.label));
  }
  choicesList_->setVisible(!choices_.isEmpty());
}

void CommitEmailDialog::validateAndAccept() {
  if (!isEmailValid()) {
    validationLabel_->setText(tr("Enter a valid email address."));
    validationLabel_->show();
    emailEdit_->setFocus();
    emailEdit_->selectAll();
    return;
  }
  accept();
}

void CommitEmailDialog::updateValidation() {
  saveButton_->setEnabled(isEmailValid());
  if (isEmailValid()) validationLabel_->hide();
}

AccountManagementDialog::AccountManagementDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Connected accounts"));
  setModal(true);
  setMinimumSize(470, 330);
  setAccessibleName(tr("Manage connected GitHub accounts"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(27, 27, 27, 22);
  layout->setSpacing(10);
  layout->addWidget(heading(tr("Connected accounts"), this));
  layout->addWidget(explanatoryText(
      tr("Removing an account signs it out locally and removes its credential from this computer."),
      this));
  accountsList_ = new QListWidget(this);
  accountsList_->setObjectName(QStringLiteral("managedAccounts"));
  accountsList_->setAccessibleName(tr("Connected GitHub accounts"));
  accountsList_->setSelectionMode(QAbstractItemView::SingleSelection);
  layout->addWidget(accountsList_, 1);
  auto* actions = new QHBoxLayout;
  editEmailButton_ = new QPushButton(tr("Edit email"), this);
  editEmailButton_->setObjectName(QStringLiteral("editAccountEmail"));
  removeButton_ = new QPushButton(tr("Remove"), this);
  removeButton_->setObjectName(QStringLiteral("removeAccount"));
  removeButton_->setProperty("kind", QStringLiteral("danger"));
  addButton_ = new QPushButton(tr("Add account"), this);
  addButton_->setObjectName(QStringLiteral("addAccount"));
  addButton_->setProperty("kind", QStringLiteral("primary"));
  actions->addWidget(editEmailButton_);
  actions->addWidget(removeButton_);
  actions->addStretch();
  actions->addWidget(addButton_);
  layout->addLayout(actions);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(accountsList_, &QListWidget::itemSelectionChanged, this,
          &AccountManagementDialog::updateActions);
  connect(accountsList_, &QListWidget::itemDoubleClicked, this,
          [this] { if (!selectedAccountId().isEmpty()) emit editEmailRequested(selectedAccountId()); });
  connect(editEmailButton_, &QPushButton::clicked, this,
          [this] { if (!selectedAccountId().isEmpty()) emit editEmailRequested(selectedAccountId()); });
  connect(removeButton_, &QPushButton::clicked, this,
          [this] { if (!selectedAccountId().isEmpty()) emit removeAccountRequested(selectedAccountId()); });
  connect(addButton_, &QPushButton::clicked, this,
          &AccountManagementDialog::addAccountRequested);
  updateActions();
  accountsList_->setFocus(Qt::OtherFocusReason);
}

void AccountManagementDialog::setAccounts(QList<Account> accounts) {
  accounts_ = std::move(accounts);
  rebuildAccounts();
}

const QList<Account>& AccountManagementDialog::accounts() const noexcept {
  return accounts_;
}

QString AccountManagementDialog::selectedAccountId() const {
  const QListWidgetItem* item = accountsList_->currentItem();
  return item != nullptr ? item->data(accountIdRole).toString() : QString{};
}

void AccountManagementDialog::rebuildAccounts() {
  const QString selected = selectedAccountId();
  accountsList_->clear();
  for (const Account& account : accounts_) {
    auto* item = new QListWidgetItem(
        tr("%1  ·  @%2  ·  %3").arg(account.name, account.handle, account.email),
        accountsList_);
    item->setData(accountIdRole, account.id);
    item->setData(Qt::AccessibleTextRole,
                  tr("%1, GitHub account @%2, commit email %3")
                      .arg(account.name, account.handle, account.email));
    if (account.id == selected) accountsList_->setCurrentItem(item);
  }
  if (accountsList_->currentItem() == nullptr && accountsList_->count() > 0) {
    accountsList_->setCurrentRow(0);
  }
  addButton_->setDefault(accounts_.isEmpty());
  updateActions();
}

void AccountManagementDialog::updateActions() {
  const bool selected = !selectedAccountId().isEmpty();
  editEmailButton_->setEnabled(selected);
  removeButton_->setEnabled(selected);
}

SshProfileDialog::SshProfileDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Add SSH identity"));
  setModal(true);
  setMinimumWidth(480);
  setAccessibleName(tr("SSH identity settings"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(27, 27, 27, 22);
  layout->setSpacing(10);
  layout->addWidget(heading(tr("SSH identity"), this));
  layout->addWidget(explanatoryText(
      tr("For Git hosts other than GitHub.com. Leave the key blank to use your SSH agent and ~/.ssh/config."),
      this));
  auto* form = new QFormLayout;
  labelEdit_ = new QLineEdit(this);
  labelEdit_->setObjectName(QStringLiteral("sshLabel"));
  labelEdit_->setPlaceholderText(tr("Work GitLab"));
  labelEdit_->setAccessibleName(tr("SSH identity name"));
  form->addRow(tr("Name"), labelEdit_);
  hostEdit_ = new QLineEdit(this);
  hostEdit_->setObjectName(QStringLiteral("sshHost"));
  hostEdit_->setPlaceholderText(QStringLiteral("gitlab.example.com"));
  hostEdit_->setAccessibleName(tr("SSH host, required"));
  form->addRow(tr("Host (required)"), hostEdit_);
  userEdit_ = new QLineEdit(this);
  userEdit_->setObjectName(QStringLiteral("sshUser"));
  userEdit_->setPlaceholderText(QStringLiteral("git"));
  userEdit_->setAccessibleName(tr("SSH user"));
  form->addRow(tr("User"), userEdit_);
  portEdit_ = new QLineEdit(this);
  portEdit_->setObjectName(QStringLiteral("sshPort"));
  portEdit_->setPlaceholderText(QStringLiteral("22"));
  portEdit_->setValidator(new QIntValidator(1, 65535, portEdit_));
  portEdit_->setAccessibleName(tr("SSH port"));
  form->addRow(tr("Port"), portEdit_);

  auto* identityWidget = new QWidget(this);
  auto* identityLayout = new QHBoxLayout(identityWidget);
  identityLayout->setContentsMargins(0, 0, 0, 0);
  identityLayout->setSpacing(7);
  identityFileEdit_ = new QLineEdit(identityWidget);
  identityFileEdit_->setObjectName(QStringLiteral("sshIdentityFile"));
  identityFileEdit_->setPlaceholderText(QStringLiteral("~/.ssh/id_ed25519"));
  identityFileEdit_->setAccessibleName(tr("Private key file"));
  auto* chooseIdentity = new QPushButton(tr("Choose…"), identityWidget);
  chooseIdentity->setObjectName(QStringLiteral("chooseSshIdentity"));
  chooseIdentity->setAccessibleName(tr("Choose a private key file"));
  identityLayout->addWidget(identityFileEdit_, 1);
  identityLayout->addWidget(chooseIdentity);
  form->addRow(tr("Private key file"), identityWidget);
  layout->addLayout(form);

  identitiesOnlyCheck_ = new QCheckBox(tr("Offer only this key"), this);
  identitiesOnlyCheck_->setObjectName(QStringLiteral("sshIdentitiesOnly"));
  identitiesOnlyCheck_->setAccessibleDescription(
      tr("Recommended when several identities share one host"));
  identitiesOnlyCheck_->setChecked(true);
  layout->addWidget(identitiesOnlyCheck_);
  testResultLabel_ = new QLabel(this);
  testResultLabel_->setObjectName(QStringLiteral("sshTestResult"));
  testResultLabel_->setWordWrap(true);
  testResultLabel_->setAccessibleName(tr("SSH connection test result"));
  testResultLabel_->hide();
  layout->addWidget(testResultLabel_);
  validationLabel_ = errorLabel(this);
  layout->addWidget(validationLabel_);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  testButton_ = buttons->addButton(tr("Test connection"), QDialogButtonBox::ActionRole);
  testButton_->setObjectName(QStringLiteral("testSshProfile"));
  saveButton_ = buttons->addButton(tr("Save identity"), QDialogButtonBox::AcceptRole);
  saveButton_->setObjectName(QStringLiteral("saveSshProfile"));
  saveButton_->setProperty("kind", QStringLiteral("primary"));
  saveButton_->setDefault(true);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(saveButton_, &QPushButton::clicked, this, &SshProfileDialog::validateAndAccept);
  connect(testButton_, &QPushButton::clicked, this, [this] {
    if (!isProfileValid()) {
      updateValidation();
      validationLabel_->setText(tr("Enter a valid SSH host and port before testing."));
      validationLabel_->show();
      return;
    }
    emit testRequested(profile());
  });
  connect(chooseIdentity, &QPushButton::clicked, this, &SshProfileDialog::chooseIdentityFile);
  connect(hostEdit_, &QLineEdit::textChanged, this, &SshProfileDialog::updateValidation);
  connect(portEdit_, &QLineEdit::textChanged, this, &SshProfileDialog::updateValidation);
  updateValidation();
  labelEdit_->setFocus(Qt::OtherFocusReason);
}

void SshProfileDialog::setProfile(const SshProfile& value) {
  profileId_ = value.id;
  labelEdit_->setText(value.label);
  hostEdit_->setText(value.host);
  userEdit_->setText(value.user);
  portEdit_->setText(value.port ? QString::number(*value.port) : QString{});
  identityFileEdit_->setText(value.identityFile);
  identitiesOnlyCheck_->setChecked(value.identitiesOnly);
  setWindowTitle(profileId_.isEmpty() ? tr("Add SSH identity") : tr("Edit SSH identity"));
  updateValidation();
}

SshProfile SshProfileDialog::profile() const {
  SshProfile value;
  value.id = profileId_;
  value.host = hostEdit_->text().trimmed().toLower();
  value.label = labelEdit_->text().trimmed();
  if (value.label.isEmpty()) value.label = value.host;
  value.user = userEdit_->text().trimmed();
  const QString port = portEdit_->text().trimmed();
  if (!port.isEmpty()) value.port = static_cast<quint16>(port.toUShort());
  value.identityFile = identityFileEdit_->text().trimmed();
  value.identitiesOnly = identitiesOnlyCheck_->isChecked();
  return value;
}

bool SshProfileDialog::isProfileValid() const {
  if (!validSshHost(hostEdit_->text())) return false;
  const QString port = portEdit_->text().trimmed();
  if (port.isEmpty()) return true;
  bool ok = false;
  const uint number = port.toUInt(&ok);
  return ok && number > 0 && number < 65536;
}

void SshProfileDialog::setTesting(bool testing) {
  testing_ = testing;
  testButton_->setEnabled(!testing && isProfileValid());
  saveButton_->setEnabled(!testing && isProfileValid());
  testButton_->setText(testing ? tr("Testing connection…") : tr("Test connection"));
}

void SshProfileDialog::setTestResult(const SshTestResult& result) {
  testResultLabel_->setText(result.message);
  testResultLabel_->setStyleSheet(result.ok ? QStringLiteral("color: #2c6248;")
                                            : QStringLiteral("color: #8f483d;"));
  testResultLabel_->show();
}

void SshProfileDialog::clearTestResult() {
  testResultLabel_->clear();
  testResultLabel_->hide();
}

void SshProfileDialog::chooseIdentityFile() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Choose a private key file"), identityFileEdit_->text());
  if (!path.isEmpty()) identityFileEdit_->setText(path);
}

void SshProfileDialog::updateValidation() {
  const bool valid = isProfileValid();
  saveButton_->setEnabled(valid && !testing_);
  testButton_->setEnabled(valid && !testing_);
  if (valid) validationLabel_->hide();
}

void SshProfileDialog::validateAndAccept() {
  if (!isProfileValid()) {
    validationLabel_->setText(tr("Enter a host name without spaces or slashes, and a port from 1 to 65535."));
    validationLabel_->show();
    hostEdit_->setFocus();
    return;
  }
  accept();
}

}  // namespace relay
