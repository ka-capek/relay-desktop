#include "relay/github_auth.hpp"

#include "relay/process_runner.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

#include <stdexcept>

namespace relay {
namespace {

constexpr auto kGitHubHost = "github.com";
constexpr auto kDeviceUrl = "https://github.com/login/device";

QString lastNonEmptyLine(const QString& value) {
  const auto lines = value.split(u'\n', Qt::SkipEmptyParts);
  return lines.isEmpty() ? QString{} : lines.last().trimmed();
}

}  // namespace

GitHubAuth::GitHubAuth(GitHubAuthContext context) : context_(std::move(context)) {}

QString GitHubAuth::executable() const {
#ifdef Q_OS_WIN
  const auto name = QStringLiteral("gh.exe");
  const auto platform = QStringLiteral("win-x64");
#else
  const auto name = QStringLiteral("gh");
  const auto platform = QStringLiteral("mac-arm64");
#endif
  const auto bundled = QDir(context_.resourcesRoot).filePath(QStringLiteral("gh/%1").arg(name));
  if (QFileInfo(bundled).isFile()) return bundled;
  const auto development = QDir(context_.sourceRoot).filePath(QStringLiteral("runtime/gh/%1/%2").arg(platform, name));
  return !context_.packaged && QFileInfo(development).isFile() ? development : name;
}

QProcessEnvironment GitHubAuth::environment(const bool interactive) const {
  auto value = QProcessEnvironment::systemEnvironment();
  value.insert(QStringLiteral("GH_CONFIG_DIR"), QDir(context_.userDataPath).filePath(QStringLiteral("github-cli")));
  value.insert(QStringLiteral("GH_NO_UPDATE_NOTIFIER"), QStringLiteral("1"));
  value.remove(QStringLiteral("GH_TOKEN"));
  value.remove(QStringLiteral("GITHUB_TOKEN"));
  value.remove(QStringLiteral("GH_HOST"));
  if (!interactive) value.insert(QStringLiteral("GH_PROMPT_DISABLED"), QStringLiteral("1"));
  else value.remove(QStringLiteral("GH_PROMPT_DISABLED"));
  return value;
}

QList<AuthenticatedAccount> GitHubAuth::accountsFromStatus(const QByteArray& json) {
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(json, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject() ||
      !document.object().value(QStringLiteral("hosts")).isObject())
    throw std::runtime_error("GitHub CLI returned an invalid account list. Existing accounts have been kept.");
  const auto hostValue = document.object().value(QStringLiteral("hosts"))
                             .toObject().value(QString::fromLatin1(kGitHubHost));
  QJsonArray accounts;
  if (hostValue.isArray()) accounts = hostValue.toArray();
  else if (hostValue.isObject()) accounts.append(hostValue.toObject());
  else if (!hostValue.isUndefined() && !hostValue.isNull())
    throw std::runtime_error("GitHub CLI returned an invalid account list. Existing accounts have been kept.");

  QList<AuthenticatedAccount> result;
  for (const auto& entry : accounts) {
    const auto object = entry.toObject();
    const auto handle = object.value(QStringLiteral("login")).toString().trimmed();
    const auto state = object.value(QStringLiteral("state")).toString(QStringLiteral("success"));
    if (state == QStringLiteral("failure"))
      throw std::runtime_error("A GitHub account needs authentication. Sign in again; existing account settings have been kept.");
    if (handle.isEmpty())
      throw std::runtime_error("GitHub CLI returned an incomplete account. Existing accounts have been kept.");
    result.append({handle,
                   object.value(QStringLiteral("active")).toBool(),
                   state,
                   object.value(QStringLiteral("tokenSource")).toString(QStringLiteral("credential store"))});
  }
  return result;
}

QList<AuthenticatedAccount> GitHubAuth::authenticatedAccounts() const {
  ProcessRequest request{executable(),
                         {QStringLiteral("auth"), QStringLiteral("status"),
                          QStringLiteral("--hostname"), QString::fromLatin1(kGitHubHost),
                          QStringLiteral("--json"), QStringLiteral("hosts")}};
  request.environment = environment();
  // A missing CLI, corrupt output, or expired account is not a logout.
  // Preserve Relay's account metadata by failing synchronization explicitly.
  QByteArray output;
  try {
    output = ProcessRunner::run(request).standardOutput;
  } catch (const ProcessError& error) {
    output = error.result().standardOutput;
    if (output.isEmpty())
      throw std::runtime_error("GitHub accounts could not be read. Check that GitHub CLI is installed and sign in again.");
  }
  return accountsFromStatus(output);
}

QString GitHubAuth::accountToken(const QString& handle) const {
  ProcessRequest request{executable(),
                         {QStringLiteral("auth"), QStringLiteral("token"), QStringLiteral("--hostname"),
                          QString::fromLatin1(kGitHubHost), QStringLiteral("--user"), handle}};
  request.environment = environment();
  const auto token = QString::fromUtf8(ProcessRunner::run(request).standardOutput).trimmed();
  if (token.isEmpty()) {
    throw std::runtime_error(QStringLiteral("GitHub credentials for @%1 are unavailable. Sign in again.")
                                 .arg(handle).toStdString());
  }
  return token;
}

void GitHubAuth::switchAccount(const QString& handle) const {
  ProcessRequest request{executable(),
                         {QStringLiteral("auth"), QStringLiteral("switch"), QStringLiteral("--hostname"),
                          QString::fromLatin1(kGitHubHost), QStringLiteral("--user"), handle}};
  request.environment = environment();
  static_cast<void>(ProcessRunner::run(request));
}

void GitHubAuth::removeAccount(const QString& handle) const {
  ProcessRequest request{executable(),
                         {QStringLiteral("auth"), QStringLiteral("logout"), QStringLiteral("--hostname"),
                          QString::fromLatin1(kGitHubHost), QStringLiteral("--user"), handle}};
  request.environment = environment();
  static_cast<void>(ProcessRunner::run(request));
}

QString GitHubAuth::cleanOutput(QString value) {
  static const QRegularExpression ansi(QStringLiteral("\\x1b\\[[0-9;?]*[ -/]*[@-~]"));
  value.remove(ansi);
  value.remove(u'\r');
  return value.trimmed();
}

GitHubLoginProgress GitHubAuth::loginProgressFromOutput(const QString& combinedOutput,
                                                        const QString& latestOutput) {
  static const QRegularExpression codePattern(QStringLiteral("\\b[A-Z0-9]{4}-[A-Z0-9]{4}\\b"));
  const auto match = codePattern.match(combinedOutput);
  const auto code = match.hasMatch() ? match.captured() : QString{};
  return {code,
          code.isEmpty() ? QString{} : QString::fromLatin1(kDeviceUrl),
          code.isEmpty() ? lastNonEmptyLine(latestOutput)
                         : QStringLiteral("Enter this one-time code in the GitHub window."),
          false};
}

void GitHubAuth::login(const std::function<void(const GitHubLoginProgress&)>& onProgress,
                       const std::shared_ptr<std::atomic_bool>& canceled) const {
  QProcess process;
  process.setProgram(executable());
  process.setArguments({QStringLiteral("auth"), QStringLiteral("login"), QStringLiteral("--hostname"),
                        QString::fromLatin1(kGitHubHost), QStringLiteral("--git-protocol"),
                        QStringLiteral("https"), QStringLiteral("--web"), QStringLiteral("--clipboard"),
                        QStringLiteral("--skip-ssh-key")});
  process.setProcessEnvironment(environment(true));
  process.setInputChannelMode(QProcess::ForwardedInputChannel);
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(QIODevice::ReadOnly);
  if (!process.waitForStarted()) {
    throw std::runtime_error(QStringLiteral("GitHub sign-in could not start: %1")
                                 .arg(process.errorString()).toStdString());
  }
  process.closeWriteChannel();

  QString combined;
  QElapsedTimer elapsed;
  elapsed.start();
  while (process.state() != QProcess::NotRunning) {
    if ((canceled && canceled->load()) || elapsed.elapsed() > 15 * 60 * 1000) {
      process.kill();
      process.waitForFinished(2000);
      throw std::runtime_error(canceled && canceled->load() ? "GitHub sign-in canceled." : "GitHub sign-in timed out. Try again.");
    }
    process.waitForReadyRead(100);
    const auto latest = cleanOutput(QString::fromUtf8(process.readAllStandardOutput()) +
                                    QString::fromUtf8(process.readAllStandardError()));
    if (!latest.isEmpty()) {
      combined = (combined + u'\n' + latest).last(4000);
      if (onProgress) onProgress(loginProgressFromOutput(combined, latest));
    }
  }
  process.waitForFinished();
  const auto finalOutput = cleanOutput(QString::fromUtf8(process.readAllStandardError()) +
                                       QString::fromUtf8(process.readAllStandardOutput()));
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    const auto detail = lastNonEmptyLine(finalOutput.isEmpty() ? combined : finalOutput);
    throw std::runtime_error((detail.isEmpty()
                                  ? QStringLiteral("GitHub CLI exited with code %1.").arg(process.exitCode())
                                  : detail).toStdString());
  }
}

}  // namespace relay
