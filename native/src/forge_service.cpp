#include "relay/forge_service.hpp"
#include "relay/ssh_service.hpp"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <stdexcept>

namespace relay {
namespace {
QString kindName(ForgeKind kind) { return kind == ForgeKind::gitlab ? QStringLiteral("gitlab") : QStringLiteral("gitea"); }
QString accountIdentifier(ForgeKind kind, const QString& server, const QString& userId) {
  const auto digest = QCryptographicHash::hash((kindName(kind) + u'\n' + server + u'\n' + userId).toUtf8(), QCryptographicHash::Sha256).toHex();
  return QStringLiteral("forge-") + QString::fromLatin1(digest);
}
QString apiPath(ForgeKind kind) { return kind == ForgeKind::gitlab ? QStringLiteral("/api/v4") : QStringLiteral("/api/v1"); }
QString identifier(const QJsonValue& value) { return value.isString() ? value.toString() : QString::number(value.toInteger()); }
void validateToken(const QString& token) {
  if (token.isEmpty() || token.toUtf8().size() > 2500 || token.contains(QRegularExpression(QStringLiteral("[\\s\\x00-\\x1f\\x7f]"))))
    throw std::runtime_error("Enter a valid API access token (without whitespace).");
}
QString safeSsh(const QString& value) {
  if (value.contains(QRegularExpression(QStringLiteral("[\\s\\x00-\\x1f\\x7f]"))) || value.contains(QStringLiteral("::"))) return {};
  const auto parsed = SshService::parseRemote(value);
  if (!parsed || parsed->path.isEmpty() || parsed->host.startsWith(u'-') || parsed->user.startsWith(u'-') ||
      parsed->user.contains(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")))) return {};
  const QUrl host(QStringLiteral("https://") + parsed->host);
  if (!host.isValid() || host.host().isEmpty() || !host.userInfo().isEmpty() || !host.path().isEmpty() || host.hasQuery() || host.hasFragment()) return {};
  if (value.startsWith(QStringLiteral("ssh://"), Qt::CaseInsensitive)) {
    const QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() || !url.password().isEmpty() || url.hasQuery() || url.hasFragment() || url.port() == 0) return {};
  }
  return value;
}
QString safeHttps(const QString& value) {
  const QUrl url(value);
  return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty() && url.userInfo().isEmpty()
      ? url.toString() : QString{};
}
}
QJsonObject forgeAccountToJson(const ForgeAccount& account) {
  return {{QStringLiteral("id"), account.id}, {QStringLiteral("kind"), kindName(account.kind)},
    {QStringLiteral("serverUrl"), account.serverUrl}, {QStringLiteral("userId"), account.userId},
    {QStringLiteral("handle"), account.handle}, {QStringLiteral("name"), account.name}, {QStringLiteral("credentialId"), account.credentialId}};
}
ForgeAccount forgeAccountFromJson(const QJsonObject& object) {
  const auto kind = object.value(QStringLiteral("kind")).toString();
  if (kind != QStringLiteral("gitea") && kind != QStringLiteral("gitlab")) throw std::runtime_error("Unknown Git server type.");
  ForgeAccount result{object.value(QStringLiteral("id")).toString(), kind == QStringLiteral("gitlab") ? ForgeKind::gitlab : ForgeKind::gitea,
    ForgeService::normalizeServerUrl(object.value(QStringLiteral("serverUrl")).toString()), object.value(QStringLiteral("userId")).toString(),
    object.value(QStringLiteral("handle")).toString(), object.value(QStringLiteral("name")).toString(), object.value(QStringLiteral("credentialId")).toString()};
  if (result.id.isEmpty() || result.userId.isEmpty() || result.handle.isEmpty()) throw std::runtime_error("Invalid saved Git server account.");
  if (result.id != accountIdentifier(result.kind, result.serverUrl, result.userId))
    throw std::runtime_error("Saved Git server account identity does not match its server and user.");
  if (!result.credentialId.isEmpty() && !result.credentialId.startsWith(result.id + u':'))
    throw std::runtime_error("Git server credential reference belongs to a different account.");
  if (result.credentialId.size() > 240 || result.credentialId.contains(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.:-]")))) throw std::runtime_error("Invalid credential reference.");
  return result;
}
ForgeService::ForgeService(Transport transport) : transport_(transport ? std::move(transport) : networkGet) {}
QString ForgeService::normalizeServerUrl(const QString& server) {
  QUrl url(server.trimmed(), QUrl::StrictMode);
  if (!url.isValid() || url.scheme() != QStringLiteral("https") || url.host().isEmpty() ||
      !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
    throw std::runtime_error("Use the HTTPS address of your Git server, without credentials, query or fragment.");
  url = url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash);
  if (url.path() == QStringLiteral("/")) url.setPath({});
  if (url.port() == 443) url.setPort(-1);
  return url.toString(QUrl::FullyEncoded);
}
QUrl ForgeService::tokenSettingsUrl(ForgeKind kind, const QString& server) {
  return QUrl(normalizeServerUrl(server) + (kind == ForgeKind::gitlab ? QStringLiteral("/-/user_settings/personal_access_tokens") : QStringLiteral("/user/settings/applications")));
}
ForgeService::Response ForgeService::networkGet(const QUrl& url, const QString& token) {
  QNetworkAccessManager manager;
  QNetworkRequest request(url);
  request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
  request.setRawHeader("Accept", "application/json");
  request.setRawHeader("User-Agent", "Relay-Desktop");
  request.setTransferTimeout(30000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  auto* reply = manager.get(request);
  bool oversized = false;
  QObject::connect(reply, &QNetworkReply::readyRead, reply, [&] {
    if (reply->bytesAvailable() > 10 * 1024 * 1024) { oversized = true; reply->abort(); }
  });
  QEventLoop loop;
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  deadline.start(30000);
  loop.exec();
  if (oversized || reply->bytesAvailable() > 10 * 1024 * 1024) throw std::runtime_error("Git server response exceeded the size limit.");
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (status == 0) throw std::runtime_error("Could not reach the Git server securely. Check its address, certificate and connection.");
  // Never surface remote error bodies: they can reflect credentials.
  if (status < 200 || status >= 300) return {status, {}, {}, {}};
  QJsonParseError error;
  const auto body = QJsonDocument::fromJson(reply->readAll(), &error);
  if (error.error != QJsonParseError::NoError) throw std::runtime_error("Git server returned invalid JSON.");
  return {status, body, reply->rawHeader("X-Next-Page"), reply->rawHeader("Link")};
}
ForgeService::Response ForgeService::get(const QUrl& url, const QString& token) const {
  validateToken(token);
  if (url.scheme() != QStringLiteral("https") || url.host().isEmpty() || !url.userInfo().isEmpty())
    throw std::runtime_error("Refusing an unsafe Git server request.");
  auto response = transport_(url, token);
  if (response.status == 401 || response.status == 403) throw std::runtime_error("Git server denied access. Check your token, its scopes and repository permissions.");
  if (response.status < 200 || response.status >= 300)
    throw std::runtime_error(QStringLiteral("Git server request failed (HTTP %1). Redirects are not followed; check the server address.").arg(response.status).toStdString());
  return response;
}
ForgeAccount ForgeService::profile(ForgeKind kind, const QString& server, const QString& token) const {
  const auto base = normalizeServerUrl(server);
  const auto response = get(QUrl(base + apiPath(kind) + QStringLiteral("/user")), token);
  const auto object = response.body.object();
  const auto userId = identifier(object.value(QStringLiteral("id")));
  const auto handle = object.value(kind == ForgeKind::gitlab ? QStringLiteral("username") : QStringLiteral("login")).toString();
  if (!response.body.isObject() || userId == QStringLiteral("0") || userId.isEmpty() || handle.isEmpty())
    throw std::runtime_error("Git server returned an invalid account profile.");
  return {accountIdentifier(kind, base, userId), kind, base, userId, handle,
    object.value(kind == ForgeKind::gitlab ? QStringLiteral("name") : QStringLiteral("full_name")).toString(handle), {}};
}
QList<ForgeRepository> ForgeService::repositories(const ForgeAccount& account, const QString& token) const {
  if (!account.id.isEmpty() || !account.credentialId.isEmpty())
    static_cast<void>(forgeAccountFromJson(forgeAccountToJson(account)));
  const auto base = normalizeServerUrl(account.serverUrl);
  const bool gitlab = account.kind == ForgeKind::gitlab;
  const QString endpoint = base + apiPath(account.kind) + (gitlab ? QStringLiteral("/projects") : QStringLiteral("/user/repos"));
  QList<ForgeRepository> result;
  QSet<QString> seen;
  QElapsedTimer duration;
  duration.start();
  for (int page = 1; page <= 1000; ++page) {
    if (duration.elapsed() > 120000) throw std::runtime_error("Repository listing timed out. No incomplete list was saved.");
    QUrl url(endpoint);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(gitlab ? QStringLiteral("per_page") : QStringLiteral("limit"), QStringLiteral("100"));
    if (gitlab) { query.addQueryItem(QStringLiteral("membership"), QStringLiteral("true")); query.addQueryItem(QStringLiteral("order_by"), QStringLiteral("id")); query.addQueryItem(QStringLiteral("sort"), QStringLiteral("asc")); }
    url.setQuery(query);
    const auto response = get(url, token);
    if (!response.body.isArray()) throw std::runtime_error("Git server returned an invalid repository list.");
    const auto entries = response.body.array();
    if (entries.isEmpty()) return result;
    const auto previousSize = result.size();
    for (const auto& entry : entries) {
      const auto o = entry.toObject();
      const auto id = identifier(o.value(QStringLiteral("id")));
      const auto fullName = o.value(gitlab ? QStringLiteral("path_with_namespace") : QStringLiteral("full_name")).toString();
      if (id == QStringLiteral("0") || id.isEmpty() || fullName.isEmpty()) throw std::runtime_error("Git server returned an invalid repository.");
      if (seen.contains(id)) continue;
      if (result.size() >= 100000) throw std::runtime_error("Repository listing exceeded 100000 entries. No incomplete list was saved.");
      seen.insert(id);
      const auto ssh = safeSsh(o.value(gitlab ? QStringLiteral("ssh_url_to_repo") : QStringLiteral("ssh_url")).toString());
      result.append({id, o.value(QStringLiteral("name")).toString(), fullName, o.value(QStringLiteral("description")).toString(),
        safeHttps(o.value(gitlab ? QStringLiteral("web_url") : QStringLiteral("html_url")).toString()), ssh,
        safeHttps(o.value(gitlab ? QStringLiteral("http_url_to_repo") : QStringLiteral("clone_url")).toString()),
        gitlab ? o.value(QStringLiteral("visibility")).toString() != QStringLiteral("public") : o.value(QStringLiteral("private")).toBool(),
        o.value(QStringLiteral("archived")).toBool()});
    }
    if (result.size() == previousSize) throw std::runtime_error("Git server repeated a repository page. Listing stopped to avoid an incomplete result.");
    // Request our own sequential pages, never remote-provided URLs. An empty page
    // ends the list even if a server caps page size below the requested 100.
  }
  throw std::runtime_error("Git server repository listing exceeded 1000 pages. No incomplete list was saved.");
}
}  // namespace relay
