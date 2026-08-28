#include "relay/repository_order.hpp"

#include <QJsonArray>
#include <QTest>

namespace {

relay::RepositorySummary repository(
    const QString& path, const QString& name, const char* firstCommit = nullptr,
    const char* latestCommit = nullptr) {
  relay::RepositorySummary result;
  result.path = path;
  result.name = name;
  if (firstCommit) result.firstCommit = QDateTime::fromString(QString::fromLatin1(firstCommit), Qt::ISODate);
  if (latestCommit) result.latestCommit = QDateTime::fromString(QString::fromLatin1(latestCommit), Qt::ISODate);
  return result;
}

QStringList paths(const QList<relay::RepositorySummary>& repositories) {
  QStringList result;
  for (const auto& repository : repositories) result.push_back(repository.path);
  return result;
}

}  // namespace

class RepositoryOrderTest final : public QObject {
  Q_OBJECT

 private slots:
  void upgradesLegacyOrderingWithoutReshuffling() {
    const QJsonObject legacy{
        {QStringLiteral("repositories"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("path"), QStringLiteral("/b")},
                 {QStringLiteral("lastOpened"), QStringLiteral("2026-01-02T00:00:00.000Z")},
             },
             QJsonObject{
                 {QStringLiteral("path"), QStringLiteral("/a")},
                 {QStringLiteral("lastOpened"), QStringLiteral("2026-01-01T00:00:00.000Z")},
             },
         }},
        {QStringLiteral("manualOrder"),
         QJsonArray{QStringLiteral("/b"), QStringLiteral("/removed"), QStringLiteral("/b")}},
        {QStringLiteral("repositoryOrder"),
         QJsonObject{
             {QStringLiteral("mode"), QStringLiteral("invalid")},
             {QStringLiteral("direction"), QStringLiteral("sideways")},
         }},
    };

    const auto normalized = relay::normalizeRepositoryOrdering(
        legacy, QDateTime::fromString(QStringLiteral("2026-02-01T00:00:00Z"), Qt::ISODate));
    QCOMPARE(
        normalized.value(QStringLiteral("manualOrder")).toArray(),
        QJsonArray({QStringLiteral("/b"), QStringLiteral("/a")}));
    QCOMPARE(
        normalized.value(QStringLiteral("repositoryOrder")).toObject().value(QStringLiteral("mode")).toString(),
        QStringLiteral("manual"));
    const auto repositories = normalized.value(QStringLiteral("repositories")).toArray();
    QCOMPARE(
        repositories[0].toObject().value(QStringLiteral("addedAt")).toString(),
        QStringLiteral("2026-01-02T00:00:00.000Z"));
    QVERIFY(repositories[0].toObject().value(QStringLiteral("latestCommit")).isNull());
    QCOMPARE(relay::repositoriesNeedingMetadataBackfill(normalized), QStringList({QStringLiteral("/b"), QStringLiteral("/a")}));
  }

  void appliesManualOrderWithoutDroppingRepositories() {
    const QJsonObject store{
        {QStringLiteral("repositories"),
         QJsonArray{
             QJsonObject{{QStringLiteral("path"), QStringLiteral("/a")}},
             QJsonObject{{QStringLiteral("path"), QStringLiteral("/b")}},
             QJsonObject{{QStringLiteral("path"), QStringLiteral("/c")}},
         }},
    };
    const auto ordered = relay::applyManualRepositoryOrder(
        store, {QStringLiteral("/b"), QStringLiteral("/ghost"), QStringLiteral("/a"), QStringLiteral("/b")});
    QCOMPARE(
        ordered.value(QStringLiteral("manualOrder")).toArray(),
        QJsonArray({QStringLiteral("/b"), QStringLiteral("/a"), QStringLiteral("/c")}));
  }

  void sortsAllSidebarModesWithNullDatesLast() {
    const QList repositories{
        repository(QStringLiteral("/ten"), QStringLiteral("Repo 10"), "2020-01-01T00:00:00Z", "2026-02-01T00:00:00Z"),
        repository(QStringLiteral("/two"), QStringLiteral("repo 2"), "2021-01-01T00:00:00Z", "2026-01-01T00:00:00Z"),
        repository(QStringLiteral("/empty"), QStringLiteral("Empty")),
    };
    QCOMPARE(
        paths(relay::sortRepositories(
            repositories, {relay::RepositoryOrderMode::manual, relay::SortDirection::descending},
            {QStringLiteral("/two"), QStringLiteral("/ten")})),
        QStringList({QStringLiteral("/two"), QStringLiteral("/ten"), QStringLiteral("/empty")}));
    QCOMPARE(
        paths(relay::sortRepositories(
            repositories, {relay::RepositoryOrderMode::name, relay::SortDirection::ascending}, {})),
        QStringList({QStringLiteral("/empty"), QStringLiteral("/two"), QStringLiteral("/ten")}));
    QCOMPARE(
        paths(relay::sortRepositories(
            repositories, {relay::RepositoryOrderMode::age, relay::SortDirection::descending}, {})),
        QStringList({QStringLiteral("/two"), QStringLiteral("/ten"), QStringLiteral("/empty")}));
    QCOMPARE(
        paths(relay::sortRepositories(
            repositories, {relay::RepositoryOrderMode::latest, relay::SortDirection::ascending}, {})),
        QStringList({QStringLiteral("/two"), QStringLiteral("/ten"), QStringLiteral("/empty")}));
  }
};

QTEST_APPLESS_MAIN(RepositoryOrderTest)
#include "tst_repository_order.moc"
