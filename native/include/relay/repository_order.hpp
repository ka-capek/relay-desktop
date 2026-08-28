#pragma once

#include "relay/domain.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QStringList>

namespace relay {

[[nodiscard]] QJsonObject normalizeRepositoryOrdering(
    QJsonObject store, const QDateTime& now = QDateTime::currentDateTimeUtc());
[[nodiscard]] QStringList repositoriesNeedingMetadataBackfill(const QJsonObject& store);
[[nodiscard]] QJsonObject applyManualRepositoryOrder(
    QJsonObject store, const QStringList& repositoryPaths,
    const QDateTime& now = QDateTime::currentDateTimeUtc());

[[nodiscard]] QList<RepositorySummary> sortRepositories(
    const QList<RepositorySummary>& repositories, RepositoryOrder order, const QStringList& manualOrder);

}  // namespace relay
