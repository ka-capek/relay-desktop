#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace relay {

// An installed editor Relay can open a repository in. program is an .app
// bundle on macOS and an executable on Windows.
struct ExternalEditor {
  QString id;
  QString name;
  QString program;
};

enum class ExternalTarget { editor, terminal, fileManager };

struct ExternalLaunch {
  QString program;
  QStringList arguments;
  QString workingDirectory;
};

// Opens repositories in other applications. Paths are passed as single
// arguments, never through a shell.
class ExternalApps final {
 public:
  // Known editors found in their standard install locations.
  [[nodiscard]] static QList<ExternalEditor> detectEditors();
  // The editor for a preference: a detected id, "custom" with a program path,
  // or empty for the first detected editor. Throws if none applies.
  [[nodiscard]] static ExternalEditor resolveEditor(const QList<ExternalEditor>& editors, const QString& id,
                                                    const QString& customProgram);
  [[nodiscard]] static ExternalLaunch editorLaunch(const ExternalEditor& editor, const QString& directory);
  [[nodiscard]] static ExternalLaunch terminalLaunch(const QString& directory);
  [[nodiscard]] static ExternalLaunch fileManagerLaunch(const QString& directory);
  // Validates the directory and starts the program detached.
  static void start(const ExternalLaunch& launch);
};

}  // namespace relay
