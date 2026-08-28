#include "relay/app_paths.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <utility>

namespace relay {
namespace {

QString joined(const QString& base, const QString& relative) {
  return QDir::cleanPath(QDir(base).filePath(relative));
}

QString executableIfPresent(const QString& candidate, const QString& fallback) {
  const QFileInfo info(candidate);
  return info.exists() && info.isFile() ? info.absoluteFilePath() : fallback;
}

QString packagedResourceRoot(const QString& applicationDirectory, const RuntimePlatform platform) {
  if (platform == RuntimePlatform::macOSArm64) return joined(applicationDirectory, QStringLiteral("../Resources"));
  return joined(applicationDirectory, QStringLiteral("resources"));
}

QString platformFolder(const RuntimePlatform platform) {
  return platform == RuntimePlatform::windowsX64 ? QStringLiteral("win-x64") : QStringLiteral("mac-arm64");
}

QString gitRelativeExecutable(const RuntimePlatform platform) {
  return platform == RuntimePlatform::windowsX64 ? QStringLiteral("cmd/git.exe") : QStringLiteral("bin/git");
}

QString githubCliName(const RuntimePlatform platform) {
  return platform == RuntimePlatform::windowsX64 ? QStringLiteral("gh.exe") : QStringLiteral("gh");
}

QString environmentPathSeparator(const RuntimePlatform platform) {
  return platform == RuntimePlatform::windowsX64 ? QStringLiteral(";") : QStringLiteral(":");
}

}  // namespace

bool GitRuntime::isBundled() const { return !root.isEmpty(); }

QProcessEnvironment GitRuntime::environment(QProcessEnvironment inherited) const {
  if (!isBundled()) return inherited;

  QStringList paths;
  if (platform == RuntimePlatform::windowsX64) {
    paths = {
        joined(root, QStringLiteral("cmd")),
        joined(root, QStringLiteral("mingw64/bin")),
        joined(root, QStringLiteral("usr/bin")),
    };
    inherited.insert(
        QStringLiteral("GIT_EXEC_PATH"), joined(root, QStringLiteral("mingw64/libexec/git-core")));
    inherited.insert(
        QStringLiteral("GIT_TEMPLATE_DIR"), joined(root, QStringLiteral("mingw64/share/git-core/templates")));
    inherited.insert(
        QStringLiteral("GIT_SSL_CAINFO"), joined(root, QStringLiteral("mingw64/etc/ssl/certs/ca-bundle.crt")));
  } else {
    paths = {joined(root, QStringLiteral("bin")), joined(root, QStringLiteral("libexec/git-core"))};
    inherited.insert(QStringLiteral("GIT_EXEC_PATH"), joined(root, QStringLiteral("libexec/git-core")));
    inherited.insert(QStringLiteral("GIT_TEMPLATE_DIR"), joined(root, QStringLiteral("share/git-core/templates")));
  }
  inherited.insert(QStringLiteral("GIT_CONFIG_SYSTEM"), joined(root, QStringLiteral("etc/gitconfig")));

  const auto oldPath = inherited.value(QStringLiteral("PATH"));
  if (!oldPath.isEmpty()) paths.push_back(oldPath);
  inherited.insert(QStringLiteral("PATH"), paths.join(environmentPathSeparator(platform)));
  return inherited;
}

RuntimePlatform AppPaths::currentPlatform() {
#ifdef Q_OS_WIN
  return RuntimePlatform::windowsX64;
#else
  return RuntimePlatform::macOSArm64;
#endif
}

QString AppPaths::legacyUserDataDirectory() {
  const auto home = QDir::homePath();
  const auto roaming = qEnvironmentVariable("APPDATA");
  return legacyUserDataDirectoryFor(currentPlatform(), home, roaming);
}

QString AppPaths::legacyUserDataDirectoryFor(
    const RuntimePlatform platform, const QString& homeDirectory, const QString& roamingAppDataDirectory) {
  if (platform == RuntimePlatform::macOSArm64) {
    return joined(homeDirectory, QStringLiteral("Library/Application Support/relay-desktop"));
  }

  auto roaming = roamingAppDataDirectory.isEmpty()
      ? joined(homeDirectory, QStringLiteral("AppData/Roaming"))
      : roamingAppDataDirectory;
  roaming.replace(u'\\', u'/');
  return joined(roaming, QStringLiteral("relay-desktop"));
}

QString AppPaths::storeFile() {
  return joined(legacyUserDataDirectory(), QStringLiteral("relay-data.json"));
}

QString AppPaths::githubCliDirectory() {
  return joined(legacyUserDataDirectory(), QStringLiteral("github-cli"));
}

QString AppPaths::avatarCacheDirectory() {
  return joined(legacyUserDataDirectory(), QStringLiteral("avatars"));
}

RuntimePaths AppPaths::resolveRuntimes(
    const QString& applicationDirectory, const QString& sourceDirectory, const RuntimePlatform platform) {
  const auto resources = packagedResourceRoot(applicationDirectory, platform);
  const auto developmentRuntime = joined(
      sourceDirectory, QStringLiteral("runtime/%1").arg(QStringLiteral("git/%1").arg(platformFolder(platform))));
  const auto packagedGitRoot = joined(resources, QStringLiteral("git"));
  const auto packagedGit = joined(packagedGitRoot, gitRelativeExecutable(platform));
  const auto developmentGit = joined(developmentRuntime, gitRelativeExecutable(platform));

  GitRuntime git;
  git.platform = platform;
  git.executable = executableIfPresent(packagedGit, {});
  if (!git.executable.isEmpty()) {
    git.root = packagedGitRoot;
  } else {
    git.executable = executableIfPresent(developmentGit, QStringLiteral("git"));
    if (git.executable != QStringLiteral("git")) git.root = developmentRuntime;
  }

  const auto cliName = githubCliName(platform);
  const auto packagedCli = joined(resources, QStringLiteral("gh/%1").arg(cliName));
  const auto developmentCli = joined(
      sourceDirectory, QStringLiteral("runtime/gh/%1/%2").arg(platformFolder(platform), cliName));

  RuntimePaths result;
  result.git = std::move(git);
  result.githubCliExecutable = executableIfPresent(
      packagedCli, executableIfPresent(developmentCli, cliName));
  return result;
}

QProcessEnvironment githubCliEnvironment(
    const QString& userDataDirectory, const bool interactive, QProcessEnvironment inherited) {
  inherited.remove(QStringLiteral("GH_TOKEN"));
  inherited.remove(QStringLiteral("GITHUB_TOKEN"));
  inherited.remove(QStringLiteral("GH_HOST"));
  inherited.insert(QStringLiteral("GH_CONFIG_DIR"), joined(userDataDirectory, QStringLiteral("github-cli")));
  inherited.insert(QStringLiteral("GH_NO_UPDATE_NOTIFIER"), QStringLiteral("1"));
  if (interactive) inherited.remove(QStringLiteral("GH_PROMPT_DISABLED"));
  else inherited.insert(QStringLiteral("GH_PROMPT_DISABLED"), QStringLiteral("1"));
  return inherited;
}

}  // namespace relay
