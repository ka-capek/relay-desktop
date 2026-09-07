#pragma once

#include "relay/domain.hpp"
#include "relay/github_api.hpp"

#include <QFutureWatcher>
#include <QException>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtConcurrentRun>

#include <functional>
#include <atomic>
#include <memory>

namespace relay {

class AvatarCache;
class GitHubAuth;
class GitService;
class RelayStore;
class SshService;

struct RelayControllerConfig {
  QString storeFile;
  QString sourceRoot;
  QString resourcesRoot;
  QString applicationDirectory;
  bool packaged{};
  bool synchronizeAccountsOnStart{true};

  // Optional deterministic gate for integration tests and diagnostics. It is
  // called on the worker thread immediately before an operation starts.
  std::function<void(const QString& operation, const QString& key)> operationGate;
};

struct RepositoryScanResult {
  QString folderPath;
  qsizetype found{};
  qsizetype readable{};
  qsizetype added{};
};

class RelayController final : public QObject {
  Q_OBJECT

 public:
  explicit RelayController(RelayControllerConfig config = {}, QObject* parent = nullptr);
  ~RelayController() override;

  [[nodiscard]] const AppState& state() const noexcept;
  [[nodiscard]] const Repository* currentRepository() const noexcept;
  [[nodiscard]] Account commitIdentity() const;
  [[nodiscard]] QString resolvedAccountId(const QString& repositoryPath) const;
  [[nodiscard]] QString resolvedSshProfileId(const QString& repositoryPath) const;

 public slots:
  void start();
  void setPreferences(relay::Preferences preferences);
  void synchronizeAccounts();
  void connectAccount();
  void cancelAccountConnection();
  void setActiveAccount(const QString& accountId);
  void removeAccount(const QString& accountId);
  void requestGitHubRepositories(const QString& accountId = {});
  void requestAccountEmails(const QString& accountId);
  void setAccountEmail(const QString& accountId, const QString& requestedEmail);

  void openRepository(const QString& repositoryPath);
  void refreshRepository();
  void closeRepository();
  void requestFileDiff(const QString& filePath);
  void requestHistory(int skip = 0, int limit = 50, const QString& anchor = {});
  void requestCommitDetail(const QString& hash);
  void requestCommitFileDiff(const QString& hash, const QString& filePath);
  void commit(const QStringList& files, const QString& summary, const QString& description,
              const QString& accountId = {});
  void fetchOrigin(const QString& accountId = {});
  void pushOrigin(const QString& accountId = {});
  void pullOrigin(const QString& accountId = {});
  void executeRepositoryAction(relay::RepositoryAction action, const QString& target = {},
                               const QStringList& paths = {});
  void createLocalRepository(const QString& destinationPath);
  void publishRepository(const QString& name, const QString& description, bool isPrivate, const QString& accountId = {});
  void createBranch(const QString& branch);
  void switchBranch(const QString& branch);
  void cloneRepository(const QString& remoteUrl, const QString& parentPath,
                       const QString& repositoryName, const QString& accountId = {},
                       const QString& sshProfileId = {});

  void scanFolder(const QString& folderPath);
  void backfillRepositoryMetadata();
  void removeRepository(const QString& repositoryPath);
  void setRepositoryOrder(RepositoryOrderMode mode, SortDirection direction);
  void setManualOrder(const QStringList& repositoryPaths);
  void setRepositoryAccount(const QString& repositoryPath, const QString& accountId);

  void saveSshProfile(const SshProfile& profile);
  void removeSshProfile(const QString& profileId);
  void setRepositorySshProfile(const QString& repositoryPath, const QString& profileId);
  void testSshProfile(const SshProfile& profile);

 signals:
  void stateChanged(relay::AppState state);
  void currentRepositoryChanged(relay::Repository repository);
  void repositoryClosed();
  void commitCreated(QString repositoryPath);
  void commitUndone(QString repositoryPath, QString summary, QString description);
  void filePreviewReady(QString repositoryPath, QString filePath, relay::FilePreview preview);
  void commitFilePreviewReady(QString repositoryPath, QString hash, QString filePath, relay::FilePreview preview);
  void fileDiffReady(QString repositoryPath, QString filePath, QString diff);
  void historyReady(QString repositoryPath, relay::HistoryPage page);
  void commitDetailReady(QString repositoryPath, relay::CommitDetail detail);
  void commitFileDiffReady(QString repositoryPath, QString hash, QString filePath,
                           QString diff);
  void githubRepositoriesReady(relay::GitHubRepositoryPage page);
  void accountEmailsReady(QString accountId, QList<relay::EmailChoice> choices,
                          QString currentEmail);
  void loginProgress(relay::GitHubLoginProgress progress);
  void repositoryScanFinished(relay::RepositoryScanResult result);
  void sshTestFinished(relay::SshTestResult result);
  void busyChanged(QString operation, bool busy);
  void operationFailed(QString operation, QString message);

 private:
  template <typename Result, typename Work, typename Completion>
  void runAsync(QString operation, Work&& work, Completion&& completion,
                std::function<bool()> isCurrent = {}) {
    const bool repositoryMutation = QStringList{QStringLiteral("commit"), QStringLiteral("fetch"),
        QStringLiteral("push"), QStringLiteral("pull"), QStringLiteral("switch-branch"),
        QStringLiteral("create-branch"), QStringLiteral("repository-action"), QStringLiteral("publish-repository"), QStringLiteral("clone")}.contains(operation);
    const bool accountMutation = QStringList{QStringLiteral("connect-account"),
        QStringLiteral("active-account"), QStringLiteral("remove-account")}.contains(operation);
    if ((repositoryMutation && (repositoryMutationActive_ || accountMutationActive_)) ||
        (accountMutation && (accountMutationActive_ || repositoryMutationActive_))) {
      emit operationFailed(operation, QStringLiteral("Wait for the current operation to finish."));
      return;
    }
    if (repositoryMutation) { repositoryMutationActive_ = true; repositoryMutationOperation_ = operation; }
    if (accountMutation) { accountMutationActive_ = true; ++accountGeneration_; }
    emit busyChanged(operation, true);
    auto* watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this,
            [this, watcher, operation = std::move(operation),
             completion = std::forward<Completion>(completion),
             isCurrent = std::move(isCurrent), repositoryMutation, accountMutation]() mutable {
              try {
                // Check before retrieving the result: stale failures must not
                // replace the state of a newer request either.
                if (!isCurrent || isCurrent()) {
                  try {
                    completion(watcher->future().result());
                  } catch (const QUnhandledException& error) {
                    // QtConcurrent wraps standard exceptions. Preserve the
                    // service's actionable message across the worker boundary.
                    if (error.exception()) std::rethrow_exception(error.exception());
                    throw;
                  }
                }
              } catch (const std::exception& error) {
                emit operationFailed(operation, QString::fromUtf8(error.what()));
              } catch (...) {
                emit operationFailed(operation, QStringLiteral("The operation failed."));
              }
              watcher->deleteLater();
              if (repositoryMutation) { repositoryMutationActive_ = false; repositoryMutationOperation_.clear(); }
              if (accountMutation) accountMutationActive_ = false;
              emit busyChanged(operation, false);
            });
    watcher->setFuture(QtConcurrent::run(std::forward<Work>(work)));
  }

  void persistState();
  void publishState();
  void rememberRepository(const Repository& repository);
  void rememberSummaries(const QList<RepositorySummary>& summaries);
  void invalidateRepositoryRequests();
  [[nodiscard]] const Account* account(const QString& requestedId,
                                       const QString& repositoryPath = {}) const;
  [[nodiscard]] QString sshCommand(const QString& repositoryPath,
                                   const QString& remote) const;
  [[nodiscard]] QString normalizePath(const QString& path) const;
  void gate(const QString& operation, const QString& key) const;

  RelayControllerConfig config_;
  std::unique_ptr<RelayStore> store_;
  std::shared_ptr<GitService> git_;
  std::shared_ptr<GitHubAuth> auth_;
  std::shared_ptr<GitHubApi> github_;
  std::shared_ptr<AvatarCache> avatars_;
  std::shared_ptr<SshService> ssh_;
  AppState state_;
  AppState persistedState_;
  bool repositoryMutationActive_{};
  QString repositoryMutationOperation_;
  bool accountMutationActive_{};
  std::shared_ptr<std::atomic_bool> loginCanceled_{std::make_shared<std::atomic_bool>(false)};
  std::unique_ptr<Repository> currentRepository_;
  quint64 accountGeneration_{};
  quint64 accountEmailsGeneration_{};
  quint64 githubRepositoriesGeneration_{};
  quint64 repositoryGeneration_{};
  quint64 diffGeneration_{};
  quint64 historyGeneration_{};
  quint64 detailGeneration_{};
  quint64 commitDiffGeneration_{};
};

}  // namespace relay

Q_DECLARE_METATYPE(relay::AppState)
Q_DECLARE_METATYPE(relay::GitHubRepositoryPage)
Q_DECLARE_METATYPE(relay::RepositoryScanResult)
Q_DECLARE_METATYPE(relay::GitHubLoginProgress)
Q_DECLARE_METATYPE(relay::SshTestResult)
Q_DECLARE_METATYPE(QList<relay::EmailChoice>)
