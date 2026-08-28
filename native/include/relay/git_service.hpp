#pragma once

#include "relay/domain.hpp"

#include <QHash>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <optional>

namespace relay {

// Native port of electron/git-service.cjs. The methods are synchronous because
// a repository read is a composition of several child processes; callers must
// invoke them from a worker thread (or wrap them in QtConcurrent) rather than
// block the GUI thread.
class GitService final {
 public:
  static constexpr int historyBatchLimit = 200;

  explicit GitService(QString resourcesPath = {}, QString sourceRoot = {});

  [[nodiscard]] QString gitExecutable() const;
  [[nodiscard]] QProcessEnvironment gitProcessEnvironment(
      const QProcessEnvironment& overrides = {}) const;

  [[nodiscard]] Repository readRepository(const QString& repositoryPath) const;
  [[nodiscard]] RepositorySummary readRepositorySummary(const QString& repositoryPath) const;

  [[nodiscard]] HistoryPage readHistoryPage(const QString& repositoryPath, int skip = 0,
                                            int limit = 50,
                                            const QString& anchor = {}) const;
  [[nodiscard]] CommitDetail readCommitDetail(const QString& repositoryPath,
                                              const QString& requestedHash) const;
  [[nodiscard]] QString readCommitFileDiff(const QString& repositoryPath,
                                           const QString& requestedHash,
                                           const QString& filePath) const;
  [[nodiscard]] QString getFileDiff(const QString& repositoryPath,
                                    const QString& filePath) const;

  void commitFiles(const QString& repositoryPath, const QStringList& files,
                   const QString& summary, const QString& description,
                   const Account& account) const;
  void fetchOrigin(const QString& repositoryPath, const QString& token = {},
                   const QString& handle = {}, const QString& sshCommand = {}) const;
  void pushOrigin(const QString& repositoryPath, const QString& token = {},
                  const QString& handle = {}, const QString& sshCommand = {}) const;
  [[nodiscard]] Repository cloneRepository(const QString& remoteUrl,
                                           const QString& destinationPath,
                                           const QString& token = {},
                                           const QString& handle = {},
                                           const QString& sshCommand = {}) const;
  void switchBranch(const QString& repositoryPath, const QString& branch) const;

  [[nodiscard]] QString originRemoteUrl(const QString& repositoryPath) const;
  [[nodiscard]] std::optional<QDateTime> latestCommitDate(const QString& root) const;
  [[nodiscard]] std::optional<QDateTime> firstCommitDate(const QString& root) const;

  [[nodiscard]] static bool isSshRemote(const QString& remote);
  [[nodiscard]] static QList<ChangedFile> parseStatus(const QString& statusText,
                                                      const QString& statText,
                                                      const QString& root);
  [[nodiscard]] static QList<HistoryItem> parseHistory(const QString& logText);
  [[nodiscard]] static QList<HistoryCommit> parseHistoryPage(const QString& logText);

 private:
  [[nodiscard]] QString runGit(const QString& repositoryPath, const QStringList& arguments,
                               const QProcessEnvironment& overrides = {}) const;
  [[nodiscard]] QString runGitWithoutRepository(
      const QStringList& arguments, const QProcessEnvironment& overrides = {},
      const QString& workingDirectory = {}) const;
  [[nodiscard]] QString runGitOrEmpty(const QString& repositoryPath,
                                      const QStringList& arguments) const;
  [[nodiscard]] QString assertCommitInRepository(const QString& repositoryPath,
                                                 const QString& requestedHash) const;

  QString resourcesPath_;
  QString sourceRoot_;
};

}  // namespace relay
