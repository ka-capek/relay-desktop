#pragma once

#include "relay/domain.hpp"

#include <QString>

#include <optional>

namespace relay {

struct ParsedSshRemote {
  QString user;
  QString host;
  std::optional<quint16> port;
  QString path;
};

class SshService final {
 public:
  explicit SshService(QString sourceRoot = {}, QString resourcesRoot = {});

  static std::optional<ParsedSshRemote> parseRemote(const QString& remote);
  static bool isGitHubRemote(const QString& remote);
  static QString shellQuote(const QString& value);
  static std::optional<SshProfile> normalizeProfile(const SshProfile& profile);
  [[nodiscard]] QString executable() const;
  [[nodiscard]] QString commandFor(const SshProfile& profile) const;
  [[nodiscard]] SshTestResult testConnection(const SshProfile& profile) const;
  static SshTestResult describeResult(const QString& host, int code, const QString& output);

 private:
  QString sourceRoot_;
  QString resourcesRoot_;
};

}  // namespace relay
