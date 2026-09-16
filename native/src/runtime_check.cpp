#include "relay/runtime_check.hpp"
#include "relay/process_runner.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>

namespace relay {

QString runtimeInstallHelp(const RuntimeTool tool) {
  const bool git = tool == RuntimeTool::git;
#ifdef Q_OS_WIN
  return git ? QStringLiteral("Install Git for Windows with: winget install --id Git.Git -e")
             : QStringLiteral("Install GitHub CLI with: winget install --id GitHub.cli -e");
#elif defined(Q_OS_MACOS)
  return git ? QStringLiteral("Install or update Git with: brew install git")
             : QStringLiteral("Install or update GitHub CLI with: brew install gh");
#else
  return git ? QStringLiteral("Install or update Git using your system package manager.")
             : QStringLiteral("Install or update GitHub CLI (gh) using your system package manager.");
#endif
}

QString systemRuntimeExecutable(const RuntimeTool tool) {
  const auto name = tool == RuntimeTool::git ? QStringLiteral("git") : QStringLiteral("gh");
  if (!QStandardPaths::findExecutable(name).isEmpty()) return name;
  QStringList candidates;
#ifdef Q_OS_WIN
  // An already-running application does not inherit PATH changes made by an
  // installer. Recheck standard user/system locations on each operation.
  const auto relative = tool == RuntimeTool::git ? QStringLiteral("Git/cmd/git.exe")
                                                : QStringLiteral("GitHub CLI/gh.exe");
  for (const auto& root : {qEnvironmentVariable("ProgramW6432"), qEnvironmentVariable("ProgramFiles"),
                          QDir(qEnvironmentVariable("LOCALAPPDATA")).filePath(QStringLiteral("Programs"))}) {
    if (!root.isEmpty()) candidates.append(QDir(root).filePath(relative));
  }
#elif defined(Q_OS_MACOS)
  candidates.append(QStringLiteral("/opt/homebrew/bin/%1").arg(name));
#endif
  for (const auto& candidate : candidates) {
    const QFileInfo file(candidate);
    if (file.isFile() && file.isExecutable()) return file.absoluteFilePath();
  }
  return name;
}

QString checkRuntime(const RuntimeTool tool, const QString& executable,
                     const QProcessEnvironment& environment) {
  const bool git = tool == RuntimeTool::git;
  const auto name = git ? QStringLiteral("Git") : QStringLiteral("GitHub CLI");
  // Conservative supported baselines; gh matches the frozen bundled version.
  const QVersionNumber minimum = git ? QVersionNumber(2, 35, 0) : QVersionNumber(2, 98, 0);
  ProcessRequest request{executable, {QStringLiteral("--version")}};
  request.environment = environment;
  request.timeoutMilliseconds = 5000;
  request.maximumOutputBytes = 16384;
  QByteArray output;
  try {
    output = ProcessRunner::run(request).standardOutput;
  } catch (const std::exception&) {
    return QStringLiteral("%1 is unavailable. %2").arg(name, runtimeInstallHelp(tool));
  }
  const QRegularExpression pattern(git ? QStringLiteral("^git version (\\d+\\.\\d+\\.\\d+)")
                                      : QStringLiteral("^gh version (\\d+\\.\\d+\\.\\d+)"));
  const auto match = pattern.match(QString::fromUtf8(output).trimmed());
  if (!match.hasMatch() || QVersionNumber::fromString(match.captured(1)) < minimum) {
    return QStringLiteral("%1 %2 or newer is required. %3")
        .arg(name, minimum.toString(), runtimeInstallHelp(tool));
  }
  return {};
}

}  // namespace relay
