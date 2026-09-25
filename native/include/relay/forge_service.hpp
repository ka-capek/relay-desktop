#pragma once
#include "relay/forge_types.hpp"
#include <QByteArray>
#include <QJsonDocument>
#include <QList>
#include <QUrl>
#include <functional>

namespace relay {
class ForgeService final {
 public:
  struct Response { int status{}; QJsonDocument body; QByteArray nextPage; QByteArray link; };
  using Transport = std::function<Response(const QUrl&, const QString&)>;
  explicit ForgeService(Transport transport = {});
  static QString normalizeServerUrl(const QString& server);
  static QUrl tokenSettingsUrl(ForgeKind kind, const QString& server);
  ForgeAccount profile(ForgeKind kind, const QString& server, const QString& token) const;
  QList<ForgeRepository> repositories(const ForgeAccount& account, const QString& token) const;
 private:
  Transport transport_;
  static Response networkGet(const QUrl& url, const QString& token);
  Response get(const QUrl& url, const QString& token) const;
};
}  // namespace relay
