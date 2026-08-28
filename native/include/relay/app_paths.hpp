#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace relay {

enum class RuntimePlatform { macOSArm64, windowsX64 };

struct GitRuntime {
  QString executable;
  QString root;
  RuntimePlatform platform{RuntimePlatform::macOSArm64};

  [[nodiscard]] bool isBundled() const;
  [[nodiscard]] QProcessEnvironment environment(
      QProcessEnvironment inherited = QProcessEnvironment::systemEnvironment()) const;
};

struct RuntimePaths {
  GitRuntime git;
  QString githubCliExecutable;
};

class AppPaths final {
 public:
  [[nodiscard]] static RuntimePlatform currentPlatform();

  // Relay 0.5.0 used Electron's `userData` directory under the application
  // name `relay-desktop`. Keep these paths exact so both clients can use the
  // same public metadata and GitHub CLI credential configuration.
  [[nodiscard]] static QString legacyUserDataDirectory();
  [[nodiscard]] static QString legacyUserDataDirectoryFor(
      RuntimePlatform platform, const QString& homeDirectory, const QString& roamingAppDataDirectory = {});
  [[nodiscard]] static QString storeFile();
  [[nodiscard]] static QString githubCliDirectory();
  [[nodiscard]] static QString avatarCacheDirectory();

  // Resolution order matches Relay 0.5.0: packaged payload, development
  // runtime, then PATH. `sourceDirectory` is the repository root.
  [[nodiscard]] static RuntimePaths resolveRuntimes(
      const QString& applicationDirectory, const QString& sourceDirectory, RuntimePlatform platform);
};

[[nodiscard]] QProcessEnvironment githubCliEnvironment(
    const QString& userDataDirectory, bool interactive,
    QProcessEnvironment inherited = QProcessEnvironment::systemEnvironment());

}  // namespace relay
