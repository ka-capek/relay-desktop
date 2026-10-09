#pragma once

#include "relay/domain.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace relay {

struct GitHubRepositoryName {
  QString owner;
  QString name;
};

struct GitHubRepositoryPage {
  QList<GitHubRepository> repositories;
  qsizetype hidden{};
};

class GitHubApi final {
 public:
  [[nodiscard]] QJsonObject profile(const QString& token, const QString& handle) const;
  [[nodiscard]] GitHubRepositoryPage repositories(const QString& token, const QString& handle) const;
  [[nodiscard]] QList<EmailChoice> emailChoices(const Account& account, const QString& token) const;

  [[nodiscard]] QString createRepository(const QString& token, const QString& handle,
      const QString& name, const QString& description, bool isPrivate) const;
  // Relay's own message for a repository-creation response other than 201.
  // A server error may arrive after GitHub created the repository.
  [[nodiscard]] static QString repositoryCreationError(int status, const QString& handle,
                                                       const QString& name);
  [[nodiscard]] static QJsonObject newRepositoryPayload(const QString& name,
      const QString& description, bool isPrivate);
  static QString noreplyAddress(const Account& account);
  static bool isValidEmail(const QString& email);
  static QList<EmailChoice> emailChoicesFromJson(const Account& account, const QJsonArray& emails);
  static QString resolveCommitEmail(const Account& account, const QString& requested);

  // owner/name of a github.com HTTPS or SSH remote; nothing for any other host.
  [[nodiscard]] static std::optional<GitHubRepositoryName> repositoryFromRemote(const QString& remote);
  // GitHub's page for opening a pull request from branch to the default branch.
  [[nodiscard]] static QString pullRequestCreationUrl(const GitHubRepositoryName& repository, const QString& branch);
  // The open pull request whose head is owner:branch, or an empty string.
  [[nodiscard]] QString openPullRequestUrl(const QString& token, const QString& handle,
                                           const GitHubRepositoryName& repository, const QString& branch) const;
  [[nodiscard]] static QString pullRequestUrlFromJson(const QJsonDocument& body, const GitHubRepositoryName& repository);

 private:
  struct Response {
    int status{};
    QJsonDocument body;
    QByteArray linkHeader;
  };

  [[nodiscard]] Response get(const QUrl& url, const QString& token, const QJsonObject& body = {}) const;
};

}  // namespace relay
