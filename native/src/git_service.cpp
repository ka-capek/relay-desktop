#include "relay/git_service.hpp"

#include "relay/process_runner.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QBuffer>
#include <QImageReader>
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

struct StatusRecord {
  QString code;
  QString path;
  QString oldPath;
};

QList<StatusRecord> statusRecords(const QString& text) {
  QList<StatusRecord> records;
  const auto fields = text.split(QChar(0), Qt::KeepEmptyParts);
  for (qsizetype i = 0; i < fields.size(); ++i) {
    const auto& field = fields.at(i);
    if (field.size() < 4) continue;
    StatusRecord record{field.left(2), field.mid(3), {}};
    if (record.code.contains(u'R') || record.code.contains(u'C')) {
      if (++i >= fields.size()) break;
      record.oldPath = fields.at(i);
    }
    records.push_back(std::move(record));
  }
  return records;
}

struct NumstatRecord {
  QString path;
  QString oldPath;
  qsizetype added{};
  qsizetype removed{};
  bool binary{};
};

QList<NumstatRecord> numstatRecords(const QString& text) {
  QList<NumstatRecord> records;
  const auto fields = text.split(QChar(0), Qt::KeepEmptyParts);
  for (qsizetype i = 0; i < fields.size(); ++i) {
    const auto& field = fields.at(i);
    const auto firstTab = field.indexOf(u'\t');
    const auto secondTab = field.indexOf(u'\t', firstTab + 1);
    if (firstTab < 0 || secondTab < 0) continue;
    const auto added = field.left(firstTab);
    const auto removed = field.mid(firstTab + 1, secondTab - firstTab - 1);
    NumstatRecord record{field.mid(secondTab + 1), {}, numericStat(added),
                         numericStat(removed), added == u"-" || removed == u"-"};
    if (record.path.isEmpty()) {
      if (i + 2 >= fields.size()) break;
      record.oldPath = fields.at(++i);
      record.path = fields.at(++i);
    }
    records.push_back(std::move(record));
  }
  return records;
}

QHash<QString, FileStatus> parseNameStatus(const QString& text) {
  QHash<QString, FileStatus> statuses;
  const auto fields = text.split(QChar(0), Qt::KeepEmptyParts);
  for (qsizetype i = 0; i + 1 < fields.size(); ++i) {
    const auto code = fields.at(i);
    if (code.isEmpty()) continue;
    auto path = fields.at(++i);
    if (code.startsWith(u'R') || code.startsWith(u'C')) {
      if (++i >= fields.size()) break;
      path = fields.at(i);
    }
    statuses.insert(path, code.startsWith(u'A') ? FileStatus::added
                            : code.startsWith(u'D') ? FileStatus::deleted
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
  for (const auto& key : {QStringLiteral("GIT_DIR"), QStringLiteral("GIT_WORK_TREE"),
      QStringLiteral("GIT_INDEX_FILE"), QStringLiteral("GIT_COMMON_DIR"), QStringLiteral("GIT_OBJECT_DIRECTORY"),
      QStringLiteral("GIT_ALTERNATE_OBJECT_DIRECTORIES"), QStringLiteral("GIT_LITERAL_PATHSPECS"),
      QStringLiteral("GIT_GLOB_PATHSPECS"), QStringLiteral("GIT_NOGLOB_PATHSPECS"), QStringLiteral("GIT_ICASE_PATHSPECS")}) environment.remove(key);
  environment.remove(QStringLiteral("RELAY_GIT_TOKEN"));
  environment.remove(QStringLiteral("RELAY_GIT_USERNAME"));
  environment.remove(QStringLiteral("GH_TOKEN"));
  environment.remove(QStringLiteral("GITHUB_TOKEN"));
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
                           const QProcessEnvironment& overrides, const bool preserveOutput, const bool literalPaths, const QByteArray& standardInput) const {
  QStringList processArguments{QStringLiteral("-C"), repositoryPath};
  if (literalPaths) processArguments.prepend(QStringLiteral("--literal-pathspecs"));
  processArguments.append(arguments);
  ProcessRequest request{gitExecutable(), processArguments};
  request.environment = gitProcessEnvironment(overrides);
  request.standardInput = standardInput;
  request.timeoutMilliseconds = 120000;
  request.maximumOutputBytes = maximumGitOutputBytes;
  try {
    const auto output = QString::fromUtf8(ProcessRunner::run(request).standardOutput);
    return preserveOutput ? output : trimEnd(output);
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
  request.timeoutMilliseconds = 120000;
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
  for (const auto& record : numstatRecords(statText))
    stats.insert(record.path, {record.added, record.removed});

  QList<ChangedFile> files;
  for (const auto& record : statusRecords(statusText)) {
    const auto& code = record.code;
    const auto& filePath = record.path;

    auto values = stats.value(filePath);
    if (code == QStringLiteral("??") && values.added == 0) {
      QFile file(root + u'/' + filePath);
      const QFileInfo info(file);
      if (info.isSymLink()) values.added = 1;
      else if (info.isFile() && file.open(QIODevice::ReadOnly) && file.size() < maximumUntrackedStatBytes) {
        const auto buffer = file.read(maximumUntrackedStatBytes);
        if (!buffer.contains('\0')) values.added = buffer.count('\n') + (!buffer.isEmpty() && !buffer.endsWith('\n') ? 1 : 0);
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
             QStringLiteral("status"), QStringLiteral("--porcelain=v1"), QStringLiteral("-z"),
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
    statText = runGit(root, {QStringLiteral("diff"), QStringLiteral("--numstat"), QStringLiteral("-z"),
                             QStringLiteral("HEAD"), QStringLiteral("--")});
  } catch (const ProcessError&) {
    statText = runGitOrEmpty(root, {QStringLiteral("diff"), QStringLiteral("--numstat"), QStringLiteral("-z"),
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
  Repository repository{root,
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
          firstCommit, {}, {}, {}, {}, false, {}, {}, {}, {}};
  readOperationState(repository);
  return repository;
}

RepositorySummary GitService::readRepositorySummary(const QString& repositoryPath) const {
  const auto root = runGit(repositoryPath,
                           {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
  const auto branch =
      runGitOrEmpty(root, {QStringLiteral("branch"), QStringLiteral("--show-current")});
  const auto status = runGit(
      root, {QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
             QStringLiteral("status"), QStringLiteral("--porcelain=v1"), QStringLiteral("-z"),
             QStringLiteral("--untracked-files=all")});
  const auto remote = runGitOrEmpty(
      root, {QStringLiteral("remote"), QStringLiteral("get-url"), QStringLiteral("origin")});
  const auto identity = repositoryIdentity(remote, root);
  return {root,
          identity.name,
          identity.owner,
          branch.isEmpty() ? QStringLiteral("detached HEAD") : branch,
          statusRecords(status).size(),
          QDateTime::currentDateTimeUtc(),
          std::nullopt,
          latestCommitDate(root),
          firstCommitDate(root)};
}

QString GitService::assertCommitInRepository(const QString& repositoryPath,
                                             const QString& requestedHash) const {
  const auto hash = requestedHash.trimmed();
  static const QRegularExpression validHash(QStringLiteral(R"(^[0-9a-f]{7,64}$)"),
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
                                        const int limit, const QString& requestedAnchor, const bool allBranches) const {
  const auto root = runGit(repositoryPath,
                           {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
  const auto head =
      runGitOrEmpty(root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
  if (head.isEmpty() && !allBranches) return {{}, {}, {}, true};

  const auto boundedSkip = std::max(0, skip);
  const auto boundedLimit = std::clamp(limit == 0 ? 50 : limit, 1, historyBatchLimit);
  QStringList tips;
  if (!requestedAnchor.isEmpty()) {
    tips = allBranches ? requestedAnchor.split(u'|', Qt::SkipEmptyParts) : QStringList{requestedAnchor};
    static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-fA-F]{7,64}$"));
    for (const auto& tip : tips)
      if (!hashPattern.match(tip).hasMatch()) throw ProcessError(QStringLiteral("Invalid history snapshot."));
    // Batch validation avoids one child process per branch per page.
    const auto objects = runGit(root, {QStringLiteral("cat-file"), QStringLiteral("--batch-check=%(objecttype)")},
        {}, false, true, (tips.join(u'\n') + u'\n').toUtf8()).split(u'\n');
    if (objects.size() != tips.size() || std::any_of(objects.cbegin(), objects.cend(), [](const QString& type) { return type != QStringLiteral("commit"); }))
      throw ProcessError(QStringLiteral("This history snapshot is no longer available. Refresh the repository."));
  } else if (allBranches) {
    tips = runGit(root, {QStringLiteral("rev-parse"), QStringLiteral("--branches"), QStringLiteral("--remotes")}).split(u'\n', Qt::SkipEmptyParts);
    if (!head.isEmpty()) tips.append(head);
    tips.removeDuplicates();
  } else tips.append(head);
  if (tips.isEmpty()) return {{}, head, {}, true};
  const auto anchor = tips.join(u'|');
  QStringList arguments{QStringLiteral("log"), QStringLiteral("--topo-order"),
      QStringLiteral("--skip=%1").arg(boundedSkip), QStringLiteral("-n"), QString::number(boundedLimit),
      QStringLiteral("--pretty=format:%H%x1f%h%x1f%P%x1f%s%x1f%an%x1f%ae%x1f%aI%x1f%D%x1e")};
  arguments.append(QStringLiteral("--stdin"));
  const auto logText = runGit(root, arguments, {}, false, true, (tips.join(u'\n') + u'\n').toUtf8());
  auto commits = parseHistoryPage(logText);
  const auto endOfHistory = commits.size() < boundedLimit;
  return {std::move(commits), head, anchor, endOfHistory};
}

CommitDetail GitService::readCommitDetail(const QString& repositoryPath,
                                          const QString& requestedHash) const {
  const auto root = runGit(repositoryPath,
                           {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
  const auto hash = assertCommitInRepository(root, requestedHash);
  const auto commitRecord = runGit(
      root,
      {QStringLiteral("show"), QStringLiteral("--no-patch"),
       QStringLiteral(
           "--format=%H%x1f%h%x1f%P%x1f%s%x1f%b%x1f%an%x1f%ae%x1f%aI%x1f%cn%x1f%ce%x1f%cI%x1f%D"),
       hash});
  const auto fields = commitRecord.split(QChar(0x1f), Qt::KeepEmptyParts);
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
  numstatArguments.append({QStringLiteral("--numstat"), QStringLiteral("-z"), QStringLiteral("--")});
  auto nameStatusArguments = compareArguments;
  nameStatusArguments.append({QStringLiteral("--name-status"), QStringLiteral("-z"), QStringLiteral("--")});
  const auto numstatText = runGitOrEmpty(root, numstatArguments);
  const auto nameStatusText = runGitOrEmpty(root, nameStatusArguments);
  const auto statuses = parseNameStatus(nameStatusText);

  QList<ChangedFile> files;
  qsizetype totalAdded = 0;
  qsizetype totalRemoved = 0;
  for (const auto& record : numstatRecords(numstatText)) {
    const auto& filePath = record.path;
    const auto status = statuses.value(filePath, FileStatus::modified);
    files.push_back({filePath, QFileInfo(filePath).fileName(), displayDirectory(filePath), status,
                     record.added, record.removed, record.binary});
    totalAdded += record.added;
    totalRemoved += record.removed;
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
  auto statsArguments = arguments;
  statsArguments.removeAll(QStringLiteral("-p"));
  statsArguments.removeAll(QStringLiteral("--unified=3"));
  statsArguments.append({QStringLiteral("--numstat"), QStringLiteral("-z"), QStringLiteral("--")});
  QStringList paths{target};
  for (const auto& record : numstatRecords(runGit(root, statsArguments))) {
    if (record.path == target && !record.oldPath.isEmpty()) paths.push_back(record.oldPath);
  }
  arguments.push_back(QStringLiteral("--"));
  arguments.append(paths);
  return runGit(root, arguments, {}, true);
}

QString GitService::getFileDiff(const QString& repositoryPath, const QString& filePath) const {
  const auto status = runGit(
      repositoryPath,
      {QStringLiteral("-c"), QStringLiteral("core.quotepath=false"), QStringLiteral("status"),
       QStringLiteral("--porcelain=v1"), QStringLiteral("-z"), QStringLiteral("--"), filePath});
  if (status.startsWith(QStringLiteral("??"))) {
    QFile file(repositoryPath + u'/' + filePath);
    const QFileInfo info(file);
    if (info.isSymLink()) return QStringLiteral("Symbolic link → %1").arg(info.symLinkTarget());
    if (!info.isFile()) return QStringLiteral("This file type cannot be previewed.");
    if (!file.open(QIODevice::ReadOnly)) {
      throw ProcessError(
          QStringLiteral("%1 could not be read: %2").arg(filePath, file.errorString()));
    }
    if (file.size() > maximumUntrackedStatBytes)
      return QStringLiteral("File is too large to preview (limit: 2 MiB).");
    const auto buffer = file.read(maximumUntrackedStatBytes + 1);
    if (buffer.size() > maximumUntrackedStatBytes)
      return QStringLiteral("File is too large to preview (limit: 2 MiB).");
    if (buffer.contains('\0')) return QStringLiteral("Binary file — preview unavailable");
    if (buffer.isEmpty()) return QStringLiteral("Empty file");
    auto lines = QString::fromUtf8(buffer).split(u'\n', Qt::KeepEmptyParts);
    if (buffer.endsWith('\n')) lines.removeLast();
    QStringList diff{QStringLiteral("--- /dev/null"), QStringLiteral("+++ b/%1").arg(filePath),
                     QStringLiteral("@@ -0,0 +1,%1 @@").arg(lines.size())};
    for (const auto& line : lines) diff.push_back(QStringLiteral("+") + line);
    if (!buffer.endsWith('\n')) diff.push_back(QStringLiteral("\\ No newline at end of file"));
    return diff.join(u'\n');
  }

  const auto head = runGitOrEmpty(repositoryPath,
      {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("HEAD")});
  QStringList arguments{QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
                        QStringLiteral("diff"),
                        head.isEmpty() ? QStringLiteral("--cached") : QStringLiteral("HEAD"),
                        QStringLiteral("--no-ext-diff"), QStringLiteral("--unified=3"),
                        QStringLiteral("--"), filePath};
  return runGit(repositoryPath, arguments, {}, true);
}

FilePreview GitService::readFilePreview(const QString& root, const QString& path, const QString& commit) const {
  FilePreview preview;
  preview.diff = commit.isEmpty() ? getFileDiff(root, path) : readCommitFileDiff(root, commit, path);
  const auto suffix = QFileInfo(path).suffix().toLower();
  if (!QStringList{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"),
      QStringLiteral("bmp"), QStringLiteral("webp")}.contains(suffix)) return preview;
  bool unavailable = false;
  const auto decode = [&unavailable](QByteArray bytes) {
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const auto size = reader.size();
    if (!size.isValid() || bytes.size() > 10 * 1024 * 1024 || static_cast<qint64>(size.width()) * size.height() > 16 * 1024 * 1024) { unavailable = true; return QImage{}; }
    auto image = reader.read();
    if (image.isNull()) unavailable = true;
    return image;
  };
  const auto blob = [this, &root, &decode, &unavailable](const QString& revision, const QString& file) {
    if (revision.isEmpty()) return QImage{};
    const auto object = runGitOrEmpty(root, {QStringLiteral("rev-parse"), QStringLiteral("--verify"), revision + u':' + file});
    if (object.isEmpty()) return QImage{};
    const auto size = runGit(root, {QStringLiteral("cat-file"), QStringLiteral("-s"), object}).toLongLong();
    if (size > 10 * 1024 * 1024) { unavailable = true; return QImage{}; }
    ProcessRequest request{gitExecutable(), {QStringLiteral("-C"), root, QStringLiteral("cat-file"), QStringLiteral("blob"), object}};
    request.environment = gitProcessEnvironment();
    request.maximumOutputBytes = 10 * 1024 * 1024;
    return decode(ProcessRunner::run(request).standardOutput);
  };
  QString before;
  QString oldPath = path;
  if (commit.isEmpty()) {
    before = runGitOrEmpty(root, {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("HEAD")});
    // Only paths actually reported by Git may be opened from the worktree.
    const auto records = statusRecords(runGit(root, {QStringLiteral("status"), QStringLiteral("--porcelain=v1"), QStringLiteral("--untracked-files=all"), QStringLiteral("-z")}));
    const auto found = std::find_if(records.cbegin(), records.cend(), [&path](const auto& value) { return value.path == path; });
    if (found != records.cend()) {
      if (!found->oldPath.isEmpty()) oldPath = found->oldPath;
      const QFileInfo info(root + u'/' + path);
      if (!info.isSymLink() && info.isFile() && info.size() <= 10 * 1024 * 1024) {
        QFile file(info.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly)) preview.after = decode(file.read(10 * 1024 * 1024 + 1));
        else unavailable = true;
      } else if (info.exists() || info.isSymLink()) unavailable = true;
    }
  } else {
    const auto detail = readCommitDetail(root, commit);
    if (!detail.parents.isEmpty()) before = detail.parents.first();
    preview.after = blob(detail.fullHash, path);
    if (!before.isEmpty()) {
      for (const auto& record : numstatRecords(runGit(root, {QStringLiteral("diff"), QStringLiteral("-M"),
          QStringLiteral("--numstat"), QStringLiteral("-z"), before, detail.fullHash, QStringLiteral("--")})))
        if (record.path == path && !record.oldPath.isEmpty()) oldPath = record.oldPath;
    }
  }
  preview.before = blob(before, oldPath);
  if (unavailable) {
    preview.before = {}; preview.after = {};
    preview.diff = QStringLiteral("Image preview unavailable: existing content is unreadable, unsupported, or exceeds the image preview limits.\n\n") + preview.diff;
  }
  return preview;
}

bool GitService::isSshRemote(const QString& remote) {
  const auto value = remote.trimmed();
  if (value.startsWith(QStringLiteral("ssh://"), Qt::CaseInsensitive)) return true;
  static const QRegularExpression scpLike(QStringLiteral(R"(^[^@\s]+@[^@:\s/\\]+:)"));
  static const QRegularExpression windowsDrive(QStringLiteral(R"(^[a-zA-Z]:[\\/])"));
  return scpLike.match(value).hasMatch() && !windowsDrive.match(value).hasMatch();
}

QString GitService::originRemoteUrl(const QString& repositoryPath, const bool forPush) const {
  QStringList arguments{QStringLiteral("remote"), QStringLiteral("get-url")};
  if (forPush) arguments.append({QStringLiteral("--push"), QStringLiteral("--all")});
  arguments.append(QStringLiteral("origin"));
  const auto remote = runGitOrEmpty(repositoryPath, arguments);
  if (forPush && remote.contains(u'\n'))
    throw ProcessError(QStringLiteral("Origin has multiple push URLs. Configure one push URL before pushing from Relay."));
  return remote;
}

QStringList GitService::expandedChangedPaths(const QString& root, const QStringList& paths) const {
  const auto records = statusRecords(runGit(root, {QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
      QStringLiteral("-z"), QStringLiteral("--untracked-files=all")}));
  QStringList expanded;
  for (const auto& path : paths) {
    const auto found = std::find_if(records.cbegin(), records.cend(), [&path](const auto& record) { return record.path == path; });
    if (found == records.cend()) throw ProcessError(QStringLiteral("A selected file is no longer changed."));
    expanded.append(path);
    if (found->code.contains(u'R') && !found->oldPath.isEmpty()) expanded.append(found->oldPath);
  }
  expanded.removeDuplicates();
  return expanded;
}

void GitService::commitFiles(const QString& repositoryPath, const QStringList& files,
                             const QString& summary, const QString& description,
                             const Account& account) const {
  requireIdle(repositoryPath);
  if (files.isEmpty()) throw ProcessError(QStringLiteral("Select at least one changed file."));
  if (summary.trimmed().isEmpty()) throw ProcessError(QStringLiteral("Enter a commit summary."));

  const auto records = statusRecords(runGit(repositoryPath,
      {QStringLiteral("status"), QStringLiteral("--porcelain=v1"), QStringLiteral("-z"),
       QStringLiteral("--untracked-files=all")}));
  QStringList selectedPaths = files;
  QStringList pathsToAdd;
  for (const auto& path : files) {
    const auto record = std::find_if(records.cbegin(), records.cend(),
        [&path](const auto& value) { return value.path == path; });
    if (record == records.cend())
      throw ProcessError(QStringLiteral("The selected file is no longer changed: %1").arg(path));
    if (record->code.contains(u'R') && !record->oldPath.isEmpty())
      selectedPaths.push_back(record->oldPath);
    // A staged deletion has no index entry left for git add to match.
    if (!record->code.startsWith(u'D') || QFileInfo::exists(repositoryPath + u'/' + path))
      pathsToAdd.push_back(path);
  }
  selectedPaths.removeDuplicates();
  if (!pathsToAdd.isEmpty()) {
    QStringList addArguments{QStringLiteral("add"), QStringLiteral("--")};
    addArguments.append(pathsToAdd);
    static_cast<void>(runGit(repositoryPath, addArguments));
  }

  QStringList arguments{QStringLiteral("-c"), QStringLiteral("user.name=%1").arg(account.name),
                        QStringLiteral("-c"), QStringLiteral("user.email=%1").arg(account.email),
                        QStringLiteral("commit"), QStringLiteral("--only")};
  arguments.append({QStringLiteral("-m"), summary.trimmed()});
  if (!description.trimmed().isEmpty()) {
    arguments.append({QStringLiteral("-m"), description.trimmed()});
  }
  arguments.push_back(QStringLiteral("--"));
  arguments.append(selectedPaths);

  QProcessEnvironment environment;
  environment.insert(QStringLiteral("GIT_AUTHOR_NAME"), account.name);
  environment.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), account.email);
  environment.insert(QStringLiteral("GIT_COMMITTER_NAME"), account.name);
  environment.insert(QStringLiteral("GIT_COMMITTER_EMAIL"), account.email);
  static_cast<void>(runGit(repositoryPath, arguments, environment));
}

QString GitService::githubCredentialHelper() {
  // Git may rewrite a URL or follow a redirect. Gate credentials on the host
  // Git actually asks for, and keep all account values out of shell source.
  return QStringLiteral(
      "!f() { [ \"$1\" = get ] || return 0; protocol=; host=; while IFS='=' read -r key value; do "
      "case \"$key\" in protocol) protocol=$value;; host) host=$value;; esac; done; "
      "if [ \"$protocol\" = https ] && [ \"$host\" = github.com ]; then "
      "printf '%s\\n' \"username=$RELAY_GIT_USERNAME\" \"password=$RELAY_GIT_TOKEN\"; fi; }; f");
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
         QStringLiteral("credential.helper=") + githubCredentialHelper()});
    environment.insert(QStringLiteral("RELAY_GIT_TOKEN"), token);
    environment.insert(QStringLiteral("RELAY_GIT_USERNAME"), username);
  }
  arguments.append(
      {QStringLiteral("fetch"), QStringLiteral("origin"), QStringLiteral("--prune")});
  static_cast<void>(runGit(repositoryPath, arguments, environment));
}

void GitService::pushOrigin(const QString& repositoryPath, const QString& token,
                            const QString& handle, const QString& sshCommand) const {
  const auto remote = originRemoteUrl(repositoryPath, true);
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
         QStringLiteral("credential.helper=") + githubCredentialHelper()});
    environment.insert(QStringLiteral("RELAY_GIT_TOKEN"), token);
    environment.insert(QStringLiteral("RELAY_GIT_USERNAME"), username);
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
         QStringLiteral("credential.helper=") + githubCredentialHelper()});
    environment.insert(QStringLiteral("RELAY_GIT_TOKEN"), token);
    environment.insert(QStringLiteral("RELAY_GIT_USERNAME"), username);
  }
  arguments.append({QStringLiteral("clone"), QStringLiteral("--progress"), QStringLiteral("--"),
                    remoteUrl, destinationPath});
  static_cast<void>(runGitWithoutRepository(arguments, environment,
                                             QFileInfo(destinationPath).absolutePath()));
  return readRepository(destinationPath);
}

void GitService::switchBranch(const QString& repositoryPath, const QString& branch) const {
  requireIdle(repositoryPath);
  if (branch.isEmpty() || branch.startsWith(u'-')) {
    throw ProcessError(QStringLiteral("Invalid branch name."));
  }
  static_cast<void>(runGit(repositoryPath, {QStringLiteral("show-ref"), QStringLiteral("--verify"),
      QStringLiteral("--quiet"), QStringLiteral("refs/heads/") + branch}));
  static_cast<void>(
      runGit(repositoryPath, {QStringLiteral("switch"), QStringLiteral("--no-guess"), branch}));
}

void GitService::createBranch(const QString& repositoryPath, const QString& branch) const {
  requireIdle(repositoryPath);
  if (branch.isEmpty() || branch.startsWith(u'-'))
    throw ProcessError(QStringLiteral("Enter a valid branch name."));
  static_cast<void>(runGit(repositoryPath, {QStringLiteral("check-ref-format"),
      QStringLiteral("refs/heads/") + branch}));
  static_cast<void>(runGit(repositoryPath, {QStringLiteral("switch"),
      QStringLiteral("--create"), branch}));
}

void GitService::pullOrigin(const QString& repositoryPath, const QString& token,
                            const QString& handle, const QString& sshCommand) const {
  const auto upstream = runGitOrEmpty(repositoryPath, {QStringLiteral("rev-parse"),
      QStringLiteral("--abbrev-ref"), QStringLiteral("--symbolic-full-name"), QStringLiteral("@{upstream}")});
  if (!upstream.startsWith(QStringLiteral("origin/")))
    throw ProcessError(QStringLiteral("Select a branch tracking origin before pulling."));
  fetchOrigin(repositoryPath, token, handle, sshCommand);
  try {
    static_cast<void>(runGit(repositoryPath, {QStringLiteral("merge"), QStringLiteral("--ff-only"),
        QStringLiteral("--no-edit"), QStringLiteral("@{upstream}")}));
  } catch (const ProcessError& error) {
    throw ProcessError(QStringLiteral("Could not fast-forward this branch. Commit or stash conflicting local changes, or merge diverged branches before pulling. %1").arg(error.qMessage()));
  }
}

}  // namespace relay
