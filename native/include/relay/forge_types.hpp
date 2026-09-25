#pragma once
#include <QJsonObject>
#include <QString>
#include <QMetaType>
#include <QList>

namespace relay {
enum class ForgeKind { gitea, gitlab };
// Public metadata only. Credentials never belong in these values.
struct ForgeAccount {
  QString id;
  ForgeKind kind{ForgeKind::gitea};
  QString serverUrl;
  QString userId;
  QString handle;
  QString name;
  QString credentialId;
};
struct ForgeRepository {
  QString id;
  QString name;
  QString fullName;
  QString description;
  QString webUrl;
  QString sshUrl;
  QString httpsUrl;
  bool isPrivate{};
  bool archived{};
};
QJsonObject forgeAccountToJson(const ForgeAccount& account);
ForgeAccount forgeAccountFromJson(const QJsonObject& object);
}  // namespace relay

Q_DECLARE_METATYPE(relay::ForgeAccount)
Q_DECLARE_METATYPE(QList<relay::ForgeRepository>)
