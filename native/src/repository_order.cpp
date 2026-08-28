#include "relay/repository_order.hpp"

#include <QCollator>
#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

#include <algorithm>
#include <limits>
#include <utility>

namespace relay {
namespace {

QString isoTimestamp(const QDateTime& value) {
  return value.toUTC().toString(Qt::ISODateWithMs);
}

bool hasTimestamp(const QJsonValue& value) {
  return value.isString() && !value.toString().isEmpty();
}

QCollator repositoryCollator() {
  QCollator collator;
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  collator.setNumericMode(true);
  return collator;
}

int compareByName(
    const RepositorySummary& left, const RepositorySummary& right, const QCollator& collator) {
  const auto nameComparison = collator.compare(left.name, right.name);
  return nameComparison != 0 ? nameComparison : collator.compare(left.path, right.path);
}

std::optional<qint64> timestamp(const std::optional<QDateTime>& value) {
  if (!value || !value->isValid()) return std::nullopt;
  return value->toMSecsSinceEpoch();
}

}  // namespace

QJsonObject normalizeRepositoryOrdering(QJsonObject store, const QDateTime& now) {
  const auto requested = store.value(QStringLiteral("repositoryOrder")).toObject();
  const auto requestedMode = requested.value(QStringLiteral("mode")).toString();
  const auto requestedDirection = requested.value(QStringLiteral("direction")).toString();
  const auto validMode = requestedMode == QStringLiteral("manual") || requestedMode == QStringLiteral("age")
      || requestedMode == QStringLiteral("name") || requestedMode == QStringLiteral("latest");
  const auto validDirection =
      requestedDirection == QStringLiteral("asc") || requestedDirection == QStringLiteral("desc");
  store.insert(
      QStringLiteral("repositoryOrder"),
      QJsonObject{
          {QStringLiteral("mode"), validMode ? requestedMode : QStringLiteral("manual")},
          {QStringLiteral("direction"), validDirection ? requestedDirection : QStringLiteral("asc")},
      });

  QJsonArray repositories;
  QSet<QString> known;
  for (const auto& value : store.value(QStringLiteral("repositories")).toArray()) {
    if (!value.isObject()) continue;
    auto repository = value.toObject();
    const auto path = repository.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) continue;

    if (!hasTimestamp(repository.value(QStringLiteral("addedAt")))) {
      const auto lastOpened = repository.value(QStringLiteral("lastOpened"));
      repository.insert(
          QStringLiteral("addedAt"), hasTimestamp(lastOpened) ? lastOpened : QJsonValue(isoTimestamp(now)));
    }
    if (!repository.contains(QStringLiteral("latestCommit"))) {
      repository.insert(QStringLiteral("latestCommit"), QJsonValue::Null);
    }
    if (!repository.contains(QStringLiteral("firstCommit"))) {
      repository.insert(QStringLiteral("firstCommit"), QJsonValue::Null);
    }
    repositories.push_back(repository);
    known.insert(path);
  }
  store.insert(QStringLiteral("repositories"), repositories);

  QSet<QString> seen;
  QJsonArray manualOrder;
  for (const auto& value : store.value(QStringLiteral("manualOrder")).toArray()) {
    if (!value.isString()) continue;
    const auto path = value.toString();
    if (!known.contains(path) || seen.contains(path)) continue;
    seen.insert(path);
    manualOrder.push_back(path);
  }
  for (const auto& value : repositories) {
    const auto path = value.toObject().value(QStringLiteral("path")).toString();
    if (seen.contains(path)) continue;
    seen.insert(path);
    manualOrder.push_back(path);
  }
  store.insert(QStringLiteral("manualOrder"), manualOrder);
  return store;
}

QStringList repositoriesNeedingMetadataBackfill(const QJsonObject& store) {
  QStringList result;
  for (const auto& value : store.value(QStringLiteral("repositories")).toArray()) {
    const auto repository = value.toObject();
    const auto firstCommit = repository.value(QStringLiteral("firstCommit"));
    const auto isFalseSentinel = firstCommit.isBool() && !firstCommit.toBool();
    if (!hasTimestamp(firstCommit) && !isFalseSentinel) {
      const auto path = repository.value(QStringLiteral("path")).toString();
      if (!path.isEmpty()) result.push_back(path);
    }
  }
  return result;
}

QJsonObject applyManualRepositoryOrder(
    QJsonObject store, const QStringList& repositoryPaths, const QDateTime& now) {
  QSet<QString> known;
  for (const auto& value : store.value(QStringLiteral("repositories")).toArray()) {
    const auto path = value.toObject().value(QStringLiteral("path")).toString();
    if (!path.isEmpty()) known.insert(path);
  }

  QSet<QString> seen;
  QJsonArray manualOrder;
  for (const auto& path : repositoryPaths) {
    if (!known.contains(path) || seen.contains(path)) continue;
    seen.insert(path);
    manualOrder.push_back(path);
  }
  store.insert(QStringLiteral("manualOrder"), manualOrder);
  return normalizeRepositoryOrdering(std::move(store), now);
}

QList<RepositorySummary> sortRepositories(
    const QList<RepositorySummary>& repositories, const RepositoryOrder order, const QStringList& manualOrder) {
  auto sorted = repositories;
  const auto collator = repositoryCollator();
  const auto sign = order.direction == SortDirection::ascending ? 1 : -1;

  if (order.mode == RepositoryOrderMode::manual) {
    QHash<QString, qsizetype> positions;
    for (qsizetype index = 0; index < manualOrder.size(); ++index) positions.tryInsert(manualOrder[index], index);
    std::stable_sort(sorted.begin(), sorted.end(), [&](const auto& left, const auto& right) {
      const auto leftPosition = positions.value(left.path, std::numeric_limits<qsizetype>::max());
      const auto rightPosition = positions.value(right.path, std::numeric_limits<qsizetype>::max());
      if (leftPosition != rightPosition) return leftPosition < rightPosition;
      return compareByName(left, right, collator) < 0;
    });
    return sorted;
  }

  if (order.mode == RepositoryOrderMode::name) {
    std::stable_sort(sorted.begin(), sorted.end(), [&](const auto& left, const auto& right) {
      const auto byName = collator.compare(left.name, right.name);
      if (byName != 0) return byName * sign < 0;
      return compareByName(left, right, collator) < 0;
    });
    return sorted;
  }

  const auto dateFor = [order](const RepositorySummary& repository) {
    return order.mode == RepositoryOrderMode::age ? timestamp(repository.firstCommit)
                                                  : timestamp(repository.latestCommit);
  };
  std::stable_sort(sorted.begin(), sorted.end(), [&](const auto& left, const auto& right) {
    const auto leftDate = dateFor(left);
    const auto rightDate = dateFor(right);
    if (leftDate.has_value() != rightDate.has_value()) return leftDate.has_value();
    if (leftDate && rightDate && *leftDate != *rightDate) return (*leftDate - *rightDate) * sign < 0;
    return compareByName(left, right, collator) < 0;
  });
  return sorted;
}

}  // namespace relay
