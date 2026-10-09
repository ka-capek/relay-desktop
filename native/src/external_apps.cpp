#include "relay/external_apps.hpp"

#include "relay/process_runner.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace relay {
namespace {
struct KnownEditor {
  const char* id;
  const char* name;
  QStringList locations;  // macOS bundle names or Windows paths with %VAR%
};

QString expanded(QString path) {
  for (const auto& variable : {QStringLiteral("LOCALAPPDATA"), QStringLiteral("ProgramFiles"), QStringLiteral("ProgramFiles(x86)")}) {
    const auto token = u'%' + variable + u'%';
    if (!path.contains(token)) continue;
    const auto value = qEnvironmentVariable(variable.toLocal8Bit().constData());
    if (value.isEmpty()) return {};
    path.replace(token, value);
  }
  return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

QList<KnownEditor> knownEditors() {
#if defined(Q_OS_MACOS)
  return {{"vscode", "Visual Studio Code", {QStringLiteral("Visual Studio Code.app")}},
          {"cursor", "Cursor", {QStringLiteral("Cursor.app")}},
          {"zed", "Zed", {QStringLiteral("Zed.app")}},
          {"sublime", "Sublime Text", {QStringLiteral("Sublime Text.app")}},
          {"nova", "Nova", {QStringLiteral("Nova.app")}},
          {"bbedit", "BBEdit", {QStringLiteral("BBEdit.app")}},
          {"intellij", "IntelliJ IDEA", {QStringLiteral("IntelliJ IDEA.app"), QStringLiteral("IntelliJ IDEA CE.app")}},
          {"pycharm", "PyCharm", {QStringLiteral("PyCharm.app"), QStringLiteral("PyCharm CE.app")}},
          {"webstorm", "WebStorm", {QStringLiteral("WebStorm.app")}},
          {"clion", "CLion", {QStringLiteral("CLion.app")}},
          {"xcode", "Xcode", {QStringLiteral("Xcode.app")}}};
#elif defined(Q_OS_WIN)
  return {{"vscode", "Visual Studio Code", {QStringLiteral("%LOCALAPPDATA%/Programs/Microsoft VS Code/Code.exe"),
                                             QStringLiteral("%ProgramFiles%/Microsoft VS Code/Code.exe")}},
          {"cursor", "Cursor", {QStringLiteral("%LOCALAPPDATA%/Programs/cursor/Cursor.exe")}},
          {"zed", "Zed", {QStringLiteral("%LOCALAPPDATA%/Programs/Zed/Zed.exe")}},
          {"sublime", "Sublime Text", {QStringLiteral("%ProgramFiles%/Sublime Text/sublime_text.exe")}},
          {"notepadpp", "Notepad++", {QStringLiteral("%ProgramFiles%/Notepad++/notepad++.exe"),
                                      QStringLiteral("%ProgramFiles(x86)%/Notepad++/notepad++.exe")}}};
#else
  return {};
#endif
}

void requireDirectory(const QString& directory) {
  const QFileInfo info(directory);
  if (directory.isEmpty() || !info.isAbsolute() || !info.isDir())
    throw ProcessError(QStringLiteral("The repository folder is not available."));
}
}  // namespace

QList<ExternalEditor> ExternalApps::detectEditors() {
  QList<ExternalEditor> editors;
#if defined(Q_OS_MACOS)
  const QStringList roots{QStringLiteral("/Applications"), QDir::home().filePath(QStringLiteral("Applications"))};
#endif
  for (const auto& known : knownEditors()) {
    for (const auto& location : known.locations) {
#if defined(Q_OS_MACOS)
      QString found;
      for (const auto& root : roots)
        if (const auto candidate = QDir(root).filePath(location); QFileInfo(candidate).isDir()) { found = candidate; break; }
#else
      const auto candidate = expanded(location);
      const auto found = !candidate.isEmpty() && QFileInfo(candidate).isFile() ? candidate : QString{};
#endif
      if (found.isEmpty()) continue;
      editors.append({QString::fromLatin1(known.id), QString::fromLatin1(known.name), found});
      break;
    }
  }
  return editors;
}

ExternalEditor ExternalApps::resolveEditor(const QList<ExternalEditor>& editors, const QString& id,
                                           const QString& customProgram) {
  if (id == QStringLiteral("custom")) {
    const QFileInfo info(customProgram);
#if defined(Q_OS_MACOS)
    const bool valid = info.isAbsolute() && (info.isDir() ? customProgram.endsWith(QStringLiteral(".app")) : info.isExecutable());
#else
    const bool valid = info.isAbsolute() && info.isFile() && info.isExecutable();
#endif
    if (!valid) throw ProcessError(QStringLiteral("Choose an installed editor in Settings → General."));
    return {QStringLiteral("custom"), info.completeBaseName(), info.absoluteFilePath()};
  }
  for (const auto& editor : editors)
    if (id.isEmpty() || editor.id == id) return editor;
  throw ProcessError(id.isEmpty() ? QStringLiteral("No supported editor was found. Choose one in Settings → General.")
                                  : QStringLiteral("The editor chosen in Settings is no longer installed."));
}

ExternalLaunch ExternalApps::editorLaunch(const ExternalEditor& editor, const QString& directory) {
#if defined(Q_OS_MACOS)
  if (editor.program.endsWith(QStringLiteral(".app")))
    return {QStringLiteral("/usr/bin/open"), {QStringLiteral("-a"), editor.program, directory}, directory};
#endif
  return {editor.program, {QDir::toNativeSeparators(directory)}, directory};
}

ExternalLaunch ExternalApps::terminalLaunch(const QString& directory) {
#if defined(Q_OS_MACOS)
  return {QStringLiteral("/usr/bin/open"), {QStringLiteral("-a"), QStringLiteral("Terminal"), directory}, directory};
#elif defined(Q_OS_WIN)
  // Windows Terminal when installed, otherwise a Command Prompt in the folder.
  const auto terminal = expanded(QStringLiteral("%LOCALAPPDATA%/Microsoft/WindowsApps/wt.exe"));
  if (!terminal.isEmpty() && QFileInfo::exists(terminal))
    return {terminal, {QStringLiteral("-d"), QDir::toNativeSeparators(directory)}, directory};
  const auto system = qEnvironmentVariable("ComSpec", QStringLiteral("cmd.exe"));
  return {system, {QStringLiteral("/K")}, directory};
#else
  return {QStringLiteral("x-terminal-emulator"), {}, directory};
#endif
}

ExternalLaunch ExternalApps::fileManagerLaunch(const QString& directory) {
#if defined(Q_OS_MACOS)
  return {QStringLiteral("/usr/bin/open"), {directory}, directory};
#elif defined(Q_OS_WIN)
  return {QStringLiteral("explorer.exe"), {QDir::toNativeSeparators(directory)}, directory};
#else
  return {QStringLiteral("xdg-open"), {directory}, directory};
#endif
}

void ExternalApps::start(const ExternalLaunch& launch) {
  requireDirectory(launch.workingDirectory);
  if (!QProcess::startDetached(launch.program, launch.arguments, launch.workingDirectory))
    throw ProcessError(QStringLiteral("%1 could not be started.").arg(QFileInfo(launch.program).completeBaseName()));
}

}  // namespace relay
