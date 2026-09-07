#include "relay/domain.hpp"

#include <QJsonArray>
#include <QJsonValue>

namespace relay {

QString fileStatusCode(const FileStatus status) {
  switch (status) {
    case FileStatus::added: return QStringLiteral("A");
    case FileStatus::deleted: return QStringLiteral("D");
    case FileStatus::modified: return QStringLiteral("M");
  }
  return QStringLiteral("M");
}

QString fileStatusTone(const FileStatus status) {
  switch (status) {
    case FileStatus::added: return QStringLiteral("added");
    case FileStatus::deleted: return QStringLiteral("deleted");
    case FileStatus::modified: return QStringLiteral("modified");
  }
  return QStringLiteral("modified");
}

FileStatus fileStatusFromGit(const QString& status) {
  if (status.contains(u'?') || status.contains(u'A')) return FileStatus::added;
  if (status.contains(u'D')) return FileStatus::deleted;
  return FileStatus::modified;
}

QString repositoryOrderModeName(const RepositoryOrderMode mode) {
  switch (mode) {
    case RepositoryOrderMode::manual: return QStringLiteral("manual");
    case RepositoryOrderMode::age: return QStringLiteral("age");
    case RepositoryOrderMode::name: return QStringLiteral("name");
    case RepositoryOrderMode::latest: return QStringLiteral("latest");
  }
  return QStringLiteral("manual");
}

RepositoryOrderMode repositoryOrderModeFromName(const QString& value) {
  if (value == QStringLiteral("age")) return RepositoryOrderMode::age;
  if (value == QStringLiteral("name")) return RepositoryOrderMode::name;
  if (value == QStringLiteral("latest")) return RepositoryOrderMode::latest;
  return RepositoryOrderMode::manual;
}

QString sortDirectionName(const SortDirection direction) {
  return direction == SortDirection::descending ? QStringLiteral("desc") : QStringLiteral("asc");
}

SortDirection sortDirectionFromName(const QString& value) {
  return value == QStringLiteral("desc") ? SortDirection::descending : SortDirection::ascending;
}

QDateTime dateTimeFromJson(const QJsonValue& value) {
  return QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
}

QJsonValue dateTimeToJson(const std::optional<QDateTime>& value) {
  if (!value || !value->isValid()) return QJsonValue::Null;
  return value->toUTC().toString(Qt::ISODateWithMs);
}

Account accountFromJson(const QJsonObject& object) {
  Account account;
  account.id = object.value(QStringLiteral("id")).toString();
  const auto githubId = object.value(QStringLiteral("githubId"));
  account.githubId = githubId.isDouble() ? githubId.toInteger() : githubId.toString().toLongLong();
  account.githubIdText = githubId.isDouble() ? QString::number(account.githubId) : githubId.toString();
  account.name = object.value(QStringLiteral("name")).toString();
  account.handle = object.value(QStringLiteral("handle")).toString();
  account.email = object.value(QStringLiteral("email")).toString();
  account.initials = object.value(QStringLiteral("initials")).toString();
  account.tone = object.value(QStringLiteral("tone")).toString();
  account.status = object.value(QStringLiteral("status")).toString();
  account.avatarUrl = object.value(QStringLiteral("avatarUrl")).toString();
  account.authSource = object.value(QStringLiteral("authSource")).toString(QStringLiteral("github-cli"));
  account.tokenSource = object.value(QStringLiteral("tokenSource")).toString(QStringLiteral("credential store"));
  account.active = object.value(QStringLiteral("active")).toBool();
  return account;
}

QJsonObject accountToJson(const Account& account) {
  QJsonObject object{
      {QStringLiteral("id"), account.id},
      {QStringLiteral("name"), account.name},
      {QStringLiteral("handle"), account.handle},
      {QStringLiteral("email"), account.email},
      {QStringLiteral("initials"), account.initials},
      {QStringLiteral("tone"), account.tone},
      {QStringLiteral("status"), account.status},
      {QStringLiteral("avatarUrl"), account.avatarUrl.isEmpty() ? QJsonValue::Null : QJsonValue(account.avatarUrl)},
      {QStringLiteral("authSource"), account.authSource},
      {QStringLiteral("tokenSource"), account.tokenSource},
      {QStringLiteral("active"), account.active},
  };
  bool numeric{};
  const auto rawId = account.githubIdText.isEmpty() ? QString::number(account.githubId) : account.githubIdText;
  const auto number = rawId.toLongLong(&numeric);
  object.insert(QStringLiteral("githubId"), numeric ? QJsonValue(number) : QJsonValue(rawId));
  return object;
}

RepositorySummary repositorySummaryFromJson(const QJsonObject& object) {
  RepositorySummary repository;
  repository.path = object.value(QStringLiteral("path")).toString();
  repository.name = object.value(QStringLiteral("name")).toString();
  repository.owner = object.value(QStringLiteral("owner")).toString();
  repository.branch = object.value(QStringLiteral("branch")).toString();
  repository.changes = object.value(QStringLiteral("changes")).toInteger();
  const auto parseOptional = [&object](const QString& key) -> std::optional<QDateTime> {
    const auto date = dateTimeFromJson(object.value(key));
    return date.isValid() ? std::optional<QDateTime>(date) : std::nullopt;
  };
  repository.lastOpened = parseOptional(QStringLiteral("lastOpened"));
  repository.addedAt = parseOptional(QStringLiteral("addedAt"));
  repository.latestCommit = parseOptional(QStringLiteral("latestCommit"));
  repository.firstCommit = parseOptional(QStringLiteral("firstCommit"));
  return repository;
}

QJsonObject repositorySummaryToJson(const RepositorySummary& repository) {
  return {
      {QStringLiteral("path"), repository.path},
      {QStringLiteral("name"), repository.name},
      {QStringLiteral("owner"), repository.owner},
      {QStringLiteral("branch"), repository.branch},
      {QStringLiteral("changes"), static_cast<qint64>(repository.changes)},
      {QStringLiteral("lastOpened"), dateTimeToJson(repository.lastOpened)},
      {QStringLiteral("addedAt"), dateTimeToJson(repository.addedAt)},
      {QStringLiteral("latestCommit"), dateTimeToJson(repository.latestCommit)},
      {QStringLiteral("firstCommit"), dateTimeToJson(repository.firstCommit)},
  };
}

SshProfile sshProfileFromJson(const QJsonObject& object) {
  SshProfile profile;
  profile.id = object.value(QStringLiteral("id")).toString();
  profile.label = object.value(QStringLiteral("label")).toString();
  profile.host = object.value(QStringLiteral("host")).toString();
  profile.user = object.value(QStringLiteral("user")).toString();
  const auto port = object.value(QStringLiteral("port")).toInt();
  if (port > 0 && port < 65536) profile.port = static_cast<quint16>(port);
  profile.identityFile = object.value(QStringLiteral("identityFile")).toString();
  profile.identitiesOnly = object.value(QStringLiteral("identitiesOnly")).toBool(true);
  return profile;
}

QJsonObject sshProfileToJson(const SshProfile& profile) {
  return {
      {QStringLiteral("id"), profile.id},
      {QStringLiteral("label"), profile.label},
      {QStringLiteral("host"), profile.host},
      {QStringLiteral("user"), profile.user.isEmpty() ? QJsonValue::Null : QJsonValue(profile.user)},
      {QStringLiteral("port"), profile.port ? QJsonValue(*profile.port) : QJsonValue::Null},
      {QStringLiteral("identityFile"), profile.identityFile.isEmpty() ? QJsonValue::Null : QJsonValue(profile.identityFile)},
      {QStringLiteral("identitiesOnly"), profile.identitiesOnly},
  };
}

AppState appStateFromJson(const QJsonObject& object) {
  AppState state;
  for (const auto& value : object.value(QStringLiteral("accounts")).toArray())
    state.accounts.append(accountFromJson(value.toObject()));
  state.activeAccountId = object.value(QStringLiteral("activeAccountId")).toString();
  for (const auto& value : object.value(QStringLiteral("repositories")).toArray())
    state.repositories.append(repositorySummaryFromJson(value.toObject()));
  const auto parseBindings = [](const QJsonObject& bindings) {
    QHash<QString, QString> result;
    for (auto iterator = bindings.begin(); iterator != bindings.end(); ++iterator)
      result.insert(iterator.key(), iterator.value().toString());
    return result;
  };
  state.repositoryAccounts = parseBindings(object.value(QStringLiteral("repositoryAccounts")).toObject());
  const auto order = object.value(QStringLiteral("repositoryOrder")).toObject();
  state.repositoryOrder = {repositoryOrderModeFromName(order.value(QStringLiteral("mode")).toString()),
                           sortDirectionFromName(order.value(QStringLiteral("direction")).toString())};
  for (const auto& value : object.value(QStringLiteral("manualOrder")).toArray())
    state.manualOrder.append(value.toString());
  for (const auto& value : object.value(QStringLiteral("sshProfiles")).toArray())
    state.sshProfiles.append(sshProfileFromJson(value.toObject()));
  state.repositorySshProfiles = parseBindings(object.value(QStringLiteral("repositorySshProfiles")).toObject());
  const auto preferences = object.value(QStringLiteral("preferences")).toObject();
  state.preferences.refreshOnFocus = preferences.value(QStringLiteral("refreshOnFocus")).toBool(true);
  state.preferences.diffFontSize = qBound(10, preferences.value(QStringLiteral("diffFontSize")).toInt(12), 24);
  state.preferences.commitName = preferences.value(QStringLiteral("commitName")).toString();
  state.preferences.commitEmail = preferences.value(QStringLiteral("commitEmail")).toString();
  state.preferences.graphHistory = preferences.value(QStringLiteral("graphHistory")).toBool();
  return state;
}

QJsonObject mergeAppStateIntoJson(const AppState& state, QJsonObject base) {
  QJsonArray accounts;
  for (const auto& account : state.accounts) accounts.append(accountToJson(account));
  QJsonArray repositories;
  for (const auto& repository : state.repositories) repositories.append(repositorySummaryToJson(repository));
  const auto bindings = [](const QHash<QString, QString>& values) {
    QJsonObject result;
    for (auto iterator = values.cbegin(); iterator != values.cend(); ++iterator)
      result.insert(iterator.key(), iterator.value());
    return result;
  };
  QJsonArray manualOrder;
  for (const auto& path : state.manualOrder) manualOrder.append(path);
  QJsonArray profiles;
  for (const auto& profile : state.sshProfiles) profiles.append(sshProfileToJson(profile));
  base.insert(QStringLiteral("accounts"), accounts);
  base.insert(QStringLiteral("activeAccountId"), state.activeAccountId.isEmpty() ? QJsonValue::Null : QJsonValue(state.activeAccountId));
  base.insert(QStringLiteral("repositories"), repositories);
  base.insert(QStringLiteral("selectedRepositoryPath"), QJsonValue::Null);
  base.insert(QStringLiteral("repositoryAccounts"), bindings(state.repositoryAccounts));
  base.insert(QStringLiteral("repositoryOrder"), QJsonObject{
      {QStringLiteral("mode"), repositoryOrderModeName(state.repositoryOrder.mode)},
      {QStringLiteral("direction"), sortDirectionName(state.repositoryOrder.direction)}});
  base.insert(QStringLiteral("manualOrder"), manualOrder);
  base.insert(QStringLiteral("sshProfiles"), profiles);
  base.insert(QStringLiteral("repositorySshProfiles"), bindings(state.repositorySshProfiles));
  auto preferences = base.value(QStringLiteral("preferences")).toObject();
  preferences.insert(QStringLiteral("refreshOnFocus"), state.preferences.refreshOnFocus);
  preferences.insert(QStringLiteral("diffFontSize"), state.preferences.diffFontSize);
  preferences.insert(QStringLiteral("commitName"), state.preferences.commitName);
  preferences.insert(QStringLiteral("commitEmail"), state.preferences.commitEmail);
  preferences.insert(QStringLiteral("graphHistory"), state.preferences.graphHistory);
  base.insert(QStringLiteral("preferences"), preferences);
  return base;
}

}  // namespace relay
