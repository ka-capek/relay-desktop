#pragma once

#include "relay/domain.hpp"

#include <QList>
#include <QProcessEnvironment>
#include <QString>

#include <functional>
#include <atomic>
#include <memory>

namespace relay {

struct AuthenticatedAccount {
  QString handle;
  bool active{};
  QString state;
  QString tokenSource;
};

struct GitHubAuthContext {
  QString sourceRoot;
  QString resourcesRoot;
  QString userDataPath;
  bool packaged{};
};

class GitHubAuth final {
 public:
  explicit GitHubAuth(GitHubAuthContext context);

  [[nodiscard]] QString executable() const;
  [[nodiscard]] QProcessEnvironment environment(bool interactive = false) const;
  [[nodiscard]] QList<AuthenticatedAccount> authenticatedAccounts() const;
  [[nodiscard]] QString accountToken(const QString& handle) const;
  void switchAccount(const QString& handle) const;
  void removeAccount(const QString& handle) const;
  void login(const std::function<void(const GitHubLoginProgress&)>& onProgress = {},
             const std::shared_ptr<std::atomic_bool>& canceled = {}) const;

  static QString cleanOutput(QString value);
  static QList<AuthenticatedAccount> accountsFromStatus(const QByteArray& json);
  static GitHubLoginProgress loginProgressFromOutput(const QString& combinedOutput,
                                                     const QString& latestOutput);

 private:
  GitHubAuthContext context_;
};

}  // namespace relay
