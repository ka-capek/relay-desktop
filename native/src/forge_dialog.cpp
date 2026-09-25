#include "relay/forge_dialog.hpp"
#include "relay/forge_service.hpp"
#include "relay/relay_controller.hpp"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QStringListModel>
#include <QUrl>
#include <QVBoxLayout>

namespace relay {
ForgeDialog::ForgeDialog(RelayController* controller, QWidget* parent)
    : QDialog(parent), controller_(controller) {
  setWindowTitle(tr("Gitea / GitLab accounts and repositories"));
  setObjectName(QStringLiteral("forgeDialog"));
  resize(760, 660);
  auto* layout = new QVBoxLayout(this);
  auto* intro = new QLabel(tr("Connect Gitea, Forgejo or GitLab to browse repositories you can access. "
      "Use SSH identities for Git operations on these or other Git servers."), this);
  intro->setWordWrap(true);
  layout->addWidget(intro);
  auto* form = new QFormLayout;
  kind_ = new QComboBox(this);
  kind_->setObjectName(QStringLiteral("forgeKind"));
  kind_->addItem(tr("Gitea / Forgejo"), static_cast<int>(ForgeKind::gitea));
  kind_->addItem(tr("GitLab"), static_cast<int>(ForgeKind::gitlab));
  server_ = new QLineEdit(this);
  server_->setObjectName(QStringLiteral("forgeServer"));
  server_->setPlaceholderText(QStringLiteral("https://git.example.com"));
  token_ = new QLineEdit(this);
  token_->setObjectName(QStringLiteral("forgeToken"));
  token_->setEchoMode(QLineEdit::Password);
  token_->setPlaceholderText(tr("API token — saved only in your system credential store"));
  form->addRow(tr("Service"), kind_);
  form->addRow(tr("Server URL"), server_);
  form->addRow(tr("API token"), token_);
  layout->addLayout(form);
  auto* explanation = new QLabel(tr("Sign in in your browser and create a read-only API token. "
      "Gitea: read:user and read:repository; GitLab: read_api. These servers require a registered "
      "OAuth application for direct browser authorization; Relay uses a token here."), this);
  explanation->setWordWrap(true);
  layout->addWidget(explanation);
  auto* connectRow = new QHBoxLayout;
  auto* browser = new QPushButton(tr("Open token settings in browser"), this);
  browser->setObjectName(QStringLiteral("forgeTokenSettings"));
  connect_ = new QPushButton(tr("Connect account"), this);
  connect_->setObjectName(QStringLiteral("forgeConnect"));
  connectRow->addWidget(browser);
  connectRow->addStretch();
  connectRow->addWidget(connect_);
  auto* cleanup = new QPushButton(tr("Retry credential cleanup"), this);
  cleanup->setObjectName(QStringLiteral("forgeCleanup"));
  cleanup->setVisible(!controller_->state().forgeCredentialCleanup.isEmpty());
  connect(cleanup, &QPushButton::clicked, controller_, &RelayController::retryForgeCredentialCleanup);
  connect(controller_, &RelayController::stateChanged, cleanup, [cleanup](const AppState& state) {
    cleanup->setVisible(!state.forgeCredentialCleanup.isEmpty());
  });
  connectRow->addWidget(cleanup);
  layout->addLayout(connectRow);
  message_ = new QLabel(this);
  message_->setObjectName(QStringLiteral("forgeMessage"));
  message_->setWordWrap(true);
  message_->setTextFormat(Qt::PlainText);
  message_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(message_);
  auto* accountRow = new QHBoxLayout;
  accounts_ = new QComboBox(this);
  accounts_->setObjectName(QStringLiteral("forgeAccounts"));
  accounts_->setMinimumContentsLength(25);
  accounts_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  remove_ = new QPushButton(tr("Disconnect"), this);
  refresh_ = new QPushButton(tr("Refresh repositories"), this);
  refresh_->setObjectName(QStringLiteral("forgeRefresh"));
  accountRow->addWidget(accounts_, 1);
  accountRow->addWidget(refresh_);
  accountRow->addWidget(remove_);
  layout->addLayout(accountRow);
  auto* filter = new QLineEdit(this);
  filter->setObjectName(QStringLiteral("forgeFilter"));
  filter->setPlaceholderText(tr("Filter repositories"));
  layout->addWidget(filter);
  model_ = new QStringListModel(this);
  proxy_ = new QSortFilterProxyModel(this);
  proxy_->setSourceModel(model_);
  proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
  repositories_ = new QListView(this);
  repositories_->setObjectName(QStringLiteral("forgeRepositories"));
  repositories_->setModel(proxy_);
  repositories_->setUniformItemSizes(true);
  repositories_->setAccessibleName(tr("Repositories available to the selected account"));
  layout->addWidget(repositories_, 1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  open_ = buttons->addButton(tr("Open in browser"), QDialogButtonBox::ActionRole);
  clone_ = buttons->addButton(tr("Clone with SSH…"), QDialogButtonBox::ActionRole);
  clone_->setObjectName(QStringLiteral("forgeClone"));
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(filter, &QLineEdit::textChanged, proxy_, &QSortFilterProxyModel::setFilterFixedString);
  connect(repositories_->selectionModel(), &QItemSelectionModel::currentChanged,
      this, [this] { updateSelection(); });
  connect(accounts_, &QComboBox::currentIndexChanged, this, [this] { loadRepositories(); });
  connect(refresh_, &QPushButton::clicked, this, &ForgeDialog::loadRepositories);
  connect(remove_, &QPushButton::clicked, this, [this] {
    controller_->removeForgeAccount(accounts_->currentData().toString());
  });
  connect(browser, &QPushButton::clicked, this, [this] {
    try {
      const auto url = ForgeService::tokenSettingsUrl(static_cast<ForgeKind>(kind_->currentData().toInt()), server_->text());
      QDesktopServices::openUrl(url);
    } catch (const std::exception& error) { message_->setText(QString::fromUtf8(error.what())); }
  });
  connect(kind_, &QComboBox::currentIndexChanged, this, [this] {
    if (server_->text().isEmpty() && kind_->currentData().toInt() == static_cast<int>(ForgeKind::gitlab))
      server_->setText(QStringLiteral("https://gitlab.com"));
  });
  connect(connect_, &QPushButton::clicked, this, [this] {
    auto token = token_->text();
    token_->clear();
    controller_->connectForgeAccount(static_cast<ForgeKind>(kind_->currentData().toInt()), server_->text(), std::move(token));
  });
  connect(controller_, &RelayController::stateChanged, this, &ForgeDialog::updateAccounts);
  connect(controller_, &RelayController::forgeAccountConnected, this, [this](const QString& id) {
    const auto index = accounts_->findData(id);
    if (index == accounts_->currentIndex()) loadRepositories();
    else accounts_->setCurrentIndex(index);
  });
  connect(controller_, &RelayController::forgeRepositoriesReady, this,
      [this](const QString& accountId, QList<ForgeRepository> entries) {
        if (accountId != accounts_->currentData().toString()) return;
        entries_ = std::move(entries);
        QStringList names;
        for (const auto& entry : entries_) {
          auto text = entry.fullName;
          if (entry.isPrivate) text += tr("  (private)");
          if (entry.archived) text += tr("  (archived)");
          names.append(text);
        }
        model_->setStringList(names);
        message_->setText(entries_.size() == 1 ? tr("1 repository") : tr("%1 repositories").arg(entries_.size()));
        updateSelection();
      });
  connect(controller_, &RelayController::operationFailed, this,
      [this](const QString& operation, const QString& message) {
        if (operation.contains(QStringLiteral("forge"))) message_->setText(message);
      });
  connect(controller_, &RelayController::busyChanged, this,
      [this](const QString& operation, bool busy) {
        if (operation == QStringLiteral("connect-forge-account")) {
          connect_->setEnabled(!busy);
          if (busy) { clearRepositories(); message_->setText(tr("Connecting account…")); }
        }
      });
  connect(open_, &QPushButton::clicked, this, [this] {
    const auto index = proxy_->mapToSource(repositories_->currentIndex());
    if (!index.isValid()) return;
    const QUrl url(entries_.at(index.row()).webUrl);
    if (url.scheme() == QStringLiteral("https") && url.userInfo().isEmpty()) QDesktopServices::openUrl(url);
  });
  connect(clone_, &QPushButton::clicked, this, [this] {
    const auto index = proxy_->mapToSource(repositories_->currentIndex());
    if (!index.isValid()) return;
    cloneUrl_ = entries_.at(index.row()).sshUrl;
    repositoryName_ = entries_.at(index.row()).name;
    accept();
  });
  connect(this, &QDialog::finished, controller_, &RelayController::cancelForgeRepositoryRequest);
  updateAccounts(controller_->state());
  if (accounts_->count() == 0) loadRepositories();
  updateSelection();
}

void ForgeDialog::clearRepositories() {
  entries_.clear();
  model_->setStringList({});
  updateSelection();
}
void ForgeDialog::updateAccounts(const AppState& state) {
  const auto selected = accounts_->currentData().toString();
  QStringList before;
  for (int i = 0; i < accounts_->count(); ++i) before.append(accounts_->itemData(i).toString());
  QStringList after;
  for (const auto& account : state.forgeAccounts) after.append(account.id);
  if (before == after) return;
  {
    const QSignalBlocker block(accounts_);
    accounts_->clear();
    for (const auto& account : state.forgeAccounts)
      accounts_->addItem(QStringLiteral("%1 — %2").arg(account.handle, account.serverUrl), account.id);
    const auto index = accounts_->findData(selected);
    if (index >= 0) accounts_->setCurrentIndex(index);
  }
  loadRepositories();
}
void ForgeDialog::loadRepositories() {
  clearRepositories();
  controller_->cancelForgeRepositoryRequest();
  const auto id = accounts_->currentData().toString();
  refresh_->setEnabled(!id.isEmpty());
  remove_->setEnabled(!id.isEmpty());
  message_->setText(id.isEmpty() ? tr("Connect an account to list its repositories.") : tr("Loading repositories…"));
  if (!id.isEmpty()) controller_->requestForgeRepositories(id);
}
void ForgeDialog::updateSelection() {
  const auto index = proxy_->mapToSource(repositories_->currentIndex());
  const bool valid = index.isValid() && index.row() >= 0 && index.row() < entries_.size();
  clone_->setEnabled(valid && !entries_.at(index.row()).sshUrl.isEmpty());
  open_->setEnabled(valid && !entries_.at(index.row()).webUrl.isEmpty());
}
QString ForgeDialog::cloneUrl() const { return cloneUrl_; }
QString ForgeDialog::repositoryName() const { return repositoryName_; }
}  // namespace relay
