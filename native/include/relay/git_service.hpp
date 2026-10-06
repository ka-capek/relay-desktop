#pragma once

#include "relay/domain.hpp"

#include <QHash>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace relay {

// Git operations run as child processes. The methods are synchronous because
// a repository read is a composition of several child processes; callers must
// invoke them from a worker thread (or wrap them in QtConcurrent) rather than
// block the GUI thread.
class GitService final {
 public:
  static constexpr int historyBatchLimit = 200;

  explicit GitService(QString resourcesPath = {}, QString sourceRoot = {});
  // Adds SSH commit signing with the account's key, after any configuration
  // already passed through GIT_CONFIG_* in the inherited environment.
  static void addSigningConfiguration(QProcessEnvironment& environment, const Account& account);

  [[nodiscard]] QString gitExecutable() const;
  [[nodiscard]] QProcessEnvironment gitProcessEnvironment(
      const QProcessEnvironment& overrides = {}) const;

  [[nodiscard]] Repository readRepository(const QString& repositoryPath) const;
  [[nodiscard]] RepositorySummary readRepositorySummary(const QString& repositoryPath) const;

  [[nodiscard]] HistoryPage readHistoryPage(const QString& repositoryPath, int skip = 0,
                                            int limit = 50,
                                            const QString& anchor = {}, bool allBranches = false,
                                            const QString& reference = {},
                                            bool topological = true) const;
  // Commits in the same scope as readHistoryPage whose message, author name
  // or email contains `query` (case-insensitive, literal), or whose hash
  // starts with it. Newest first, at most `limit`; `truncated` reports more.
  struct SearchResult {
    QList<HistoryCommit> commits;
    bool truncated{};
  };
  [[nodiscard]] SearchResult searchHistory(const QString& repositoryPath, const QString& query,
                                           bool allBranches = false, const QString& reference = {},
                                           int limit = 200) const;
  [[nodiscard]] CommitDetail readCommitDetail(const QString& repositoryPath,
                                              const QString& requestedHash) const;
  [[nodiscard]] QString readCommitFileDiff(const QString& repositoryPath,
                                           const QString& requestedHash,
                                           const QString& filePath) const;
  [[nodiscard]] QString getFileDiff(const QString& repositoryPath,
                                    const QString& filePath) const;

  [[nodiscard]] FilePreview readFilePreview(const QString& repositoryPath, const QString& filePath, const QString& commit = {}) const;

  void commitFiles(const QString& repositoryPath, const QStringList& files,
                   const QString& summary, const QString& description,
                   const Account& account) const;
  void fetchOrigin(const QString& repositoryPath, const QString& token = {},
                   const QString& handle = {}, const QString& sshCommand = {}, bool allBranches = false) const;
  void pushOrigin(const QString& repositoryPath, const QString& token = {},
                  const QString& handle = {}, const QString& sshCommand = {}) const;
  // Fetches, then fast-forwards to the origin upstream. Returns the upstream
  // ref (refs/remotes/origin/...) when local and origin have diverged and
  // nothing was changed; an empty string otherwise.
  [[nodiscard]] QString pullOrigin(const QString& repositoryPath, const QString& token = {},
                                   const QString& handle = {}, const QString& sshCommand = {}) const;
  void performAction(const QString& repositoryPath, RepositoryAction action,
                     const QString& target = {}, const QStringList& paths = {},
                     const Account& account = {}) const;
  [[nodiscard]] Repository createRepository(const QString& destinationPath) const;
  void setOriginRemote(const QString& repositoryPath, const QString& remote, bool replace = false) const;
  void createBranch(const QString& repositoryPath, const QString& branch, const QString& startPoint = {}) const;
  [[nodiscard]] Repository cloneRepository(const QString& remoteUrl,
                                           const QString& destinationPath,
                                           const QString& token = {},
                                           const QString& handle = {},
                                           const QString& sshCommand = {}) const;
  void switchBranch(const QString& repositoryPath, const QString& branch) const;

  [[nodiscard]] QString originRemoteUrl(const QString& repositoryPath, bool forPush = false) const;
  [[nodiscard]] std::optional<QDateTime> latestCommitDate(const QString& root) const;
  [[nodiscard]] std::optional<QDateTime> firstCommitDate(const QString& root) const;

  [[nodiscard]] static QString githubCredentialHelper();
  [[nodiscard]] static bool isSshRemote(const QString& remote);
  [[nodiscard]] static QList<ChangedFile> parseStatus(const QString& statusText,
                                                      const QString& statText,
                                                      const QString& root);
  [[nodiscard]] static QList<HistoryItem> parseHistory(const QString& logText);
  [[nodiscard]] static QList<HistoryCommit> parseHistoryPage(const QString& logText);

 private:
  [[nodiscard]] QString runGit(const QString& repositoryPath, const QStringList& arguments,
                               const QProcessEnvironment& overrides = {}, bool preserveOutput = false, bool literalPaths = true, const QByteArray& standardInput = {}) const;
  [[nodiscard]] QString runGitWithoutRepository(
      const QStringList& arguments, const QProcessEnvironment& overrides = {},
      const QString& workingDirectory = {}) const;
  [[nodiscard]] QString runGitOrEmpty(const QString& repositoryPath,
                                      const QStringList& arguments) const;
  [[nodiscard]] QString assertCommitInRepository(const QString& repositoryPath,
                                                 const QString& requestedHash) const;

  [[nodiscard]] QStringList expandedChangedPaths(const QString& root, const QStringList& paths) const;
  void readOperationState(Repository& repository) const;
  void requireIdle(const QString& repositoryPath, bool clean = false) const;

  QString resourcesPath_;
  QString sourceRoot_;
  // Recent history windows, shared by copies of this service. Guarded by its
  // own mutex because pages are read from worker threads.
  std::shared_ptr<struct HistoryCache> historyCache_;
};

}  // namespace relay
