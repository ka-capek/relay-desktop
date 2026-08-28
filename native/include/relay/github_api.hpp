#pragma once

#include "relay/domain.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace relay {

struct GitHubRepositoryPage {
  QList<GitHubRepository> repositories;
  qsizetype hidden{};
};

class GitHubApi final {
 public:
  [[nodiscard]] QJsonObject profile(const QString& token, const QString& handle) const;
  [[nodiscard]] GitHubRepositoryPage repositories(const QString& token, const QString& handle) const;
  [[nodiscard]] QList<EmailChoice> emailChoices(const Account& account, const QString& token) const;

  static QString noreplyAddress(const Account& account);
  static bool isValidEmail(const QString& email);
  static QList<EmailChoice> emailChoicesFromJson(const Account& account, const QJsonArray& emails);
  static QString resolveCommitEmail(const Account& account, const QString& requested);

 private:
  struct Response {
    int status{};
    QJsonDocument body;
    QByteArray linkHeader;
  };

  [[nodiscard]] Response get(const QUrl& url, const QString& token) const;
};

}  // namespace relay
