#pragma once
#include <QString>

namespace relay {
// Synchronous worker-only access to the operating system credential vault.
// No files, shell commands or plaintext fallback.
class CredentialStore {
 public:
  virtual ~CredentialStore() = default;
  virtual void write(const QString& accountId, const QString& token);
  virtual QString read(const QString& accountId);
  virtual void remove(const QString& accountId);
};
}  // namespace relay
