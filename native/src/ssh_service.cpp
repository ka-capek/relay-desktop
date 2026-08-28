#include "relay/ssh_service.hpp"

#include "relay/process_runner.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <stdexcept>

namespace relay {

SshService::SshService(QString sourceRoot, QString resourcesRoot)
    : sourceRoot_(std::move(sourceRoot)), resourcesRoot_(std::move(resourcesRoot)) {}

std::optional<ParsedSshRemote> SshService::parseRemote(const QString& remote) {
  const auto value = remote.trimmed();
  if (value.isEmpty()) return std::nullopt;
  static const QRegularExpression explicitPattern(
      QStringLiteral("^ssh://(?:([^@/]+)@)?([^:/]+)(?::(\\d+))?(/.*)?$"),
      QRegularExpression::CaseInsensitiveOption);
  auto match = explicitPattern.match(value);
  if (match.hasMatch()) {
    const auto portValue = match.captured(3).toUInt();
    return ParsedSshRemote{match.captured(1), match.captured(2),
                           portValue > 0 && portValue < 65536
                               ? std::optional<quint16>(static_cast<quint16>(portValue)) : std::nullopt,
                           match.captured(4)};
  }
  static const QRegularExpression scpPattern(
      QStringLiteral("^(?:([^@\\s]+)@)?([^@:\\s/\\\\]+):(?!//)(.+)$"));
  match = scpPattern.match(value);
  if (match.hasMatch() && !(match.captured(2).size() == 1 && match.captured(2).front().isLetter())) {
    return ParsedSshRemote{match.captured(1), match.captured(2), std::nullopt, match.captured(3)};
  }
  return std::nullopt;
}

bool SshService::isGitHubRemote(const QString& remote) {
  if (remote.startsWith(QStringLiteral("https://github.com/"), Qt::CaseInsensitive)) return true;
  const auto parsed = parseRemote(remote);
  return parsed && parsed->host.compare(QStringLiteral("github.com"), Qt::CaseInsensitive) == 0;
}

QString SshService::shellQuote(const QString& value) {
  auto escaped = value;
  escaped.replace(u'\'', QStringLiteral("'\\''"));
  return u'\'' + escaped + u'\'';
}

std::optional<SshProfile> SshService::normalizeProfile(const SshProfile& profile) {
  SshProfile result = profile;
  result.host = result.host.trimmed().toLower();
  static const QRegularExpression invalidHost(QStringLiteral("[\\s/\\\\]"));
  if (result.host.isEmpty() || invalidHost.match(result.host).hasMatch()) return std::nullopt;
  if (result.id.trimmed().isEmpty()) {
    result.id = QStringLiteral("ssh-%1-%2").arg(result.host).arg(QDateTime::currentMSecsSinceEpoch());
  }
  result.label = result.label.trimmed().isEmpty() ? result.host : result.label.trimmed();
  result.user = result.user.trimmed();
  result.identityFile = result.identityFile.trimmed();
  return result;
}

QString SshService::executable() const {
#ifdef Q_OS_WIN
  const QStringList roots{resourcesRoot_.isEmpty() ? QString{} : QDir(resourcesRoot_).filePath(QStringLiteral("git")),
                          sourceRoot_.isEmpty() ? QString{} : QDir(sourceRoot_).filePath(QStringLiteral("runtime/git/win-x64"))};
  for (const auto& root : roots) {
    const auto candidate = QDir(root).filePath(QStringLiteral("usr/bin/ssh.exe"));
    if (QFileInfo::exists(candidate)) return candidate;
  }
  return QStringLiteral("ssh.exe");
#else
  return QStringLiteral("ssh");
#endif
}

QString SshService::commandFor(const SshProfile& profile) const {
  const auto normalized = normalizeProfile(profile);
  if (!normalized) return {};
  QStringList parts{shellQuote(executable())};
  if (!normalized->identityFile.isEmpty()) {
    parts.append({QStringLiteral("-i"), shellQuote(normalized->identityFile)});
    if (normalized->identitiesOnly) {
      parts.append({QStringLiteral("-o"), shellQuote(QStringLiteral("IdentitiesOnly=yes"))});
    }
  }
  if (normalized->port) parts.append({QStringLiteral("-p"), QString::number(*normalized->port)});
  return parts.size() == 1 ? QString{} : parts.join(u' ');
}

SshTestResult SshService::describeResult(const QString& host, const int code, const QString& output) {
  const auto contains = [&output](const QString& expression) {
    return QRegularExpression(expression, QRegularExpression::CaseInsensitiveOption).match(output).hasMatch();
  };
  if (contains(QStringLiteral("successfully authenticated|you've successfully|welcome to|logged in as")))
    return {true, QStringLiteral("%1 accepted the key.").arg(host)};
  if (contains(QStringLiteral("permission denied")))
    return {false, QStringLiteral("%1 refused the key. Add the matching public key to your account on %1, or choose a different identity.").arg(host)};
  if (contains(QStringLiteral("could not resolve hostname|name or service not known|nodename nor servname")))
    return {false, QStringLiteral("%1 could not be resolved. Check the host name and your network connection.").arg(host)};
  if (contains(QStringLiteral("host key verification failed|remote host identification has changed")))
    return {false, QStringLiteral("The host key for %1 is not trusted yet. Connect once with ssh in a terminal to review and accept it.").arg(host)};
  if (contains(QStringLiteral("connection timed out|operation timed out|connection refused")))
    return {false, QStringLiteral("%1 did not answer on the SSH port. Check the port and whether the host is reachable.").arg(host)};
  if (contains(QStringLiteral("passphrase|agent has no identities|no such identity|unprotected private key")))
    return {false, QStringLiteral("%1 needs a key your SSH agent is not offering. Add it with ssh-add in a terminal; Relay never handles passphrases.").arg(host)};
  if (code == 0) return {true, QStringLiteral("%1 accepted the connection.").arg(host)};
  return {false, QStringLiteral("Relay could not authenticate to %1. Try the same remote with ssh in a terminal to see the full response.").arg(host)};
}

SshTestResult SshService::testConnection(const SshProfile& profile) const {
  const auto normalized = normalizeProfile(profile);
  if (!normalized) throw std::runtime_error("Enter a host name for this SSH identity.");
  if (!normalized->identityFile.isEmpty() && !QFileInfo::exists(normalized->identityFile)) {
    throw std::runtime_error("That private key file does not exist. Check the path, or clear it to use your SSH agent.");
  }
  QStringList arguments{QStringLiteral("-T"), QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                        QStringLiteral("-o"), QStringLiteral("ConnectTimeout=10"),
                        QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=accept-new")};
  if (!normalized->identityFile.isEmpty()) {
    arguments.append({QStringLiteral("-i"), normalized->identityFile});
    if (normalized->identitiesOnly) arguments.append({QStringLiteral("-o"), QStringLiteral("IdentitiesOnly=yes")});
  }
  if (normalized->port) arguments.append({QStringLiteral("-p"), QString::number(*normalized->port)});
  arguments.append(QStringLiteral("%1@%2").arg(normalized->user.isEmpty() ? QStringLiteral("git") : normalized->user,
                                               normalized->host));
  ProcessRequest request{executable(), arguments};
  request.timeoutMilliseconds = 20000;
  try {
    const auto result = ProcessRunner::run(request);
    return describeResult(normalized->host, result.exitCode,
                          QString::fromUtf8(result.standardOutput + result.standardError));
  } catch (const ProcessError& error) {
    if (error.qMessage().contains(QStringLiteral("timed out"), Qt::CaseInsensitive)) {
      return {false, QStringLiteral("%1 did not answer within 20 seconds.").arg(normalized->host)};
    }
    return describeResult(normalized->host, error.result().exitCode,
                          QString::fromUtf8(error.result().standardOutput + error.result().standardError));
  }
}

}  // namespace relay
