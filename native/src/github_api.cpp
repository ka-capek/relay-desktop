#include "relay/github_api.hpp"

#include <QEventLoop>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <stdexcept>

namespace relay {
namespace {

QString githubError(const int status, const QJsonDocument& body, const QString& fallback) {
  if (status == 401) return fallback;
  const auto message = body.object().value(QStringLiteral("message")).toString();
  return message.isEmpty() ? QStringLiteral("GitHub request failed (%1).").arg(status) : message;
}

}  // namespace

GitHubApi::Response GitHubApi::get(const QUrl& url, const QString& token) const {
  QNetworkAccessManager manager;
  QNetworkRequest request(url);
  request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/vnd.github+json"));
  request.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8());
  request.setRawHeader(QByteArrayLiteral("User-Agent"), QByteArrayLiteral("Relay-Desktop"));
  request.setRawHeader(QByteArrayLiteral("X-GitHub-Api-Version"), QByteArrayLiteral("2022-11-28"));
  request.setTransferTimeout(30000);

  auto* reply = manager.get(request);
  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();

  const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const auto payload = reply->readAll();
  const auto link = reply->rawHeader(QByteArrayLiteral("Link"));
  const auto networkError = reply->error();
  const auto networkMessage = reply->errorString();
  reply->deleteLater();
  if (networkError != QNetworkReply::NoError && status == 0) {
    throw std::runtime_error(QStringLiteral("GitHub could not be reached: %1").arg(networkMessage).toStdString());
  }

  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(payload, &parseError);
  if (!payload.isEmpty() && parseError.error != QJsonParseError::NoError) {
    throw std::runtime_error("GitHub returned an invalid response.");
  }
  return {status, document, link};
}

QJsonObject GitHubApi::profile(const QString& token, const QString& handle) const {
  const auto response = get(QUrl(QStringLiteral("https://api.github.com/user")), token);
  if (response.status < 200 || response.status >= 300 || !response.body.isObject()) {
    throw std::runtime_error(githubError(response.status, response.body,
        QStringLiteral("GitHub credentials for @%1 expired. Sign in again.").arg(handle)).toStdString());
  }
  return response.body.object();
}

GitHubRepositoryPage GitHubApi::repositories(const QString& token, const QString& handle) const {
  GitHubRepositoryPage result;
  QUrl next(QStringLiteral("https://api.github.com/user/repos"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("visibility"), QStringLiteral("all"));
  query.addQueryItem(QStringLiteral("affiliation"), QStringLiteral("owner,collaborator,organization_member"));
  query.addQueryItem(QStringLiteral("sort"), QStringLiteral("updated"));
  query.addQueryItem(QStringLiteral("direction"), QStringLiteral("desc"));
  query.addQueryItem(QStringLiteral("per_page"), QStringLiteral("100"));
  next.setQuery(query);

  static const QRegularExpression nextLink(QStringLiteral("<([^>]+)>;\\s*rel=\"next\""));
  while (next.isValid() && !next.isEmpty()) {
    const auto response = get(next, token);
    if (response.status < 200 || response.status >= 300 || !response.body.isArray()) {
      throw std::runtime_error(githubError(response.status, response.body,
          QStringLiteral("GitHub credentials for @%1 expired. Sign in again.").arg(handle)).toStdString());
    }
    for (const auto& entry : response.body.array()) {
      const auto object = entry.toObject();
      const auto permissions = object.value(QStringLiteral("permissions"));
      bool canPush = true;
      if (permissions.isObject()) {
        const auto values = permissions.toObject();
        canPush = values.value(QStringLiteral("push")).toBool() ||
                  values.value(QStringLiteral("maintain")).toBool() ||
                  values.value(QStringLiteral("admin")).toBool();
      }
      if (!canPush || object.value(QStringLiteral("archived")).toBool()) {
        ++result.hidden;
        continue;
      }
      const auto fullName = object.value(QStringLiteral("full_name")).toString();
      result.repositories.append({
          QString::number(object.value(QStringLiteral("id")).toInteger()),
          object.value(QStringLiteral("name")).toString(),
          fullName,
          object.value(QStringLiteral("owner")).toObject().value(QStringLiteral("login")).toString(
              fullName.section(u'/', 0, 0)),
          object.value(QStringLiteral("description")).toString(),
          object.value(QStringLiteral("private")).toBool(),
          object.value(QStringLiteral("archived")).toBool(),
          object.value(QStringLiteral("fork")).toBool(),
          object.value(QStringLiteral("clone_url")).toString(),
          QDateTime::fromString(object.value(QStringLiteral("updated_at")).toString(), Qt::ISODate),
      });
    }
    const auto match = nextLink.match(QString::fromLatin1(response.linkHeader));
    next = match.hasMatch() ? QUrl(match.captured(1)) : QUrl{};
  }
  return result;
}

QString GitHubApi::noreplyAddress(const Account& account) {
  const auto githubId = account.githubIdText.isEmpty() ? QString::number(account.githubId) : account.githubIdText;
  return QStringLiteral("%1+%2@users.noreply.github.com").arg(githubId, account.handle);
}

bool GitHubApi::isValidEmail(const QString& email) {
  static const QRegularExpression pattern(QStringLiteral("^[^\\s@]+@[^\\s@]+\\.[^\\s@]+$"));
  return pattern.match(email.trimmed()).hasMatch();
}

QList<EmailChoice> GitHubApi::emailChoicesFromJson(const Account& account, const QJsonArray& emails) {
  const auto noreply = noreplyAddress(account);
  QList<EmailChoice> choices{{noreply, QStringLiteral("GitHub noreply"), false, true}};
  QSet<QString> seen{noreply.toLower()};
  for (const auto& entry : emails) {
    const auto object = entry.toObject();
    const auto email = object.value(QStringLiteral("email")).toString().trimmed();
    if (email.isEmpty() || !isValidEmail(email) || !object.value(QStringLiteral("verified")).toBool() ||
        seen.contains(email.toLower())) continue;
    seen.insert(email.toLower());
    const auto primary = object.value(QStringLiteral("primary")).toBool();
    choices.append({email, primary ? QStringLiteral("Primary") : QStringLiteral("Verified"), primary, false});
  }
  return choices;
}

QString GitHubApi::resolveCommitEmail(const Account& account, const QString& requested) {
  const auto value = requested.trimmed();
  if (value.isEmpty()) return noreplyAddress(account);
  if (!isValidEmail(value)) throw std::runtime_error("Enter a valid email address.");
  return value;
}

QList<EmailChoice> GitHubApi::emailChoices(const Account& account, const QString& token) const {
  try {
    const auto response = get(QUrl(QStringLiteral("https://api.github.com/user/emails")), token);
    if (response.status >= 200 && response.status < 300 && response.body.isArray()) {
      return emailChoicesFromJson(account, response.body.array());
    }
  } catch (...) {
    // Missing user:email scope and transient network failures degrade to noreply.
  }
  return emailChoicesFromJson(account, {});
}

}  // namespace relay
