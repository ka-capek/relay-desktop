#pragma once

#include "relay/forge_types.hpp"

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace relay {

enum class FileStatus { added, modified, deleted };

struct Account {
  QString id;
  qint64 githubId{};
  QString githubIdText;
  QString name;
  QString handle;
  QString email;
  QString initials;
  QString tone;
  QString status;
  QString avatarUrl;
  QString avatarData;
  QString authSource{QStringLiteral("github-cli")};
  QString tokenSource{QStringLiteral("credential store")};
  bool active{};
  // Path to an SSH key that signs this account's commits. Empty follows the
  // user's Git configuration. Never key contents.
  QString signingKey;
};

struct RepositorySummary {
  QString path;
  QString name;
  QString owner;
  QString branch;
  qsizetype changes{};
  std::optional<QDateTime> lastOpened;
  std::optional<QDateTime> addedAt;
  std::optional<QDateTime> latestCommit;
  std::optional<QDateTime> firstCommit;
  bool operator==(const RepositorySummary&) const = default;
};

struct SshProfile {
  QString id;
  QString label;
  QString host;
  QString user;
  std::optional<quint16> port;
  QString identityFile;
  bool identitiesOnly{true};
};

enum class RepositoryOrderMode { manual, age, name, latest };
enum class SortDirection { ascending, descending };

struct RepositoryOrder {
  RepositoryOrderMode mode{RepositoryOrderMode::manual};
  SortDirection direction{SortDirection::ascending};
  bool operator==(const RepositoryOrder&) const = default;
};

struct ChangedFile {
  QString path;
  QString name;
  QString directory;
  FileStatus status{FileStatus::modified};
  qsizetype added{};
  qsizetype removed{};
  bool binary{};
};

struct FilePreview {
  QString diff;
  QImage before;
  QImage after;
};

struct HistoryItem {
  QString fullHash;
  QString hash;
  QString title;
  QString author;
  QString email;
  QDateTime date;
};

struct HistoryCommit : HistoryItem {
  QStringList parents;
  QStringList refs;
};

struct HistoryPage {
  QList<HistoryCommit> commits;
  QString head;
  QString anchor;
  bool endOfHistory{};
};

struct CommitDetail {
  QString fullHash;
  QString hash;
  QString title;
  QString body;
  QString author;
  QString authorEmail;
  QDateTime authorDate;
  QString committer;
  QString committerEmail;
  QDateTime committerDate;
  QStringList parents;
  QStringList refs;
  bool isRoot{};
  bool isMerge{};
  QList<ChangedFile> files;
  qsizetype added{};
  qsizetype removed{};
  // The commit carries a GPG, SSH or X.509 signature. Not verified.
  bool isSigned{};
};

// Two branches compared the way a pull request compares them: commits only
// on each side, and the files compare changed since their merge base.
struct BranchComparison {
  QString base;     // refs/heads/... or refs/remotes/...
  QString compare;
  QString baseHash;
  QString compareHash;
  QString mergeBase;  // empty for unrelated histories
  QList<HistoryCommit> ahead;   // in compare, not in base; newest first
  QList<HistoryCommit> behind;  // in base, not in compare
  bool truncated{};             // either list stopped at its limit
  QList<ChangedFile> files;
  qsizetype added{};
  qsizetype removed{};
};

// A commit on the current branch that no remote branch contains yet.
struct UnpublishedCommit {
  QString fullHash;
  QString hash;
  QString message;  // subject and body
  bool merge{};
};

// One line of a rewrite, oldest first. A squash step joins the step before
// it. message, when set on the first step of a group, replaces its message.
struct CommitRewriteStep {
  QString hash;
  bool squash{};
  QString message;
};

enum class RepositoryAction {
  renameBranch, deleteBranch, checkoutRemote, mergeBranch, rebaseBranch, abortOperation, skipOperation,
  continueOperation, resolveOurs, resolveTheirs, markResolved, stash,
  applyStash, dropStash, discardFiles, revertCommit, cherryPick, undoCommit, setOrigin, amendMessage, createTag, deleteTag,
  // A rebase the user confirmed although it rewrites commits on a remote branch.
  rebasePublished
};

// What rebasing the current branch onto a ref would replay.
struct RebasePlan {
  QString onto;
  int replayed{};
  int published{};  // of those, commits some remote branch already contains
};

struct StashEntry {
  QString hash;
  QString description;
};

struct Repository {
  QString path;
  QString name;
  QString owner;
  QString branch;
  QString remote;
  QStringList branches;
  QList<ChangedFile> files;
  QList<HistoryItem> history;
  int ahead{};
  int behind{};
  bool hasUpstream{};
  std::optional<QDateTime> latestCommit;
  std::optional<QDateTime> firstCommit;
  QStringList remoteBranches;
  QList<StashEntry> stashes;
  QString pendingOperation;
  QStringList conflictedFiles;
  bool hasHead{};
  QString commitName;
  QString commitEmail;
  QString historyRefState;
  QStringList tags;
};

struct Preferences {
  bool refreshOnFocus{true};
  int diffFontSize{12};
  QString commitName;
  QString commitEmail;
  bool graphHistory{true};  // the all-branch graph is the default view
  QString themeId{QStringLiteral("light")};
  QJsonObject customTheme;
  // Window geometry and splitter states, saved by the window itself. Only
  // RelayController::saveLayout() changes it.
  QJsonObject layout;
  // External editor: a detected editor id, "custom" for editorPath, or empty
  // for the first detected editor.
  QString editorId;
  QString editorPath;
};

struct AppState {
  QList<ForgeAccount> forgeAccounts;
  QStringList forgeCredentialCleanup;
  QList<Account> accounts;
  QString activeAccountId;
  QList<RepositorySummary> repositories;
  QHash<QString, QString> repositoryAccounts;
  RepositoryOrder repositoryOrder;
  QStringList manualOrder;
  QList<SshProfile> sshProfiles;
  QHash<QString, QString> repositorySshProfiles;
  Preferences preferences;
};

struct GitHubRepository {
  QString id;
  QString name;
  QString fullName;
  QString owner;
  QString description;
  bool isPrivate{};
  bool archived{};
  bool fork{};
  QString cloneUrl;
  QDateTime updatedAt;
};

struct EmailChoice {
  QString email;
  QString label;
  bool primary{};
  bool noreply{};
};

struct GitHubLoginProgress {
  QString code;
  QString verificationUrl;
  QString message;
  bool browserOpenFailed{};
};

struct SshTestResult {
  bool ok{};
  QString message;
};

QString fileStatusCode(FileStatus status);
QString fileStatusTone(FileStatus status);
FileStatus fileStatusFromGit(const QString& status);

QString repositoryOrderModeName(RepositoryOrderMode mode);
RepositoryOrderMode repositoryOrderModeFromName(const QString& value);
QString sortDirectionName(SortDirection direction);
SortDirection sortDirectionFromName(const QString& value);

QDateTime dateTimeFromJson(const QJsonValue& value);
QJsonValue dateTimeToJson(const std::optional<QDateTime>& value);

Account accountFromJson(const QJsonObject& object);
QJsonObject accountToJson(const Account& account);
RepositorySummary repositorySummaryFromJson(const QJsonObject& object);
QJsonObject repositorySummaryToJson(const RepositorySummary& repository);
SshProfile sshProfileFromJson(const QJsonObject& object);
QJsonObject sshProfileToJson(const SshProfile& profile);
AppState appStateFromJson(const QJsonObject& object);
QJsonObject mergeAppStateIntoJson(const AppState& state, QJsonObject base = {});

}  // namespace relay

Q_DECLARE_METATYPE(relay::Repository)
Q_DECLARE_METATYPE(relay::HistoryPage)
Q_DECLARE_METATYPE(relay::RebasePlan)
Q_DECLARE_METATYPE(relay::BranchComparison)
Q_DECLARE_METATYPE(QList<relay::UnpublishedCommit>)
Q_DECLARE_METATYPE(relay::HistoryCommit)
Q_DECLARE_METATYPE(relay::CommitDetail)

Q_DECLARE_METATYPE(relay::FilePreview)
