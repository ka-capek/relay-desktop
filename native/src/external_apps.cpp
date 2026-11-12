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
  QStringList locations;  // macOS bundle names, Windows paths with %VAR%, or Linux command names
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
  // Command names, including the launchers Flatpak exports under its app ids.
  return {{"vscode", "Visual Studio Code", {QStringLiteral("code"), QStringLiteral("com.visualstudio.code")}},
          {"vscodium", "VSCodium", {QStringLiteral("codium"), QStringLiteral("com.vscodium.codium")}},
          {"cursor", "Cursor", {QStringLiteral("cursor")}},
          {"zed", "Zed", {QStringLiteral("zed"), QStringLiteral("zeditor"), QStringLiteral("dev.zed.Zed")}},
          {"sublime", "Sublime Text", {QStringLiteral("subl")}},
          {"kate", "Kate", {QStringLiteral("kate")}},
          {"intellij", "IntelliJ IDEA", {QStringLiteral("idea"), QStringLiteral("intellij-idea-ultimate"),
                                         QStringLiteral("intellij-idea-community")}},
          {"pycharm", "PyCharm", {QStringLiteral("pycharm"), QStringLiteral("pycharm-professional"),
                                  QStringLiteral("pycharm-community")}},
          {"webstorm", "WebStorm", {QStringLiteral("webstorm")}},
          {"clion", "CLion", {QStringLiteral("clion")}}};
#endif
}

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
// A desktop launcher may start Relay without the login shell's PATH, so the
// usual per-user and packaging locations are searched as well.
QString findCommand(const QString& name) {
  if (const auto found = QStandardPaths::findExecutable(name); !found.isEmpty()) return found;
  const auto home = QDir::home();
  return QStandardPaths::findExecutable(name, {home.filePath(QStringLiteral(".local/bin")),
      QStringLiteral("/usr/local/bin"), QStringLiteral("/snap/bin"),
      QStringLiteral("/var/lib/flatpak/exports/bin"),
      home.filePath(QStringLiteral(".local/share/flatpak/exports/bin")),
      home.filePath(QStringLiteral(".local/share/JetBrains/Toolbox/scripts"))});
}

struct KnownTerminal {
  const char* command;
  const char* directoryOption;  // nullptr: the terminal starts in its working directory
  bool joined;                  // --option=DIR rather than --option DIR
};

// The Debian alternative and the XDG default-terminal launcher first, then
// common terminals. Each one also starts with the repository as its working
// directory; the option covers terminals that hand the window to a server.
constexpr KnownTerminal knownTerminals[] = {
    {"x-terminal-emulator", nullptr, false}, {"xdg-terminal-exec", nullptr, false},
    {"gnome-terminal", "--working-directory", true}, {"kgx", nullptr, false}, {"ptyxis", nullptr, false},
    {"konsole", "--workdir", false}, {"xfce4-terminal", "--working-directory", true},
    {"kitty", "--directory", false}, {"alacritty", "--working-directory", false},
    {"foot", "--working-directory", true}, {"wezterm", nullptr, false}, {"tilix", nullptr, false},
    {"xterm", nullptr, false}};
#endif

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
#elif defined(Q_OS_WIN)
      const auto candidate = expanded(location);
      const auto found = !candidate.isEmpty() && QFileInfo(candidate).isFile() ? candidate : QString{};
#else
      const auto found = findCommand(location);
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
  for (const auto& terminal : knownTerminals) {
    const auto program = findCommand(QString::fromLatin1(terminal.command));
    if (program.isEmpty()) continue;
    QStringList arguments;
    if (terminal.directoryOption) {
      const auto option = QString::fromLatin1(terminal.directoryOption);
      if (terminal.joined) arguments = {option + u'=' + directory};
      else arguments = {option, directory};
    }
    return {program, arguments, directory};
  }
  // start() then reports that it could not be started.
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
