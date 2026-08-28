#include "relay/git_service.hpp"

#include "relay/process_runner.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <limits>
#include <utility>

namespace relay {
namespace {

constexpr qsizetype maximumGitOutputBytes = 20 * 1024 * 1024;
constexpr qsizetype maximumUntrackedStatBytes = 2 * 1024 * 1024;

struct RepositoryIdentity {
  QString owner;
  QString name;
};

struct FileStats {
  qsizetype added{};
  qsizetype removed{};
};

QString trimEnd(QString value) {
  while (!value.isEmpty() && value.back().isSpace()) value.chop(1);
  return value;
}

QString cleanGitError(QString value) {
  value = value.trimmed();
  static const QRegularExpression fatalPrefix(QStringLiteral(R"(^fatal:\s*)"),
                                               QRegularExpression::CaseInsensitiveOption);
  value.remove(fatalPrefix);
  return value.isEmpty() ? QStringLiteral("Git command failed") : value;
}

QString fieldAt(const QStringList& fields, const qsizetype index) {
  return index >= 0 && index < fields.size() ? fields.at(index) : QString{};
}

QDateTime gitDate(const QString& value) {
  return QDateTime::fromString(value.trimmed(), Qt::ISODate);
}

std::optional<QDateTime> optionalGitDate(const QString& value) {
  const auto parsed = gitDate(value);
  return parsed.isValid() ? std::optional<QDateTime>(parsed) : std::nullopt;
}

qsizetype numericStat(const QString& value) {
  bool ok = false;
  const auto parsed = value.toLongLong(&ok);
  if (!ok || parsed < 0) return 0;
  const auto maximum = static_cast<qlonglong>(std::numeric_limits<qsizetype>::max());
  return static_cast<qsizetype>(std::min(parsed, maximum));
}

QString displayDirectory(const QString& filePath) {
  const auto directory = QFileInfo(filePath).path();
  return directory == QStringLiteral(".") ? QStringLiteral("Repository root") : directory;
}

RepositoryIdentity repositoryIdentity(QString remote, const QString& root) {
  remote.replace(u'\\', u'/');
  static const QRegularExpression github(
      QStringLiteral(R"(github\.com[/:]([^/]+)/([^/]+?)(?:\.git)?$)"),
      QRegularExpression::CaseInsensitiveOption);
  const auto match = github.match(remote);
  if (match.hasMatch()) return {match.captured(1), match.captured(2)};

  const QDir rootDirectory(root);
  QDir parentDirectory(rootDirectory);
  parentDirectory.cdUp();
  return {parentDirectory.dirName(), rootDirectory.dirName()};
}

QHash<QString, FileStatus> parseNameStatus(const QString& text) {
  QHash<QString, FileStatus> statuses;
  const auto lines = text.split(u'\n', Qt::SkipEmptyParts);
  for (const auto& line : lines) {
    const auto parts = line.split(u'\t', Qt::KeepEmptyParts);
    if (parts.isEmpty() || parts.constFirst().isEmpty()) continue;
    const auto filePath = parts.size() > 2 ? parts.at(2) : fieldAt(parts, 1);
    if (filePath.isEmpty()) continue;
    const auto code = parts.constFirst().front();
    statuses.insert(filePath, code == u'A' ? FileStatus::added
                                           : code == u'D' ? FileStatus::deleted
                                                          : FileStatus::modified);
  }
  return statuses;
}

QStringList nonEmptyLines(const QString& value) {
  return value.split(u'\n', Qt::SkipEmptyParts);
}

QStringList splitRefs(const QString& value) {
  auto refs = value.split(QStringLiteral(", "), Qt::SkipEmptyParts);
  for (auto& ref : refs) ref = ref.trimmed();
  return refs;
}

QProcessEnvironment sshEnvironment(const QString& sshCommand, const QString& remote) {
  QProcessEnvironment environment;
  if (!sshCommand.isEmpty() && GitService::isSshRemote(remote)) {
    environment.insert(QStringLiteral("GIT_SSH_COMMAND"), sshCommand);
  }
  return environment;
}

void insertEnvironment(QProcessEnvironment& destination, const QProcessEnvironment& source) {
  for (const auto& key : source.keys()) destination.insert(key, source.value(key));
}

QString defaultSourceRoot() {
#ifdef RELAY_SOURCE_DIR
  return QStringLiteral(RELAY_SOURCE_DIR);
#else
  return QDir::currentPath();
#endif
}

QString defaultResourcesPath() {
  const auto applicationDirectory = QCoreApplication::instance()
                                        ? QCoreApplication::applicationDirPath()
                                        : QDir::currentPath();
#ifdef Q_OS_MACOS
  return QDir::cleanPath(QDir(applicationDirectory).absoluteFilePath(QStringLiteral("../Resources")));
#elif defined(Q_OS_WIN)
  const auto resources = QDir(applicationDirectory).absoluteFilePath(QStringLiteral("resources"));
  return QDir(resources).exists() ? resources : applicationDirectory;
#else
  return QDir(applicationDirectory).absoluteFilePath(QStringLiteral("resources"));
#endif
}

}  // namespace

GitService::GitService(QString resourcesPath, QString sourceRoot)
    : resourcesPath_(resourcesPath.isEmpty() ? defaultResourcesPath()
                                             : QDir::cleanPath(std::move(resourcesPath))),
      sourceRoot_(sourceRoot.isEmpty() ? defaultSourceRoot()
                                       : QDir::cleanPath(std::move(sourceRoot))) {}

QString GitService::gitExecutable() const {
#ifdef Q_OS_WIN
  const auto packaged = QDir(resourcesPath_).absoluteFilePath(QStringLiteral("git/cmd/git.exe"));
  const auto development =
      QDir(sourceRoot_).absoluteFilePath(QStringLiteral("runtime/git/win-x64/cmd/git.exe"));
#else
  const auto packaged = QDir(resourcesPath_).absoluteFilePath(QStringLiteral("git/bin/git"));
  const auto development =
      QDir(sourceRoot_).absoluteFilePath(QStringLiteral("runtime/git/mac-arm64/bin/git"));
#endif
  if (QFileInfo::exists(packaged)) return packaged;
  if (QFileInfo::exists(development)) return development;
  return QStringLiteral("git");
}

QProcessEnvironment GitService::gitProcessEnvironment(
    const QProcessEnvironment& overrides) const {
  auto environment = QProcessEnvironment::systemEnvironment();
  const auto executable = gitExecutable();
  if (executable != QStringLiteral("git")) {
    const auto root = QDir::cleanPath(QDir(QFileInfo(executable).absolutePath())
                                          .absoluteFilePath(QStringLiteral("..")));
    QStringList bundledPaths;
#ifdef Q_OS_WIN
    bundledPaths = {
        QDir(root).absoluteFilePath(QStringLiteral("cmd")),
        QDir(root).absoluteFilePath(QStringLiteral("mingw64/bin")),
        QDir(root).absoluteFilePath(QStringLiteral("usr/bin")),
    };
    environment.insert(QStringLiteral("GIT_EXEC_PATH"),
                       QDir(root).absoluteFilePath(QStringLiteral("mingw64/libexec/git-core")));
    environment.insert(QStringLiteral("GIT_TEMPLATE_DIR"),
                       QDir(root).absoluteFilePath(
                           QStringLiteral("mingw64/share/git-core/templates")));
    environment.insert(QStringLiteral("GIT_SSL_CAINFO"),
                       QDir(root).absoluteFilePath(
                           QStringLiteral("mingw64/etc/ssl/certs/ca-bundle.crt")));
#else
    bundledPaths = {
        QDir(root).absoluteFilePath(QStringLiteral("bin")),
        QDir(root).absoluteFilePath(QStringLiteral("libexec/git-core")),
    };
    environment.insert(QStringLiteral("GIT_EXEC_PATH"),
                       QDir(root).absoluteFilePath(QStringLiteral("libexec/git-core")));
    environment.insert(QStringLiteral("GIT_TEMPLATE_DIR"),
                       QDir(root).absoluteFilePath(QStringLiteral("share/git-core/templates")));
#endif
    environment.insert(QStringLiteral("GIT_CONFIG_SYSTEM"),
                       QDir(root).absoluteFilePath(QStringLiteral("etc/gitconfig")));
    const auto inheritedPath = environment.value(QStringLiteral("PATH"));
    if (!inheritedPath.isEmpty()) bundledPaths.push_back(inheritedPath);
    environment.insert(QStringLiteral("PATH"), bundledPaths.join(QDir::listSeparator()));
  }
  insertEnvironment(environment, overrides);
  return environment;
}

QString GitService::runGit(const QString& repositoryPath, const QStringList& arguments,
                           const QProcessEnvironment& overrides) const {
  QStringList processArguments{QStringLiteral("-C"), repositoryPath};
  processArguments.append(arguments);
  ProcessRequest request{gitExecutable(), processArguments};
  request.environment = gitProcessEnvironment(overrides);
  request.timeoutMilliseconds = -1;
  request.maximumOutputBytes = maximumGitOutputBytes;
  try {
    return trimEnd(QString::fromUtf8(ProcessRunner::run(request).standardOutput));
  } catch (const ProcessError& error) {
    throw ProcessError(cleanGitError(error.qMessage()), error.result());
  }
}

QString GitService::runGitWithoutRepository(
    const QStringList& arguments, const QProcessEnvironment& overrides,
    const QString& workingDirectory) const {
  ProcessRequest request{gitExecutable(), arguments};
  request.workingDirectory = workingDirectory;
  request.environment = gitProcessEnvironment(overrides);
  request.timeoutMilliseconds = -1;
  request.maximumOutputBytes = maximumGitOutputBytes;
  try {
    return trimEnd(QString::fromUtf8(ProcessRunner::run(request).standardOutput));
  } catch (const ProcessError& error) {
    throw ProcessError(cleanGitError(error.qMessage()), error.result());
  }
}

QString GitService::runGitOrEmpty(const QString& repositoryPath,
                                  const QStringList& arguments) const {
  try {
    return runGit(repositoryPath, arguments);
  } catch (const ProcessError&) {
    return {};
  }
}

QList<ChangedFile> GitService::parseStatus(const QString& statusText, const QString& statText,
                                           const QString& root) {
  QHash<QString, FileStats> stats;
  for (const auto& line : statText.split(u'\n', Qt::SkipEmptyParts)) {
    const auto fields = line.split(u'\t', Qt::KeepEmptyParts);
    if (fields.size() < 3) continue;
    const auto filePath = QStringList(fields.cbegin() + 2, fields.cend()).join(u'\t');
    stats.insert(filePath, {numericStat(fields.at(0)), numericStat(fields.at(1))});
  }

  QList<ChangedFile> files;
  for (const auto& line : statusText.split(u'\n', Qt::SkipEmptyParts)) {
    const auto code = line.left(2);
    auto filePath = line.mid(3).trimmed();
    const auto renameSeparator = filePath.lastIndexOf(QStringLiteral(" -> "));
    if (renameSeparator >= 0) filePath = filePath.mid(renameSeparator + 4);
    if (filePath.size() >= 2 && filePath.front() == u'"' && filePath.back() == u'"') {
      filePath = filePath.mid(1, filePath.size() - 2);
    }

    auto values = stats.value(filePath);
    if (code == QStringLiteral("??") && values.added == 0) {
      QFile file(QDir(root).absoluteFilePath(filePath));
      if (file.open(QIODevice::ReadOnly) && file.size() < maximumUntrackedStatBytes) {
        const auto buffer = file.readAll();
        if (!buffer.contains('\0')) values.added = QString::fromUtf8(buffer).split(u'\n').size();
      }
    }

    const auto status = fileStatusFromGit(code);
    files.push_back({filePath, QFileInfo(filePath).fileName(), displayDirectory(filePath), status,
                     values.added, values.removed, false});
  }
  return files;
}

QList<HistoryItem> GitService::parseHistory(const QString& logText) {
  QList<HistoryItem> history;
  const auto records = logText.split(QChar(0x1e), Qt::SkipEmptyParts);
  for (const auto& untrimmed : records) {
    const auto record = untrimmed.trimmed();
    if (record.isEmpty()) continue;
    const auto fields = record.split(QChar(0x1f), Qt::KeepEmptyParts);
    history.push_back({fieldAt(fields, 0), fieldAt(fields, 1), fieldAt(fields, 2),
                       fieldAt(fields, 3), fieldAt(fields, 4), gitDate(fieldAt(fields, 5))});
  }
  return history;
}

QList<HistoryCommit> GitService::parseHistoryPage(const QString& logText) {
  QList<HistoryCommit> history;
  const auto records = logText.split(QChar(0x1e), Qt::SkipEmptyParts);
  for (const auto& untrimmed : records) {
    const auto record = untrimmed.trimmed();
    if (record.isEmpty()) continue;
    const auto fields = record.split(QChar(0x1f), Qt::KeepEmptyParts);
    HistoryCommit commit;
    commit.fullHash = fieldAt(fields, 0);
    commit.hash = fieldAt(fields, 1);
    commit.parents = fieldAt(fields, 2).split(u' ', Qt::SkipEmptyParts);
    commit.title = fieldAt(fields, 3);
    commit.author = fieldAt(fields, 4);
    commit.email = fieldAt(fields, 5);
    commit.date = gitDate(fieldAt(fields, 6));
    commit.refs = splitRefs(fieldAt(fields, 7));
    history.push_back(std::move(commit));
  }
  return history;
}

std::optional<QDateTime> GitService::latestCommitDate(const QString& root) const {
  return optionalGitDate(runGitOrEmpty(root, {QStringLiteral("log"), QStringLiteral("-1"),
                                               QStringLiteral("--format=%cI")}));
}

std::optional<QDateTime> GitService::firstCommitDate(const QString& root) const {
  auto dates = nonEmptyLines(runGitOrEmpty(
      root, {QStringLiteral("log"), QStringLiteral("--max-parents=0"),
             QStringLiteral("--format=%cI")}));
  for (auto& date : dates) date = date.trimmed();
  dates.removeAll(QString{});
  dates.sort();
  return dates.isEmpty() ? std::nullopt : optionalGitDate(dates.constFirst());
}

Repository GitService::readRepository(const QString& repositoryPath) const {
  const auto root = runGit(repositoryPath,
                           {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
  const auto branch =
      runGitOrEmpty(root, {QStringLiteral("branch"), QStringLiteral("--show-current")});
  const auto status = runGit(
      root, {QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
             QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
             QStringLiteral("--untracked-files=all")});
  const auto remote = runGitOrEmpty(
      root, {QStringLiteral("remote"), QStringLiteral("get-url"), QStringLiteral("origin")});
  const auto branches = runGitOrEmpty(
      root, {QStringLiteral("for-each-ref"), QStringLiteral("--format=%(refname:short)"),
             QStringLiteral("refs/heads/")});
  const auto historyText = runGitOrEmpty(
      root, {QStringLiteral("log"), QStringLiteral("-30"),
             QStringLiteral("--pretty=format:%H%x1f%h%x1f%s%x1f%an%x1f%ae%x1f%aI%x1e")});
  const auto latestCommit = latestCommitDate(root);
  const auto firstCommit = firstCommitDate(root);

  QString statText;
  try {
    statText = runGit(root, {QStringLiteral("diff"), QStringLiteral("--numstat"),
                             QStringLiteral("HEAD"), QStringLiteral("--")});
  } catch (const ProcessError&) {
    statText = runGitOrEmpty(root, {QStringLiteral("diff"), QStringLiteral("--numstat"),
                                    QStringLiteral("--cached"), QStringLiteral("--")});
  }

  int ahead = 0;
  int behind = 0;
  bool hasUpstream = false;
  try {
    const auto counts = runGit(
        root, {QStringLiteral("rev-list"), QStringLiteral("--left-right"),
               QStringLiteral("--count"), QStringLiteral("HEAD...@{upstream}")});
    const auto values = counts.split(QRegularExpression(QStringLiteral(R"(\s+)")),
                                     Qt::SkipEmptyParts);
    if (values.size() >= 2) {
      bool aheadOk = false;
      bool behindOk = false;
      const auto parsedAhead = values.at(0).toInt(&aheadOk);
      const auto parsedBehind = values.at(1).toInt(&behindOk);
      ahead = aheadOk ? parsedAhead : 0;
      behind = behindOk ? parsedBehind : 0;
    }
    hasUpstream = true;
  } catch (const ProcessError&) {
    if (!remote.isEmpty() && !branch.isEmpty()) {
      try {
        const auto counts = runGit(
            root,
            {QStringLiteral("rev-list"), QStringLiteral("--left-right"),
             QStringLiteral("--count"),
             QStringLiteral("HEAD...refs/remotes/origin/%1").arg(branch)});
        const auto values = counts.split(QRegularExpression(QStringLiteral(R"(\s+)")),
                                         Qt::SkipEmptyParts);
        if (values.size() >= 2) {
          bool aheadOk = false;
          bool behindOk = false;
          const auto parsedAhead = values.at(0).toInt(&aheadOk);
          const auto parsedBehind = values.at(1).toInt(&behindOk);
          ahead = aheadOk ? parsedAhead : 0;
          behind = behindOk ? parsedBehind : 0;
        }
      } catch (const ProcessError&) {
        ahead = historyText.isEmpty() ? 0 : 1;
      }
    }
  }

  const auto identity = repositoryIdentity(remote, root);
  return {root,
          identity.name,
          identity.owner,
          branch.isEmpty() ? QStringLiteral("detached HEAD") : branch,
          remote,
          nonEmptyLines(branches),
          parseStatus(status, statText, root),
          parseHistory(historyText),
          ahead,
          behind,
          hasUpstream,
          latestCommit,
          firstCommit};
}

RepositorySummary GitService::readRepositorySummary(const QString& repositoryPath) const {
  const auto root = runGit(repositoryPath,
                           {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
  const auto branch =
      runGitOrEmpty(root, {QStringLiteral("branch"), QStringLiteral("--show-current")});
  const auto status = runGit(
      root, {QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
             QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
             QStringLiteral("--untracked-files=all")});
  const auto remote = runGitOrEmpty(
      root, {QStringLiteral("remote"), QStringLiteral("get-url"), QStringLiteral("origin")});
  const auto identity = repositoryIdentity(remote, root);
  return {root,
          identity.name,
          identity.owner,
          branch.isEmpty() ? QStringLiteral("detached HEAD") : branch,
          nonEmptyLines(status).size(),
          QDateTime::currentDateTimeUtc(),
          std::nullopt,
          latestCommitDate(root),
          firstCommitDate(root)};
}

QString GitService::assertCommitInRepository(const QString& repositoryPath,
                                             const QString& requestedHash) const {
  const auto hash = requestedHash.trimmed();
  static const QRegularExpression validHash(QStringLiteral(R"(^[0-9a-f]{7,40}$)"),
                                             QRegularExpression::CaseInsensitiveOption);
  if (!validHash.match(hash).hasMatch()) throw ProcessError(QStringLiteral("Invalid commit hash."));
  try {
    static_cast<void>(runGit(repositoryPath,
                             {QStringLiteral("cat-file"), QStringLiteral("-e"),
                              QStringLiteral("%1^{commit}").arg(hash)}));
  } catch (const ProcessError&) {
    throw ProcessError(QStringLiteral("That commit is not in this repository."));
  }
  return hash;
}

HistoryPage GitService::readHistoryPage(const QString& repositoryPath, const int skip,
                                        const int limit, const QString& requestedAnchor) const {
  const auto root = runGit(repositoryPath,
                           {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
  const auto head =
      runGitOrEmpty(root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
  if (head.isEmpty()) return {{}, {}, {}, true};

  const auto boundedSkip = std::max(0, skip);
  const auto boundedLimit = std::clamp(limit == 0 ? 50 : limit, 1, historyBatchLimit);
  const auto anchor = requestedAnchor.isEmpty()
                          ? head
                          : assertCommitInRepository(root, requestedAnchor);
  const auto logText = runGitOrEmpty(
      root,
      {QStringLiteral("log"), anchor, QStringLiteral("--skip=%1").arg(boundedSkip),
       QStringLiteral("-n"), QString::number(boundedLimit),
       QStringLiteral(
           "--pretty=format:%H%x1f%h%x1f%P%x1f%s%x1f%an%x1f%ae%x1f%aI%x1f%D%x1e")});
  auto commits = parseHistoryPage(logText);
  const auto endOfHistory = commits.size() < boundedLimit;
  return {std::move(commits), head, anchor, endOfHistory};
}

CommitDetail GitService::readCommitDetail(const QString& repositoryPath,
                                          const QString& requestedHash) const {
  const auto root = runGit(repositoryPath,
                           {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
  const auto hash = assertCommitInRepository(root, requestedHash);
  const auto record = runGit(
      root,
      {QStringLiteral("show"), QStringLiteral("--no-patch"),
       QStringLiteral(
           "--format=%H%x1f%h%x1f%P%x1f%s%x1f%b%x1f%an%x1f%ae%x1f%aI%x1f%cn%x1f%ce%x1f%cI%x1f%D"),
       hash});
  const auto fields = record.split(QChar(0x1f), Qt::KeepEmptyParts);
  const auto fullHash = fieldAt(fields, 0);
  const auto parents = fieldAt(fields, 2).trimmed().split(u' ', Qt::SkipEmptyParts);
  const auto isRoot = parents.isEmpty();

  QStringList compareArguments;
  if (isRoot) {
    compareArguments = {QStringLiteral("diff-tree"), QStringLiteral("--root"),
                        QStringLiteral("-r"), QStringLiteral("--no-commit-id"),
                        QStringLiteral("-M"), fullHash};
  } else {
    compareArguments = {QStringLiteral("diff"), QStringLiteral("-M"), parents.constFirst(),
                        fullHash};
  }

  auto numstatArguments = compareArguments;
  numstatArguments.append({QStringLiteral("--numstat"), QStringLiteral("--")});
  auto nameStatusArguments = compareArguments;
  nameStatusArguments.append({QStringLiteral("--name-status"), QStringLiteral("--")});
  const auto numstatText = runGitOrEmpty(root, numstatArguments);
  const auto nameStatusText = runGitOrEmpty(root, nameStatusArguments);
  const auto statuses = parseNameStatus(nameStatusText);

  QList<ChangedFile> files;
  qsizetype totalAdded = 0;
  qsizetype totalRemoved = 0;
  for (const auto& line : numstatText.split(u'\n', Qt::SkipEmptyParts)) {
    const auto parts = line.split(u'\t', Qt::KeepEmptyParts);
    if (parts.size() < 3) continue;
    const auto pathParts = QStringList(parts.cbegin() + 2, parts.cend());
    const auto filePath = pathParts.size() > 1 ? pathParts.constLast() : pathParts.constFirst();
    const auto added = numericStat(parts.at(0));
    const auto removed = numericStat(parts.at(1));
    const auto status = statuses.value(filePath, FileStatus::modified);
    files.push_back({filePath, QFileInfo(filePath).fileName(), displayDirectory(filePath), status,
                     added, removed,
                     parts.at(0) == QStringLiteral("-") || parts.at(1) == QStringLiteral("-")});
    totalAdded += added;
    totalRemoved += removed;
  }

  return {fullHash,
          fieldAt(fields, 1),
          fieldAt(fields, 3),
          fieldAt(fields, 4).trimmed(),
          fieldAt(fields, 5),
          fieldAt(fields, 6),
          gitDate(fieldAt(fields, 7)),
          fieldAt(fields, 8),
          fieldAt(fields, 9),
          gitDate(fieldAt(fields, 10)),
          parents,
          splitRefs(fieldAt(fields, 11)),
          isRoot,
          parents.size() > 1,
          std::move(files),
          totalAdded,
          totalRemoved};
}

QString GitService::readCommitFileDiff(const QString& repositoryPath,
                                       const QString& requestedHash,
                                       const QString& filePath) const {
  const auto root = runGit(repositoryPath,
                           {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
  const auto hash = assertCommitInRepository(root, requestedHash);
  const auto target = filePath;
  if (target.isEmpty()) throw ProcessError(QStringLiteral("Choose a file to compare."));

  const auto parents = runGit(
                           root, {QStringLiteral("show"), QStringLiteral("--no-patch"),
                                  QStringLiteral("--format=%P"), hash})
                           .trimmed()
                           .split(u' ', Qt::SkipEmptyParts);
  QStringList arguments{QStringLiteral("-c"), QStringLiteral("core.quotepath=false")};
  if (parents.isEmpty()) {
    arguments.append({QStringLiteral("diff-tree"), QStringLiteral("--root"),
                      QStringLiteral("-r"), QStringLiteral("--no-commit-id"),
                      QStringLiteral("-M"), QStringLiteral("--no-ext-diff"),
                      QStringLiteral("--unified=3"), QStringLiteral("-p"), hash});
  } else {
    arguments.append({QStringLiteral("diff"), QStringLiteral("-M"),
                      QStringLiteral("--no-ext-diff"), QStringLiteral("--unified=3"),
                      parents.constFirst(), hash});
  }
  arguments.append({QStringLiteral("--"), target});
  return runGit(root, arguments);
}

QString GitService::getFileDiff(const QString& repositoryPath, const QString& filePath) const {
  const auto status = runGit(
      repositoryPath,
      {QStringLiteral("-c"), QStringLiteral("core.quotepath=false"), QStringLiteral("status"),
       QStringLiteral("--porcelain=v1"), QStringLiteral("--"), filePath});
  if (status.startsWith(QStringLiteral("??"))) {
    QFile file(QDir(repositoryPath).absoluteFilePath(filePath));
    if (!file.open(QIODevice::ReadOnly)) {
      throw ProcessError(
          QStringLiteral("%1 could not be read: %2").arg(filePath, file.errorString()));
    }
    const auto buffer = file.readAll();
    if (buffer.contains('\0')) return QStringLiteral("Binary file — preview unavailable");
    const auto lines = QString::fromUtf8(buffer).split(u'\n', Qt::KeepEmptyParts);
    QStringList diff{QStringLiteral("--- /dev/null"), QStringLiteral("+++ b/%1").arg(filePath),
                     QStringLiteral("@@ -0,0 +1,%1 @@").arg(lines.size())};
    for (const auto& line : lines) diff.push_back(QStringLiteral("+") + line);
    return diff.join(u'\n');
  }

  try {
    return runGit(repositoryPath,
                  {QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
                   QStringLiteral("diff"), QStringLiteral("HEAD"),
                   QStringLiteral("--no-ext-diff"), QStringLiteral("--unified=3"),
                   QStringLiteral("--"), filePath});
  } catch (const ProcessError&) {
    return runGit(repositoryPath,
                  {QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
                   QStringLiteral("diff"), QStringLiteral("--cached"),
                   QStringLiteral("--no-ext-diff"), QStringLiteral("--unified=3"),
                   QStringLiteral("--"), filePath});
  }
}

bool GitService::isSshRemote(const QString& remote) {
  const auto value = remote.trimmed();
  if (value.startsWith(QStringLiteral("ssh://"), Qt::CaseInsensitive)) return true;
  static const QRegularExpression scpLike(QStringLiteral(R"(^[^@\s]+@[^@:\s/\\]+:)"));
  static const QRegularExpression windowsDrive(QStringLiteral(R"(^[a-zA-Z]:[\\/])"));
  return scpLike.match(value).hasMatch() && !windowsDrive.match(value).hasMatch();
}

QString GitService::originRemoteUrl(const QString& repositoryPath) const {
  return runGitOrEmpty(repositoryPath,
                       {QStringLiteral("remote"), QStringLiteral("get-url"),
                        QStringLiteral("origin")});
}

void GitService::commitFiles(const QString& repositoryPath, const QStringList& files,
                             const QString& summary, const QString& description,
                             const Account& account) const {
  if (files.isEmpty()) throw ProcessError(QStringLiteral("Select at least one changed file."));
  if (summary.trimmed().isEmpty()) throw ProcessError(QStringLiteral("Enter a commit summary."));

  QStringList addArguments{QStringLiteral("add"), QStringLiteral("--")};
  addArguments.append(files);
  static_cast<void>(runGit(repositoryPath, addArguments));

  QStringList arguments{QStringLiteral("-c"), QStringLiteral("user.name=%1").arg(account.name),
                        QStringLiteral("-c"), QStringLiteral("user.email=%1").arg(account.email),
                        QStringLiteral("commit"), QStringLiteral("--only")};
  arguments.append({QStringLiteral("-m"), summary.trimmed()});
  if (!description.trimmed().isEmpty()) {
    arguments.append({QStringLiteral("-m"), description.trimmed()});
  }
  arguments.push_back(QStringLiteral("--"));
  arguments.append(files);

  QProcessEnvironment environment;
  environment.insert(QStringLiteral("GIT_AUTHOR_NAME"), account.name);
  environment.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), account.email);
  environment.insert(QStringLiteral("GIT_COMMITTER_NAME"), account.name);
  environment.insert(QStringLiteral("GIT_COMMITTER_EMAIL"), account.email);
  static_cast<void>(runGit(repositoryPath, arguments, environment));
}

void GitService::fetchOrigin(const QString& repositoryPath, const QString& token,
                             const QString& handle, const QString& sshCommand) const {
  const auto remote = originRemoteUrl(repositoryPath);
  if (remote.isEmpty()) {
    throw ProcessError(QStringLiteral("This repository does not have an origin remote."));
  }

  QStringList arguments;
  auto environment = sshEnvironment(sshCommand, remote);
  environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
  static const QRegularExpression githubHttps(QStringLiteral(R"(^https://github\.com/)"),
                                               QRegularExpression::CaseInsensitiveOption);
  if (!token.isEmpty() && githubHttps.match(remote).hasMatch()) {
    const auto username = handle.isEmpty() ? QStringLiteral("x-access-token") : handle;
    arguments.append(
        {QStringLiteral("-c"), QStringLiteral("credential.helper="), QStringLiteral("-c"),
         QStringLiteral(
             "credential.helper=!f() { echo username=%1; echo password=$RELAY_GIT_TOKEN; }; f")
             .arg(username)});
    environment.insert(QStringLiteral("RELAY_GIT_TOKEN"), token);
  }
  arguments.append(
      {QStringLiteral("fetch"), QStringLiteral("origin"), QStringLiteral("--prune")});
  static_cast<void>(runGit(repositoryPath, arguments, environment));
}

void GitService::pushOrigin(const QString& repositoryPath, const QString& token,
                            const QString& handle, const QString& sshCommand) const {
  const auto remote = originRemoteUrl(repositoryPath);
  if (remote.isEmpty()) {
    throw ProcessError(QStringLiteral("This repository does not have an origin remote."));
  }
  const auto branch =
      runGit(repositoryPath, {QStringLiteral("branch"), QStringLiteral("--show-current")});
  if (branch.isEmpty()) throw ProcessError(QStringLiteral("Switch to a branch before pushing."));

  QStringList arguments;
  auto environment = sshEnvironment(sshCommand, remote);
  environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
  static const QRegularExpression githubHttps(QStringLiteral(R"(^https://github\.com/)"),
                                               QRegularExpression::CaseInsensitiveOption);
  if (!token.isEmpty() && githubHttps.match(remote).hasMatch()) {
    const auto username = handle.isEmpty() ? QStringLiteral("x-access-token") : handle;
    arguments.append(
        {QStringLiteral("-c"), QStringLiteral("credential.helper="), QStringLiteral("-c"),
         QStringLiteral(
             "credential.helper=!f() { echo username=%1; echo password=$RELAY_GIT_TOKEN; }; f")
             .arg(username)});
    environment.insert(QStringLiteral("RELAY_GIT_TOKEN"), token);
  }
  arguments.append({QStringLiteral("push"), QStringLiteral("--set-upstream"),
                    QStringLiteral("origin"), QStringLiteral("HEAD")});
  static_cast<void>(runGit(repositoryPath, arguments, environment));
}

Repository GitService::cloneRepository(const QString& remoteUrl, const QString& destinationPath,
                                       const QString& token, const QString& handle,
                                       const QString& sshCommand) const {
  QStringList arguments;
  auto environment = sshEnvironment(sshCommand, remoteUrl);
  environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
  static const QRegularExpression githubHttps(QStringLiteral(R"(^https://github\.com/)"),
                                               QRegularExpression::CaseInsensitiveOption);
  if (!token.isEmpty() && githubHttps.match(remoteUrl).hasMatch()) {
    const auto username = handle.isEmpty() ? QStringLiteral("x-access-token") : handle;
    arguments.append(
        {QStringLiteral("-c"), QStringLiteral("credential.helper="), QStringLiteral("-c"),
         QStringLiteral(
             "credential.helper=!f() { echo username=%1; echo password=$RELAY_GIT_TOKEN; }; f")
             .arg(username)});
    environment.insert(QStringLiteral("RELAY_GIT_TOKEN"), token);
  }
  arguments.append({QStringLiteral("clone"), QStringLiteral("--progress"), QStringLiteral("--"),
                    remoteUrl, destinationPath});
  static_cast<void>(runGitWithoutRepository(arguments, environment,
                                             QFileInfo(destinationPath).absolutePath()));
  return readRepository(destinationPath);
}

void GitService::switchBranch(const QString& repositoryPath, const QString& branch) const {
  static const QRegularExpression invalidCharacter(QStringLiteral(R"([^A-Za-z0-9_./-])"));
  if (branch.isEmpty() || invalidCharacter.match(branch).hasMatch()) {
    throw ProcessError(QStringLiteral("Invalid branch name."));
  }
  static_cast<void>(
      runGit(repositoryPath, {QStringLiteral("switch"), branch}));
}

}  // namespace relay
