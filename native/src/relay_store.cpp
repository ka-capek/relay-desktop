#include "relay/relay_store.hpp"

#include "relay/app_paths.hpp"
#include "relay/repository_order.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringList>

#include <cmath>
#include <optional>
#include <utility>

namespace relay {
namespace {

QJsonObject sanitizedAccount(QJsonObject account) {
  static const QStringList sensitiveFields{
      QStringLiteral("encryptedToken"), QStringLiteral("token"), QStringLiteral("oauthToken"),
      QStringLiteral("accessToken"), QStringLiteral("refreshToken"), QStringLiteral("password"),
      QStringLiteral("passphrase"), QStringLiteral("privateKey"), QStringLiteral("privateKeyContents"),
  };
  for (const auto& field : sensitiveFields) account.remove(field);
  return account;
}

std::optional<QJsonObject> normalizedSshProfile(QJsonObject profile, const qint64 nowMilliseconds) {
  const auto host = profile.value(QStringLiteral("host")).toString().trimmed().toLower();
  static const QRegularExpression forbiddenHostCharacter(QStringLiteral(R"([\s/\\])"));
  if (host.isEmpty() || forbiddenHostCharacter.match(host).hasMatch()) return std::nullopt;

  auto id = profile.value(QStringLiteral("id")).toString().trimmed();
  if (id.isEmpty()) id = QStringLiteral("ssh-%1-%2").arg(host).arg(nowMilliseconds);
  auto label = profile.value(QStringLiteral("label")).toString().trimmed();
  if (label.isEmpty()) label = host;

  const auto user = profile.value(QStringLiteral("user")).toString().trimmed();
  const auto identityFile = profile.value(QStringLiteral("identityFile")).toString().trimmed();
  QJsonValue port = QJsonValue::Null;
  const auto requestedPort = profile.value(QStringLiteral("port"));
  if (requestedPort.isDouble()) {
    const auto number = requestedPort.toDouble();
    if (std::floor(number) == number && number > 0 && number < 65536) port = number;
  }

  return QJsonObject{
      {QStringLiteral("id"), id},
      {QStringLiteral("label"), label},
      {QStringLiteral("host"), host},
      {QStringLiteral("user"), user.isEmpty() ? QJsonValue::Null : QJsonValue(user)},
      {QStringLiteral("port"), port},
      {QStringLiteral("identityFile"), identityFile.isEmpty() ? QJsonValue::Null : QJsonValue(identityFile)},
      {QStringLiteral("identitiesOnly"),
       !(profile.value(QStringLiteral("identitiesOnly")).isBool()
         && !profile.value(QStringLiteral("identitiesOnly")).toBool())},
  };
}

}  // namespace

StoreError::StoreError(const QString& message) : std::runtime_error(message.toStdString()) {}

QJsonObject emptyStoreJson() {
  return {
      {QStringLiteral("accounts"), QJsonArray{}},
      {QStringLiteral("activeAccountId"), QJsonValue::Null},
      {QStringLiteral("repositories"), QJsonArray{}},
      {QStringLiteral("selectedRepositoryPath"), QJsonValue::Null},
      {QStringLiteral("repositoryAccounts"), QJsonObject{}},
      {QStringLiteral("repositoryOrder"),
       QJsonObject{
           {QStringLiteral("mode"), QStringLiteral("manual")},
           {QStringLiteral("direction"), QStringLiteral("asc")},
       }},
      {QStringLiteral("manualOrder"), QJsonArray{}},
      {QStringLiteral("sshProfiles"), QJsonArray{}},
      {QStringLiteral("repositorySshProfiles"), QJsonObject{}},
  };
}

QJsonObject normalizeStoreJson(QJsonObject store, const QDateTime& now) {
  const auto defaults = emptyStoreJson();
  for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it) {
    if (!store.contains(it.key())) store.insert(it.key(), it.value());
  }

  QJsonArray accounts;
  for (const auto& value : store.value(QStringLiteral("accounts")).toArray()) {
    if (!value.isObject()) continue;
    auto account = value.toObject();
    if (account.value(QStringLiteral("authSource")).toString() != QStringLiteral("github-cli")) continue;
    accounts.push_back(sanitizedAccount(std::move(account)));
  }
  store.insert(QStringLiteral("accounts"), accounts);
  if (!store.value(QStringLiteral("activeAccountId")).isString()) {
    store.insert(QStringLiteral("activeAccountId"), QJsonValue::Null);
  }
  if (!store.value(QStringLiteral("repositoryAccounts")).isObject()) {
    store.insert(QStringLiteral("repositoryAccounts"), QJsonObject{});
  }

  // A remembered repository is sidebar metadata, not an instruction to open
  // it. This is reset both when reading and before writing.
  store.insert(QStringLiteral("selectedRepositoryPath"), QJsonValue::Null);
  store = normalizeRepositoryOrdering(std::move(store), now);

  QSet<QString> profileIds;
  QJsonArray sshProfiles;
  for (const auto& value : store.value(QStringLiteral("sshProfiles")).toArray()) {
    if (!value.isObject()) continue;
    auto profile = normalizedSshProfile(value.toObject(), now.toMSecsSinceEpoch());
    if (!profile) continue;
    const auto id = profile->value(QStringLiteral("id")).toString();
    if (profileIds.contains(id)) continue;
    profileIds.insert(id);
    sshProfiles.push_back(*profile);
  }
  store.insert(QStringLiteral("sshProfiles"), sshProfiles);

  QJsonObject repositorySshProfiles;
  const auto requestedBindings = store.value(QStringLiteral("repositorySshProfiles")).toObject();
  for (auto it = requestedBindings.constBegin(); it != requestedBindings.constEnd(); ++it) {
    if (it.value().isString() && profileIds.contains(it.value().toString())) {
      repositorySshProfiles.insert(it.key(), it.value());
    }
  }
  store.insert(QStringLiteral("repositorySshProfiles"), repositorySshProfiles);
  return store;
}

RelayStore::RelayStore(QString filePath)
    : filePath_(filePath.isEmpty() ? AppPaths::storeFile() : QDir::cleanPath(std::move(filePath))) {}

const QString& RelayStore::filePath() const { return filePath_; }

QJsonObject RelayStore::read() const {
  QFile file(filePath_);
  if (!file.open(QIODevice::ReadOnly)) return emptyStoreJson();

  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return emptyStoreJson();
  return normalizeStoreJson(document.object());
}

void RelayStore::write(const QJsonObject& store) const {
  const QFileInfo target(filePath_);
  QDir directory(target.absolutePath());
  if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
    throw StoreError(QStringLiteral("Relay could not create its data directory: %1").arg(directory.path()));
  }

  QSaveFile file(filePath_);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) {
    throw StoreError(QStringLiteral("Relay could not open its metadata store for writing: %1").arg(file.errorString()));
  }
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  const auto bytes = QJsonDocument(normalizeStoreJson(store)).toJson(QJsonDocument::Indented);
  if (file.write(bytes) != bytes.size()) {
    file.cancelWriting();
    throw StoreError(QStringLiteral("Relay could not write its metadata store: %1").arg(file.errorString()));
  }
  if (!file.commit()) {
    throw StoreError(QStringLiteral("Relay could not replace its metadata store: %1").arg(file.errorString()));
  }
  QFile::setPermissions(filePath_, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

}  // namespace relay
