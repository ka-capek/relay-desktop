#include "relay/relay_controller.hpp"

#include "relay/app_paths.hpp"
#include "relay/process_runner.hpp"
#include "relay/avatar_cache.hpp"
#include "relay/git_service.hpp"
#include "relay/github_auth.hpp"
#include "relay/relay_store.hpp"
#include "relay/repository_discovery.hpp"
#include "relay/ssh_service.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <optional>
#include <utility>

namespace relay {
namespace {

struct AccountSyncResult {
  QList<Account> accounts;
  QString activeAccountId;
};

struct ScanPayload {
  QString folderPath;
  QStringList found;
  QList<RepositorySummary> summaries;
};

struct BackfillPayload {
  QList<RepositorySummary> summaries;
};

struct ClonePayload {
  Repository repository;
  QString sshProfileId;
  QString accountId;
};

QString defaultSourceRoot() {
#ifdef RELAY_SOURCE_DIR
  return QStringLiteral(RELAY_SOURCE_DIR);
#else
  return QDir::currentPath();
#endif
}

QString defaultApplicationDirectory() {
  return QCoreApplication::instance() ? QCoreApplication::applicationDirPath()
                                      : QDir::currentPath();
}

QString defaultResourcesRoot(const QString& applicationDirectory) {
#ifdef Q_OS_MACOS
  return QDir::cleanPath(
      QDir(applicationDirectory).absoluteFilePath(QStringLiteral("../Resources")));
#else
  return QDir(applicationDirectory).absoluteFilePath(QStringLiteral("resources"));
#endif
}

QString initials(const QString& name) {
  QString result;
  const auto words = name.split(QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
  for (qsizetype index = 0; index < std::min<qsizetype>(2, words.size()); ++index) {
    if (!words.at(index).isEmpty()) result += words.at(index).front().toUpper();
  }
  return result.isEmpty() ? QStringLiteral("GH") : result;
}

void invokeGate(const std::function<void(const QString&, const QString&)>& gate,
                const QString& operation, const QString& key) {
  if (gate) gate(operation, key);
}

template <typename T>
std::optional<T> valueNamed(const QList<T>& values, const QString& id) {
  const auto iterator = std::find_if(values.cbegin(), values.cend(), [&id](const auto& value) {
    return value.id == id;
  });
  return iterator == values.cend() ? std::nullopt : std::optional<T>(*iterator);
}

}  // namespace

RelayController::RelayController(RelayControllerConfig config, QObject* parent)
    : QObject(parent), config_(std::move(config)) {
  if (config_.sourceRoot.isEmpty()) config_.sourceRoot = defaultSourceRoot();
  if (config_.applicationDirectory.isEmpty()) {
    config_.applicationDirectory = defaultApplicationDirectory();
  }
  if (config_.resourcesRoot.isEmpty()) {
    config_.resourcesRoot = defaultResourcesRoot(config_.applicationDirectory);
  }
  if (config_.storeFile.isEmpty()) config_.storeFile = AppPaths::storeFile();

  const auto userDataPath = QFileInfo(config_.storeFile).absolutePath();
  store_ = std::make_unique<RelayStore>(config_.storeFile);
  git_ = std::make_shared<GitService>(config_.resourcesRoot, config_.sourceRoot);
  auth_ = std::make_shared<GitHubAuth>(GitHubAuthContext{
      config_.sourceRoot, config_.resourcesRoot, userDataPath, config_.packaged});
  github_ = std::make_shared<GitHubApi>();
  avatars_ = std::make_shared<AvatarCache>(userDataPath);
  ssh_ = std::make_shared<SshService>(config_.sourceRoot, config_.resourcesRoot);

  qRegisterMetaType<AppState>();
  qRegisterMetaType<Repository>();
  qRegisterMetaType<HistoryPage>();
  qRegisterMetaType<CommitDetail>();
  qRegisterMetaType<GitHubRepositoryPage>();
  qRegisterMetaType<RepositoryScanResult>();
  qRegisterMetaType<GitHubLoginProgress>();
  qRegisterMetaType<SshTestResult>();
  qRegisterMetaType<QList<EmailChoice>>();
}

RelayController::~RelayController() { loginCanceled_->store(true); }

void RelayController::cancelAccountConnection() { loginCanceled_->store(true); }

const AppState& RelayController::state() const noexcept { return state_; }

const Repository* RelayController::currentRepository() const noexcept {
  return currentRepository_.get();
}

QString RelayController::normalizePath(const QString& path) const {
  const QFileInfo info(path);
  const auto canonical = info.canonicalFilePath();
  return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

void RelayController::gate(const QString& operation, const QString& key) const {
  invokeGate(config_.operationGate, operation, key);
}

void RelayController::publishState() { emit stateChanged(state_); }

void RelayController::setPreferences(Preferences preferences) {
  preferences.diffFontSize = qBound(10, preferences.diffFontSize, 24);
  const auto previous = state_.preferences;
  state_.preferences = preferences;
  try {
    persistState();
    publishState();
  } catch (const std::exception& error) {
    state_.preferences = previous;
    emit operationFailed(QStringLiteral("settings"), QString::fromUtf8(error.what()));
  }
}

void RelayController::persistState() {
  try {
    store_->write(mergeAppStateIntoJson(state_, store_->read()));
    persistedState_ = state_;
  } catch (...) {
    // Never change the effective identity/transport behind an unchanged UI
    // when saving metadata fails.
    state_ = persistedState_;
    throw;
  }
}

void RelayController::start() {
  try {
    state_ = appStateFromJson(store_->read());
    persistedState_ = state_;
    currentRepository_.reset();
    invalidateRepositoryRequests();
    ++repositoryGeneration_;
    publishState();
    emit repositoryClosed();
    if (config_.synchronizeAccountsOnStart) synchronizeAccounts();
  } catch (const std::exception& error) {
    emit operationFailed(QStringLiteral("startup"), QString::fromUtf8(error.what()));
  }
}

void RelayController::synchronizeAccounts() {
  const auto generation = ++accountGeneration_;
  const auto known = state_.accounts;
  const auto auth = auth_;
  const auto github = github_;
  const auto avatars = avatars_;
  const auto operationGate = config_.operationGate;
  runAsync<AccountSyncResult>(
      QStringLiteral("accounts"),
      [known, auth, github, avatars, operationGate] {
        invokeGate(operationGate, QStringLiteral("accounts"), {});
        const auto authenticated = auth->authenticatedAccounts();
        QList<Account> accounts;
        for (const auto& authenticatedAccount : authenticated) {
          const auto knownIterator = std::find_if(
              known.cbegin(), known.cend(), [&authenticatedAccount](const auto& candidate) {
                return candidate.handle.compare(authenticatedAccount.handle,
                                                Qt::CaseInsensitive) == 0;
              });
          const Account* previous = knownIterator == known.cend() ? nullptr : &*knownIterator;
          QJsonObject profile;
          if (!previous) {
            const auto token = auth->accountToken(authenticatedAccount.handle);
            profile = github->profile(token, authenticatedAccount.handle);
          }

          Account account;
          const auto profileId = profile.value(QStringLiteral("id")).toInteger();
          account.githubId = profileId > 0 ? profileId : previous ? previous->githubId : 0;
          account.githubIdText = previous ? previous->githubIdText : QString{};
          const auto fallbackId = account.githubIdText.isEmpty()
                                      ? QString::number(account.githubId)
                                      : account.githubIdText;
          account.id = previous ? previous->id : QStringLiteral("github-%1").arg(fallbackId);
          account.handle = profile.value(QStringLiteral("login"))
                               .toString(authenticatedAccount.handle);
          account.name = profile.value(QStringLiteral("name"))
                             .toString(previous ? previous->name : account.handle);
          if (account.name.isEmpty()) account.name = account.handle;
          account.email = profile.value(QStringLiteral("email")).toString();
          if (account.email.isEmpty() && previous) account.email = previous->email;
          if (account.email.isEmpty()) account.email = GitHubApi::noreplyAddress(account);
          account.initials = initials(account.name);
          account.avatarUrl = profile.value(QStringLiteral("avatar_url"))
                                  .toString(previous ? previous->avatarUrl : QString{});
          account.avatarData = avatars->read(account.id);
          if (account.avatarData.isEmpty() && !account.avatarUrl.isEmpty()) {
            account.avatarData = avatars->fetch(account.id, QUrl(account.avatarUrl));
          }
          account.status = previous && !previous->status.isEmpty() ? previous->status : account.name;
          const auto tones = QStringList{QStringLiteral("coral"), QStringLiteral("violet"),
                                         QStringLiteral("blue")};
          account.tone = previous && !previous->tone.isEmpty()
                             ? previous->tone
                             : tones.at(static_cast<qsizetype>(account.githubId % tones.size()));
          account.authSource = QStringLiteral("github-cli");
          account.tokenSource = authenticatedAccount.tokenSource;
          account.active = authenticatedAccount.active;
          accounts.push_back(std::move(account));
        }
        const auto active = std::find_if(accounts.cbegin(), accounts.cend(),
                                         [](const auto& account) { return account.active; });
        return AccountSyncResult{accounts,
                                 active == accounts.cend()
                                     ? accounts.isEmpty() ? QString{} : accounts.constFirst().id
                                     : active->id};
      },
      [this, generation](AccountSyncResult result) {
        if (generation != accountGeneration_) return;
        // User edits made while account discovery was running take precedence
        // over the metadata snapshot captured at the start of the request.
        for (auto& refreshed : result.accounts) {
          if (const auto* current = account(refreshed.id)) refreshed.email = current->email;
        }
        state_.accounts = std::move(result.accounts);
        state_.activeAccountId = std::move(result.activeAccountId);
        for (auto iterator = state_.repositoryAccounts.begin();
             iterator != state_.repositoryAccounts.end();) {
          const auto exists = std::any_of(state_.accounts.cbegin(), state_.accounts.cend(),
                                          [&iterator](const auto& account) {
                                            return account.id == iterator.value();
                                          });
          if (!exists) iterator = state_.repositoryAccounts.erase(iterator);
          else ++iterator;
        }
        persistState();
        publishState();
      }, [this, generation] { return generation == accountGeneration_; });
}

void RelayController::connectAccount() {
  if (accountMutationActive_) {
    emit operationFailed(QStringLiteral("connect-account"), QStringLiteral("Finish the current account operation first."));
    return;
  }
  ++accountGeneration_;
  loginCanceled_->store(false);
  const auto canceled = loginCanceled_;
  const auto auth = auth_;
  const auto operationGate = config_.operationGate;
  QPointer<RelayController> guard(this);
  runAsync<bool>(
      QStringLiteral("connect-account"),
      [auth, operationGate, guard, canceled] {
        invokeGate(operationGate, QStringLiteral("connect-account"), {});
        auth->login([guard](const GitHubLoginProgress& progress) {
          if (!guard) return;
          QMetaObject::invokeMethod(
              guard,
              [guard, progress] {
                if (guard) emit guard->loginProgress(progress);
              },
              Qt::QueuedConnection);
        }, canceled);
        return true;
      },
      [this](bool) { synchronizeAccounts(); });
}

const Account* RelayController::account(const QString& requestedId,
                                        const QString& repositoryPath) const {
  auto id = requestedId;
  if (id.isEmpty() && !repositoryPath.isEmpty()) id = resolvedAccountId(repositoryPath);
  if (id.isEmpty()) id = state_.activeAccountId;
  const auto iterator = std::find_if(state_.accounts.cbegin(), state_.accounts.cend(),
                                     [&id](const auto& candidate) {
                                       return candidate.id == id;
                                     });
  return iterator == state_.accounts.cend() ? nullptr : &*iterator;
}

QString RelayController::resolvedAccountId(const QString& repositoryPath) const {
  return state_.repositoryAccounts.value(repositoryPath, state_.activeAccountId);
}

void RelayController::setActiveAccount(const QString& accountId) {
  const auto* selected = account(accountId);
  if (!selected) {
    emit operationFailed(QStringLiteral("active-account"), QStringLiteral("Account not found."));
    return;
  }
  const auto handle = selected->handle;
  const auto auth = auth_;
  const auto operationGate = config_.operationGate;
  runAsync<bool>(QStringLiteral("active-account"),
                 [auth, operationGate, handle] {
                   invokeGate(operationGate, QStringLiteral("active-account"), handle);
                   auth->switchAccount(handle);
                   return true;
                 },
                 [this](bool) { synchronizeAccounts(); });
}

void RelayController::removeAccount(const QString& accountId) {
  const auto* selected = account(accountId);
  if (!selected) {
    emit operationFailed(QStringLiteral("remove-account"), QStringLiteral("Account not found."));
    return;
  }
  const auto handle = selected->handle;
  const auto id = selected->id;
  const auto auth = auth_;
  const auto avatars = avatars_;
  const auto operationGate = config_.operationGate;
  runAsync<bool>(QStringLiteral("remove-account"),
                 [auth, avatars, operationGate, handle, id] {
                   invokeGate(operationGate, QStringLiteral("remove-account"), handle);
                   auth->removeAccount(handle);
                   avatars->forget(id);
                   return true;
                 },
                 [this](bool) { synchronizeAccounts(); });
}

void RelayController::requestGitHubRepositories(const QString& accountId) {
  const auto generation = ++githubRepositoriesGeneration_;
  const auto* selected = account(accountId);
  if (!selected) {
    emit operationFailed(QStringLiteral("github-repositories"),
                         QStringLiteral("Connect a GitHub account to browse its repositories."));
    return;
  }
  const auto handle = selected->handle;
  const auto auth = auth_;
  const auto github = github_;
  const auto operationGate = config_.operationGate;
  runAsync<GitHubRepositoryPage>(
      QStringLiteral("github-repositories"),
      [auth, github, operationGate, handle] {
        invokeGate(operationGate, QStringLiteral("github-repositories"), handle);
        const auto token = auth->accountToken(handle);
        return github->repositories(token, handle);
      },
      [this](GitHubRepositoryPage page) { emit githubRepositoriesReady(std::move(page)); },
      [this, generation] { return generation == githubRepositoriesGeneration_; });
}

void RelayController::requestAccountEmails(const QString& accountId) {
  const auto generation = ++accountEmailsGeneration_;
  const auto* selected = account(accountId);
  if (!selected) {
    emit operationFailed(QStringLiteral("account-emails"), QStringLiteral("Account not found."));
    return;
  }
  const auto selectedAccount = *selected;
  const auto auth = auth_;
  const auto github = github_;
  const auto operationGate = config_.operationGate;
  runAsync<QList<EmailChoice>>(
      QStringLiteral("account-emails"),
      [auth, github, operationGate, selectedAccount] {
        invokeGate(operationGate, QStringLiteral("account-emails"), selectedAccount.id);
        const auto token = auth->accountToken(selectedAccount.handle);
        return github->emailChoices(selectedAccount, token);
      },
      [this, selectedAccount](QList<EmailChoice> choices) {
        if (const auto* current = account(selectedAccount.id))
          emit accountEmailsReady(selectedAccount.id, std::move(choices), current->email);
      }, [this, generation] { return generation == accountEmailsGeneration_; });
}

void RelayController::setAccountEmail(const QString& accountId, const QString& requestedEmail) {
  auto iterator = std::find_if(state_.accounts.begin(), state_.accounts.end(),
                               [&accountId](const auto& account) {
                                 return account.id == accountId;
                               });
  if (iterator == state_.accounts.end()) {
    emit operationFailed(QStringLiteral("account-email"), QStringLiteral("Account not found."));
    return;
  }
  try {
    iterator->email = GitHubApi::resolveCommitEmail(*iterator, requestedEmail);
    persistState();
    publishState();
  } catch (const std::exception& error) {
    emit operationFailed(QStringLiteral("account-email"), QString::fromUtf8(error.what()));
  }
}

void RelayController::invalidateRepositoryRequests() {
  ++diffGeneration_;
  ++historyGeneration_;
  ++detailGeneration_;
  ++commitDiffGeneration_;
}

void RelayController::openRepository(const QString& repositoryPath) {
  if (repositoryMutationActive_ && repositoryMutationOperation_ != QStringLiteral("clone")) {
    emit operationFailed(QStringLiteral("open-repository"), tr("Wait for the current Git operation to finish before opening a repository."));
    return;
  }
  const auto generation = ++repositoryGeneration_;
  invalidateRepositoryRequests();
  const auto git = git_;
  const auto operationGate = config_.operationGate;
  runAsync<Repository>(QStringLiteral("open-repository"),
                       [git, operationGate, repositoryPath] {
                         invokeGate(operationGate, QStringLiteral("open-repository"),
                                    repositoryPath);
                         return git->readRepository(repositoryPath);
                       },
                       [this, generation](Repository repository) {
                         if (generation != repositoryGeneration_) return;
                         rememberRepository(repository);
                         currentRepository_ = std::make_unique<Repository>(repository);
                         emit currentRepositoryChanged(std::move(repository));
                       }, [this, generation] { return generation == repositoryGeneration_; });
}

void RelayController::refreshRepository() {
  // Every mutation reads fresh repository state before completing. A concurrent
  // refresh could supersede that result with a snapshot taken before the write.
  if (!currentRepository_ || repositoryMutationActive_) return;
  const auto path = currentRepository_->path;
  const auto generation = ++repositoryGeneration_;
  invalidateRepositoryRequests();
  const auto git = git_;
  const auto operationGate = config_.operationGate;
  runAsync<Repository>(QStringLiteral("refresh-repository"),
                       [git, operationGate, path] {
                         invokeGate(operationGate, QStringLiteral("refresh-repository"), path);
                         return git->readRepository(path);
                       },
                       [this, generation, path](Repository repository) {
                         if (generation != repositoryGeneration_ || !currentRepository_ ||
                             currentRepository_->path != path)
                           return;
                         rememberRepository(repository);
                         currentRepository_ = std::make_unique<Repository>(repository);
                         emit currentRepositoryChanged(std::move(repository));
                       }, [this, generation] { return generation == repositoryGeneration_; });
}

void RelayController::closeRepository() {
  ++repositoryGeneration_;
  invalidateRepositoryRequests();
  currentRepository_.reset();
  emit repositoryClosed();
}

void RelayController::requestFileDiff(const QString& filePath) {
  if (!currentRepository_ || filePath.isEmpty()) return;
  const auto repositoryPath = currentRepository_->path;
  const auto generation = ++diffGeneration_;
  const auto git = git_;
  const auto operationGate = config_.operationGate;
  runAsync<FilePreview>(QStringLiteral("file-diff"),
                    [git, operationGate, repositoryPath, filePath] {
                      invokeGate(operationGate, QStringLiteral("file-diff"), filePath);
                      return git->readFilePreview(repositoryPath, filePath);
                    },
                    [this, generation, repositoryPath, filePath](FilePreview preview) {
                      if (generation != diffGeneration_ || !currentRepository_ ||
                          currentRepository_->path != repositoryPath)
                        return;
                      emit fileDiffReady(repositoryPath, filePath, preview.diff);
                      emit filePreviewReady(repositoryPath, filePath, std::move(preview));
                    }, [this, generation] { return generation == diffGeneration_; });
}

void RelayController::requestHistory(const int skip, const int limit, const QString& anchor) {
  if (!currentRepository_) return;
  const auto repositoryPath = currentRepository_->path;
  const auto generation = ++historyGeneration_;
  const auto git = git_;
  const auto operationGate = config_.operationGate;
  const bool graph = state_.preferences.graphHistory;
  runAsync<HistoryPage>(QStringLiteral("history"),
                        [git, operationGate, repositoryPath, skip, limit, anchor, graph] {
                          invokeGate(operationGate, QStringLiteral("history"), repositoryPath);
                          return git->readHistoryPage(repositoryPath, skip, limit, anchor, graph);
                        },
                        [this, generation, repositoryPath](HistoryPage page) {
                          if (generation != historyGeneration_ || !currentRepository_ ||
                              currentRepository_->path != repositoryPath)
                            return;
                          emit historyReady(repositoryPath, std::move(page));
                        }, [this, generation] { return generation == historyGeneration_; });
}

void RelayController::requestCommitDetail(const QString& hash) {
  if (!currentRepository_ || hash.isEmpty()) return;
  const auto repositoryPath = currentRepository_->path;
  const auto generation = ++detailGeneration_;
  ++commitDiffGeneration_;
  const auto git = git_;
  const auto operationGate = config_.operationGate;
  runAsync<CommitDetail>(QStringLiteral("commit-detail"),
                         [git, operationGate, repositoryPath, hash] {
                           invokeGate(operationGate, QStringLiteral("commit-detail"), hash);
                           return git->readCommitDetail(repositoryPath, hash);
                         },
                         [this, generation, repositoryPath](CommitDetail detail) {
                           if (generation != detailGeneration_ || !currentRepository_ ||
                               currentRepository_->path != repositoryPath)
                             return;
                           emit commitDetailReady(repositoryPath, std::move(detail));
                         }, [this, generation] { return generation == detailGeneration_; });
}

void RelayController::requestCommitFileDiff(const QString& hash, const QString& filePath) {
  if (!currentRepository_ || hash.isEmpty() || filePath.isEmpty()) return;
  const auto repositoryPath = currentRepository_->path;
  const auto generation = ++commitDiffGeneration_;
  const auto git = git_;
  const auto operationGate = config_.operationGate;
  runAsync<FilePreview>(QStringLiteral("commit-diff"),
                    [git, operationGate, repositoryPath, hash, filePath] {
                      invokeGate(operationGate, QStringLiteral("commit-diff"), filePath);
                      return git->readFilePreview(repositoryPath, filePath, hash);
                    },
                    [this, generation, repositoryPath, hash, filePath](FilePreview preview) {
                      if (generation != commitDiffGeneration_ || !currentRepository_ ||
                          currentRepository_->path != repositoryPath)
                        return;
                      emit commitFileDiffReady(repositoryPath, hash, filePath, preview.diff);
                      emit commitFilePreviewReady(repositoryPath, hash, filePath, std::move(preview));
                    }, [this, generation] { return generation == commitDiffGeneration_; });
}

Account RelayController::commitIdentity() const {
  if (!currentRepository_) return {};
  if (const auto* selected = account({}, currentRepository_->path)) return *selected;
  Account identity;
  identity.name = state_.preferences.commitName.isEmpty() ? currentRepository_->commitName : state_.preferences.commitName;
  identity.email = state_.preferences.commitEmail.isEmpty() ? currentRepository_->commitEmail : state_.preferences.commitEmail;
  return identity;
}

void RelayController::commit(const QStringList& files, const QString& summary,
                             const QString& description, const QString& accountId) {
  if (!currentRepository_) return;
  if (repositoryMutationActive_) {
    emit operationFailed(QStringLiteral("commit"), QStringLiteral("Wait for the current Git operation to finish."));
    return;
  }
  const auto* selected = account(accountId, currentRepository_->path);
  const auto selectedAccount = selected ? *selected : commitIdentity();
  if (selectedAccount.name.isEmpty() || selectedAccount.email.isEmpty()) {
    emit operationFailed(QStringLiteral("commit"), tr("Set a Git name and email in Settings, or select a GitHub account before committing."));
    return;
  }
  const auto repositoryPath = currentRepository_->path;
  const auto generation = ++repositoryGeneration_;
  invalidateRepositoryRequests();
  const auto git = git_;
  const auto operationGate = config_.operationGate;
  runAsync<Repository>(QStringLiteral("commit"),
                       [git, operationGate, repositoryPath, files, summary, description,
                        selectedAccount] {
                         invokeGate(operationGate, QStringLiteral("commit"), repositoryPath);
                         git->commitFiles(repositoryPath, files, summary, description,
                                          selectedAccount);
                         return git->readRepository(repositoryPath);
                       },
                       [this, generation](Repository repository) {
                         emit commitCreated(repository.path);
                         if (generation != repositoryGeneration_) return;
                         currentRepository_ = std::make_unique<Repository>(repository);
                         emit currentRepositoryChanged(repository);
                         rememberRepository(repository);
                       });
}

QString RelayController::sshCommand(const QString& repositoryPath, const QString& remote) const {
  if (SshService::isGitHubRemote(remote)) return {};
  const auto profileId = state_.repositorySshProfiles.value(repositoryPath);
  const auto profile = valueNamed(state_.sshProfiles, profileId);
  return profile ? ssh_->commandFor(*profile) : QString{};
}

void RelayController::fetchOrigin(const QString& accountId) {
  if (!currentRepository_) return;
  if (repositoryMutationActive_) {
    emit operationFailed(QStringLiteral("fetch"), QStringLiteral("Wait for the current Git operation to finish."));
    return;
  }
  const auto repositoryPath = currentRepository_->path;
  const auto selected = account(accountId, repositoryPath);
  const auto selectedAccount = selected ? std::optional<Account>(*selected) : std::nullopt;
  const auto profile = valueNamed(state_.sshProfiles,
                                  state_.repositorySshProfiles.value(repositoryPath));
  const auto generation = ++repositoryGeneration_;
  invalidateRepositoryRequests();
  const auto git = git_;
  const auto auth = auth_;
  const auto ssh = ssh_;
  const auto operationGate = config_.operationGate;
  runAsync<Repository>(QStringLiteral("fetch"),
                       [git, auth, ssh, operationGate, repositoryPath, selectedAccount, profile] {
                         invokeGate(operationGate, QStringLiteral("fetch"), repositoryPath);
                         const auto remote = git->originRemoteUrl(repositoryPath);
                         const auto githubRemote = SshService::isGitHubRemote(remote);
                         const auto token = selectedAccount && remote.startsWith(QStringLiteral("https://github.com/"), Qt::CaseInsensitive)
                                                ? auth->accountToken(selectedAccount->handle)
                                                : QString{};
                         const auto command = !githubRemote && profile ? ssh->commandFor(*profile)
                                                                       : QString{};
                         git->fetchOrigin(repositoryPath, token,
                                          selectedAccount ? selectedAccount->handle : QString{},
                                          command);
                         return git->readRepository(repositoryPath);
                       },
                       [this, generation, repositoryPath](Repository repository) {
                         if (generation != repositoryGeneration_ || !currentRepository_ ||
                             currentRepository_->path != repositoryPath)
                           return;
                         currentRepository_ = std::make_unique<Repository>(repository);
                         emit currentRepositoryChanged(repository);
                         rememberRepository(repository);
                       });
}

void RelayController::pullOrigin(const QString& accountId) {
  if (!currentRepository_) return;
  if (repositoryMutationActive_) {
    emit operationFailed(QStringLiteral("pull"), QStringLiteral("Wait for the current Git operation to finish."));
    return;
  }
  const auto repositoryPath = currentRepository_->path;
  const auto selected = account(accountId, repositoryPath);
  const auto selectedAccount = selected ? std::optional<Account>(*selected) : std::nullopt;
  const auto profile = valueNamed(state_.sshProfiles,
                                  state_.repositorySshProfiles.value(repositoryPath));
  const auto generation = ++repositoryGeneration_;
  invalidateRepositoryRequests();
  const auto git = git_;
  const auto auth = auth_;
  const auto ssh = ssh_;
  const auto operationGate = config_.operationGate;
  runAsync<Repository>(QStringLiteral("pull"),
                       [git, auth, ssh, operationGate, repositoryPath, selectedAccount, profile] {
                         invokeGate(operationGate, QStringLiteral("pull"), repositoryPath);
                         const auto remote = git->originRemoteUrl(repositoryPath);
                         const auto githubRemote = SshService::isGitHubRemote(remote);
                         const auto token = selectedAccount && remote.startsWith(QStringLiteral("https://github.com/"), Qt::CaseInsensitive)
                                                ? auth->accountToken(selectedAccount->handle)
                                                : QString{};
                         const auto command = !githubRemote && profile ? ssh->commandFor(*profile)
                                                                       : QString{};
                         git->pullOrigin(repositoryPath, token,
                                          selectedAccount ? selectedAccount->handle : QString{},
                                          command);
                         return git->readRepository(repositoryPath);
                       },
                       [this, generation, repositoryPath](Repository repository) {
                         if (generation != repositoryGeneration_ || !currentRepository_ ||
                             currentRepository_->path != repositoryPath)
                           return;
                         currentRepository_ = std::make_unique<Repository>(repository);
                         emit currentRepositoryChanged(repository);
                         rememberRepository(repository);
                       });
}

void RelayController::pushOrigin(const QString& accountId) {
  if (!currentRepository_) return;
  if (repositoryMutationActive_) {
    emit operationFailed(QStringLiteral("push"), QStringLiteral("Wait for the current Git operation to finish."));
    return;
  }
  const auto repositoryPath = currentRepository_->path;
  const auto selected = account(accountId, repositoryPath);
  const auto selectedAccount = selected ? std::optional<Account>(*selected) : std::nullopt;
  const auto profile = valueNamed(state_.sshProfiles,
                                  state_.repositorySshProfiles.value(repositoryPath));
  const auto generation = ++repositoryGeneration_;
  invalidateRepositoryRequests();
  const auto git = git_;
  const auto auth = auth_;
  const auto ssh = ssh_;
  const auto operationGate = config_.operationGate;
  runAsync<Repository>(QStringLiteral("push"),
                       [git, auth, ssh, operationGate, repositoryPath, selectedAccount, profile] {
                         invokeGate(operationGate, QStringLiteral("push"), repositoryPath);
                         const auto remote = git->originRemoteUrl(repositoryPath, true);
                         const auto githubRemote = SshService::isGitHubRemote(remote);
                         const auto token = selectedAccount && remote.startsWith(QStringLiteral("https://github.com/"), Qt::CaseInsensitive)
                                                ? auth->accountToken(selectedAccount->handle)
                                                : QString{};
                         const auto command = !githubRemote && profile ? ssh->commandFor(*profile)
                                                                       : QString{};
                         git->pushOrigin(repositoryPath, token,
                                         selectedAccount ? selectedAccount->handle : QString{},
                                         command);
                         return git->readRepository(repositoryPath);
                       },
                       [this, generation, repositoryPath](Repository repository) {
                         if (generation != repositoryGeneration_ || !currentRepository_ ||
                             currentRepository_->path != repositoryPath)
                           return;
                         currentRepository_ = std::make_unique<Repository>(repository);
                         emit currentRepositoryChanged(repository);
                         rememberRepository(repository);
                       });
}

void RelayController::switchBranch(const QString& branch) {
  if (!currentRepository_) return;
  if (repositoryMutationActive_) {
    emit operationFailed(QStringLiteral("switch-branch"), QStringLiteral("Wait for the current Git operation to finish."));
    return;
  }
  const auto repositoryPath = currentRepository_->path;
  const auto generation = ++repositoryGeneration_;
  invalidateRepositoryRequests();
  const auto git = git_;
  const auto operationGate = config_.operationGate;
  runAsync<Repository>(QStringLiteral("switch-branch"),
                       [git, operationGate, repositoryPath, branch] {
                         invokeGate(operationGate, QStringLiteral("switch-branch"), branch);
                         git->switchBranch(repositoryPath, branch);
                         return git->readRepository(repositoryPath);
                       },
                       [this, generation, repositoryPath](Repository repository) {
                         if (generation != repositoryGeneration_ || !currentRepository_ ||
                             currentRepository_->path != repositoryPath)
                           return;
                         currentRepository_ = std::make_unique<Repository>(repository);
                         emit currentRepositoryChanged(repository);
                         rememberRepository(repository);
                       });
}

void RelayController::createBranch(const QString& branch) {
  if (!currentRepository_) return;
  if (repositoryMutationActive_) {
    emit operationFailed(QStringLiteral("create-branch"), QStringLiteral("Wait for the current Git operation to finish."));
    return;
  }
  const auto repositoryPath = currentRepository_->path;
  const auto generation = ++repositoryGeneration_;
  invalidateRepositoryRequests();
  const auto git = git_;
  const auto operationGate = config_.operationGate;
  runAsync<Repository>(QStringLiteral("create-branch"),
                       [git, operationGate, repositoryPath, branch] {
                         invokeGate(operationGate, QStringLiteral("create-branch"), branch);
                         git->createBranch(repositoryPath, branch);
                         return git->readRepository(repositoryPath);
                       },
                       [this, generation, repositoryPath](Repository repository) {
                         if (generation != repositoryGeneration_ || !currentRepository_ ||
                             currentRepository_->path != repositoryPath)
                           return;
                         currentRepository_ = std::make_unique<Repository>(repository);
                         emit currentRepositoryChanged(repository);
                         rememberRepository(repository);
                       });
}

void RelayController::executeRepositoryAction(const RepositoryAction action, const QString& target,
                                               const QStringList& paths) {
  if (!currentRepository_) return;
  if (repositoryMutationActive_) {
    emit operationFailed(QStringLiteral("repository-action"), tr("Wait for the current Git operation to finish."));
    return;
  }
  const auto path = currentRepository_->path;
  const auto identity = commitIdentity();
  const bool createsCommit = action == RepositoryAction::mergeBranch || action == RepositoryAction::revertCommit ||
      action == RepositoryAction::cherryPick || action == RepositoryAction::continueOperation ||
      action == RepositoryAction::rebaseBranch || action == RepositoryAction::skipOperation || action == RepositoryAction::amendMessage;
  if (createsCommit && (identity.name.isEmpty() || identity.email.isEmpty())) {
    emit operationFailed(QStringLiteral("repository-action"), tr("Set a Git name and email in Settings, or select a GitHub account before creating commits."));
    return;
  }
  const auto generation = ++repositoryGeneration_;
  invalidateRepositoryRequests();
  const auto git = git_;
  const auto operationGate = config_.operationGate;
  struct Result { Repository repository; QString error; std::optional<CommitDetail> undone; };
  runAsync<Result>(QStringLiteral("repository-action"),
      [git, operationGate, path, action, target, paths, identity] {
        invokeGate(operationGate, QStringLiteral("repository-action"), path);
        QString error;
        std::optional<CommitDetail> undone;
        try {
          if (action == RepositoryAction::undoCommit && !target.isEmpty()) undone = git->readCommitDetail(path, target);
          git->performAction(path, action, target, paths, identity);
        } catch (const ProcessError& failure) { error = failure.qMessage(); undone.reset(); }
        // Conflicted merges/reverts/stash applications return nonzero but have
        // changed the worktree. Always publish their actual conflict state.
        return Result{git->readRepository(path), error, undone};
      }, [this, generation, path](Result result) {
        if (result.undone) emit commitUndone(path, result.undone->title, result.undone->body);
        if (!result.error.isEmpty()) emit operationFailed(QStringLiteral("repository-action"), tr("%1: %2").arg(result.repository.name, result.error));
        if (generation != repositoryGeneration_ || !currentRepository_ || currentRepository_->path != path) return;
        currentRepository_ = std::make_unique<Repository>(result.repository);
        emit currentRepositoryChanged(result.repository);
        rememberRepository(result.repository);
      });
}

void RelayController::publishRepository(const QString& name, const QString& description, const bool isPrivate, const QString& accountId) {
  if (!currentRepository_) return;
  if (repositoryMutationActive_ || accountMutationActive_) {
    emit operationFailed(QStringLiteral("publish-repository"), tr("Wait for the current Git operation to finish."));
    return;
  }
  const auto* selected = account(accountId, currentRepository_->path);
  if (!selected || !currentRepository_->remote.isEmpty() || !currentRepository_->hasHead) {
    emit operationFailed(QStringLiteral("publish-repository"), tr("Select a GitHub account and create an initial commit in a repository without an origin remote."));
    return;
  }
  const auto identity = *selected;
  const auto path = currentRepository_->path;
  state_.repositoryAccounts.insert(path, identity.id);
  try { persistState(); publishState(); }
  catch (const std::exception& failure) {
    emit operationFailed(QStringLiteral("publish-repository"), QString::fromUtf8(failure.what()));
    return;
  }
  const auto original = *currentRepository_;
  const auto generation = ++repositoryGeneration_;
  invalidateRepositoryRequests();
  const auto git = git_;
  const auto auth = auth_;
  const auto github = github_;
  struct Result { Repository repository; QString error; };
  runAsync<Result>(QStringLiteral("publish-repository"),
      [git, auth, github, path, identity, name, description, isPrivate, original] {
        if (!git->originRemoteUrl(path).isEmpty()) throw ProcessError(QStringLiteral("An origin remote was added. Refresh before publishing."));
        const auto token = auth->accountToken(identity.handle);
        const auto remote = github->createRepository(token, identity.handle, name, description, isPrivate);
        QString error;
        bool originAdded = false;
        try {
          git->setOriginRemote(path, remote);
          originAdded = true;
          git->pushOrigin(path, token, identity.handle);
        } catch (const std::exception& failure) {
          const auto recovery = originAdded ? QStringLiteral("Fix the issue and retry Push.")
              : QStringLiteral("Set this URL using Repository → Origin remote URL, then Push.");
          error = QStringLiteral("GitHub repository created at %1, but publication could not finish: %2. %3").arg(remote, QString::fromUtf8(failure.what()), recovery);
        }
        auto repository = original;
        if (originAdded) repository.remote = remote;
        try { repository = git->readRepository(path); }
        catch (const std::exception& failure) {
          error += QStringLiteral(" Repository created at %1. Refresh failed: %2").arg(remote, QString::fromUtf8(failure.what()));
        }
        return Result{repository, error};
      }, [this, generation, path](Result result) {
        if (!result.error.isEmpty()) emit operationFailed(QStringLiteral("publish-repository"), result.error);
        if (generation != repositoryGeneration_ || !currentRepository_ || currentRepository_->path != path) return;
        currentRepository_ = std::make_unique<Repository>(result.repository);
        emit currentRepositoryChanged(result.repository);
        rememberRepository(result.repository);
      });
}

void RelayController::createLocalRepository(const QString& destinationPath) {
  if (repositoryMutationActive_) {
    emit operationFailed(QStringLiteral("create-repository"), tr("Wait for the current Git operation to finish before creating a repository."));
    return;
  }
  const auto generation = ++repositoryGeneration_;
  invalidateRepositoryRequests();
  const auto git = git_;
  runAsync<Repository>(QStringLiteral("create-repository"),
      [git, destinationPath] { return git->createRepository(destinationPath); },
      [this, generation](Repository repository) {
        rememberRepository(repository);
        if (generation != repositoryGeneration_) return;
        currentRepository_ = std::make_unique<Repository>(repository);
        emit currentRepositoryChanged(std::move(repository));
      });
}

void RelayController::cloneRepository(const QString& remoteUrl, const QString& parentPath,
                                      const QString& repositoryName, const QString& accountId,
                                      const QString& sshProfileId) {
  if (repositoryMutationActive_ || accountMutationActive_) {
    emit operationFailed(QStringLiteral("clone"), tr("Wait for the current Git or account operation to finish before cloning."));
    return;
  }
  static const QRegularExpression allowedRemote(QStringLiteral(R"(^(https?://|ssh://|git@))"),
                                                 QRegularExpression::CaseInsensitiveOption);
  const auto remote = remoteUrl.trimmed();
  const auto parent = QDir::cleanPath(QFileInfo(parentPath).absoluteFilePath());
  if (!allowedRemote.match(remote).hasMatch() && !GitService::isSshRemote(remote)) {
    emit operationFailed(QStringLiteral("clone"),
                         QStringLiteral("Enter a valid HTTPS or SSH Git repository URL."));
    return;
  }
  if (!QFileInfo(parent).isDir()) {
    emit operationFailed(QStringLiteral("clone"),
                         QStringLiteral("Choose an existing local folder for the clone."));
    return;
  }
  if (repositoryName.isEmpty() || QFileInfo(repositoryName).fileName() != repositoryName ||
      repositoryName == QStringLiteral(".") || repositoryName == QStringLiteral("..")) {
    emit operationFailed(QStringLiteral("clone"),
                         QStringLiteral("Enter a valid folder name for the cloned repository."));
    return;
  }
  const auto destination = QDir(parent).filePath(repositoryName);
  if (QFileInfo::exists(destination)) {
    emit operationFailed(QStringLiteral("clone"),
                         QStringLiteral("A file or folder named %1 already exists there.")
                             .arg(repositoryName));
    return;
  }

  const auto selected = account(accountId);
  const auto selectedAccount = selected ? std::optional<Account>(*selected) : std::nullopt;
  const auto profile = valueNamed(state_.sshProfiles, sshProfileId);
  const auto git = git_;
  const auto auth = auth_;
  const auto ssh = ssh_;
  const auto operationGate = config_.operationGate;
  const auto generation = ++repositoryGeneration_;
  invalidateRepositoryRequests();
  runAsync<ClonePayload>(
      QStringLiteral("clone"),
      [git, auth, ssh, operationGate, remote, destination, selectedAccount, profile,
       sshProfileId] {
        invokeGate(operationGate, QStringLiteral("clone"), remote);
        const auto githubRemote = SshService::isGitHubRemote(remote);
        const auto token = selectedAccount && remote.startsWith(QStringLiteral("https://github.com/"), Qt::CaseInsensitive)
                               ? auth->accountToken(selectedAccount->handle)
                               : QString{};
        const auto command = !githubRemote && profile ? ssh->commandFor(*profile) : QString{};
        return ClonePayload{
            git->cloneRepository(remote, destination, token,
                                 selectedAccount ? selectedAccount->handle : QString{}, command),
            profile ? sshProfileId : QString{},
            githubRemote && selectedAccount ? selectedAccount->id : QString{}};
      },
      [this, generation](ClonePayload payload) {
        // A successful clone must keep its selected identity even if the user
        // navigated elsewhere while Git was running. Only activation is stale.
        if (!payload.sshProfileId.isEmpty()) {
          state_.repositorySshProfiles.insert(payload.repository.path, payload.sshProfileId);
        }
        if (!payload.accountId.isEmpty() && account(payload.accountId)) {
          state_.repositoryAccounts.insert(payload.repository.path, payload.accountId);
        }
        rememberRepository(payload.repository);
        if (generation != repositoryGeneration_) return;
        currentRepository_ = std::make_unique<Repository>(payload.repository);
        emit currentRepositoryChanged(std::move(payload.repository));
      });
}

void RelayController::scanFolder(const QString& folderPath) {
  const auto git = git_;
  const auto operationGate = config_.operationGate;
  runAsync<ScanPayload>(QStringLiteral("scan-folder"),
                        [git, operationGate, folderPath] {
                          invokeGate(operationGate, QStringLiteral("scan-folder"), folderPath);
                          const auto found = scanForRepositories(folderPath);
                          QList<RepositorySummary> summaries;
                          for (const auto& path : found) {
                            try {
                              summaries.push_back(git->readRepositorySummary(path));
                            } catch (...) {
                              // One unreadable repository does not fail the scan.
                            }
                          }
                          return ScanPayload{folderPath, found, summaries};
                        },
                        [this](ScanPayload payload) {
                          QSet<QString> existing;
                          for (const auto& repository : state_.repositories)
                            existing.insert(repository.path);
                          qsizetype added = 0;
                          for (const auto& summary : payload.summaries)
                            if (!existing.contains(summary.path)) ++added;
                          const auto result = RepositoryScanResult{
                              payload.folderPath, payload.found.size(), payload.summaries.size(), added};
                          rememberSummaries(payload.summaries);
                          emit repositoryScanFinished(result);
                        });
}

void RelayController::backfillRepositoryMetadata() {
  QStringList pending;
  for (const auto& repository : state_.repositories)
    if (!repository.firstCommit) pending.push_back(repository.path);
  if (pending.isEmpty()) return;
  const auto git = git_;
  const auto operationGate = config_.operationGate;
  runAsync<BackfillPayload>(QStringLiteral("repository-metadata"),
                            [git, operationGate, pending] {
                              QList<RepositorySummary> summaries;
                              for (const auto& path : pending) {
                                try {
                                  invokeGate(operationGate, QStringLiteral("repository-metadata"),
                                             path);
                                  summaries.push_back(git->readRepositorySummary(path));
                                } catch (...) {
                                }
                              }
                              return BackfillPayload{summaries};
                            },
                            [this](BackfillPayload payload) {
                              for (auto& repository : state_.repositories) {
                                const auto value = std::find_if(
                                    payload.summaries.cbegin(), payload.summaries.cend(),
                                    [&repository](const auto& summary) {
                                      return summary.path == repository.path;
                                    });
                                if (value == payload.summaries.cend()) continue;
                                repository.firstCommit = value->firstCommit;
                                repository.latestCommit = value->latestCommit;
                                repository.branch = value->branch;
                                repository.changes = value->changes;
                              }
                              persistState();
                              publishState();
                            });
}

void RelayController::rememberRepository(const Repository& repository) {
  const auto now = QDateTime::currentDateTimeUtc();
  const auto known = std::find_if(state_.repositories.cbegin(), state_.repositories.cend(),
                                  [&repository](const auto& item) {
                                    return item.path == repository.path;
                                  });
  RepositorySummary summary{repository.path,
                            repository.name,
                            repository.owner,
                            repository.branch,
                            repository.files.size(),
                            now,
                            known == state_.repositories.cend() || !known->addedAt
                                ? std::optional<QDateTime>(now)
                                : known->addedAt,
                            repository.latestCommit,
                            repository.firstCommit};
  if (!summary.latestCommit && known != state_.repositories.cend())
    summary.latestCommit = known->latestCommit;
  if (!summary.firstCommit && known != state_.repositories.cend())
    summary.firstCommit = known->firstCommit;
  state_.repositories.removeIf([&repository](const auto& item) {
    return item.path == repository.path;
  });
  state_.repositories.prepend(summary);
  while (state_.repositories.size() > maximumRememberedRepositories)
    state_.repositories.removeLast();
  if (!state_.manualOrder.contains(repository.path)) state_.manualOrder.push_back(repository.path);
  persistState();
  publishState();
}

void RelayController::rememberSummaries(const QList<RepositorySummary>& summaries) {
  QHash<QString, RepositorySummary> known;
  for (const auto& repository : state_.repositories) known.insert(repository.path, repository);
  QList<RepositorySummary> merged;
  QSet<QString> scanned;
  for (auto summary : summaries) {
    scanned.insert(summary.path);
    const auto previous = known.constFind(summary.path);
    if (previous != known.cend()) {
      summary.addedAt = previous->addedAt;
      summary.lastOpened = previous->lastOpened;
      if (!summary.firstCommit) summary.firstCommit = previous->firstCommit;
    } else if (!summary.addedAt) {
      summary.addedAt = summary.lastOpened;
    }
    merged.push_back(std::move(summary));
  }
  for (const auto& repository : state_.repositories)
    if (!scanned.contains(repository.path)) merged.push_back(repository);
  while (merged.size() > maximumRememberedRepositories) merged.removeLast();
  state_.repositories = std::move(merged);
  for (const auto& repository : state_.repositories)
    if (!state_.manualOrder.contains(repository.path)) state_.manualOrder.push_back(repository.path);
  persistState();
  publishState();
}

void RelayController::removeRepository(const QString& repositoryPath) {
  try {
    const auto target = normalizePath(repositoryPath);
    state_.repositories.removeIf([this, &target](const auto& repository) {
      return normalizePath(repository.path) == target;
    });
    for (auto iterator = state_.repositoryAccounts.begin();
         iterator != state_.repositoryAccounts.end();) {
      if (normalizePath(iterator.key()) == target)
        iterator = state_.repositoryAccounts.erase(iterator);
      else
        ++iterator;
    }
    state_.manualOrder.removeIf([this, &target](const auto& path) {
      return normalizePath(path) == target;
    });
    const auto closesCurrent =
        currentRepository_ && normalizePath(currentRepository_->path) == target;
    persistState();
    publishState();
    if (closesCurrent) closeRepository();
  } catch (const std::exception& error) {
    emit operationFailed(QStringLiteral("remove-repository"), QString::fromUtf8(error.what()));
  }
}

void RelayController::setRepositoryOrder(const RepositoryOrderMode mode,
                                         const SortDirection direction) {
  try {
    state_.repositoryOrder = {mode, direction};
    persistState();
    publishState();
  } catch (const std::exception& error) {
    emit operationFailed(QStringLiteral("repository-order"), QString::fromUtf8(error.what()));
  }
}

void RelayController::setManualOrder(const QStringList& repositoryPaths) {
  try {
    QSet<QString> known;
    for (const auto& repository : state_.repositories) known.insert(repository.path);
    QSet<QString> seen;
    QStringList order;
    for (const auto& path : repositoryPaths) {
      if (!known.contains(path) || seen.contains(path)) continue;
      seen.insert(path);
      order.push_back(path);
    }
    for (const auto& repository : state_.repositories)
      if (!seen.contains(repository.path)) order.push_back(repository.path);
    state_.manualOrder = std::move(order);
    persistState();
    publishState();
  } catch (const std::exception& error) {
    emit operationFailed(QStringLiteral("manual-order"), QString::fromUtf8(error.what()));
  }
}

void RelayController::setRepositoryAccount(const QString& repositoryPath,
                                           const QString& accountId) {
  if (!accountId.isEmpty() && !account(accountId)) {
    emit operationFailed(QStringLiteral("repository-account"), QStringLiteral("Account not found."));
    return;
  }
  try {
    if (accountId.isEmpty()) state_.repositoryAccounts.remove(repositoryPath);
    else state_.repositoryAccounts.insert(repositoryPath, accountId);
    persistState();
    publishState();
  } catch (const std::exception& error) {
    emit operationFailed(QStringLiteral("repository-account"), QString::fromUtf8(error.what()));
  }
}

void RelayController::saveSshProfile(const SshProfile& profile) {
  const auto normalized = SshService::normalizeProfile(profile);
  if (!normalized) {
    emit operationFailed(QStringLiteral("ssh-profile"),
                         QStringLiteral("Enter a host name for this SSH identity."));
    return;
  }
  try {
    auto iterator = std::find_if(state_.sshProfiles.begin(), state_.sshProfiles.end(),
                                 [&normalized](const auto& current) {
                                   return current.id == normalized->id;
                                 });
    if (iterator == state_.sshProfiles.end()) state_.sshProfiles.push_back(*normalized);
    else *iterator = *normalized;
    persistState();
    publishState();
  } catch (const std::exception& error) {
    emit operationFailed(QStringLiteral("ssh-profile"), QString::fromUtf8(error.what()));
  }
}

void RelayController::removeSshProfile(const QString& profileId) {
  try {
    state_.sshProfiles.removeIf([&profileId](const auto& profile) {
      return profile.id == profileId;
    });
    for (auto iterator = state_.repositorySshProfiles.begin();
         iterator != state_.repositorySshProfiles.end();) {
      if (iterator.value() == profileId) iterator = state_.repositorySshProfiles.erase(iterator);
      else ++iterator;
    }
    persistState();
    publishState();
  } catch (const std::exception& error) {
    emit operationFailed(QStringLiteral("ssh-profile"), QString::fromUtf8(error.what()));
  }
}

void RelayController::setRepositorySshProfile(const QString& repositoryPath,
                                              const QString& profileId) {
  if (!profileId.isEmpty() && !valueNamed(state_.sshProfiles, profileId)) {
    emit operationFailed(QStringLiteral("repository-ssh"),
                         QStringLiteral("SSH identity not found."));
    return;
  }
  try {
    if (profileId.isEmpty()) state_.repositorySshProfiles.remove(repositoryPath);
    else state_.repositorySshProfiles.insert(repositoryPath, profileId);
    persistState();
    publishState();
  } catch (const std::exception& error) {
    emit operationFailed(QStringLiteral("repository-ssh"), QString::fromUtf8(error.what()));
  }
}

void RelayController::testSshProfile(const SshProfile& profile) {
  const auto ssh = ssh_;
  const auto operationGate = config_.operationGate;
  runAsync<SshTestResult>(QStringLiteral("ssh-test"),
                          [ssh, operationGate, profile] {
                            invokeGate(operationGate, QStringLiteral("ssh-test"), profile.host);
                            return ssh->testConnection(profile);
                          },
                          [this](SshTestResult result) {
                            emit sshTestFinished(std::move(result));
                          });
}

}  // namespace relay
