#include "relay/domain.hpp"

#include <QTest>

class DomainTest final : public QObject {
  Q_OBJECT

 private slots:
  void mapsGitStatuses() {
    QCOMPARE(relay::fileStatusFromGit(QStringLiteral("??")), relay::FileStatus::added);
    QCOMPARE(relay::fileStatusFromGit(QStringLiteral(" D")), relay::FileStatus::deleted);
    QCOMPARE(relay::fileStatusFromGit(QStringLiteral(" M")), relay::FileStatus::modified);
  }

  void normalizesOrderingNames() {
    QCOMPARE(relay::repositoryOrderModeFromName(QStringLiteral("latest")), relay::RepositoryOrderMode::latest);
    QCOMPARE(relay::repositoryOrderModeFromName(QStringLiteral("invalid")), relay::RepositoryOrderMode::manual);
    QCOMPARE(relay::sortDirectionFromName(QStringLiteral("desc")), relay::SortDirection::descending);
  }

  void roundTripsCompatibleState() {
    relay::AppState state;
    relay::Account account;
    account.id = QStringLiteral("github-octo");
    account.githubIdText = QStringLiteral("octo");
    account.handle = QStringLiteral("octo");
    account.name = QStringLiteral("Octo Cat");
    state.accounts.append(account);
    state.activeAccountId = account.id;
    relay::RepositorySummary repository;
    repository.path = QStringLiteral("/tmp/repo");
    repository.name = QStringLiteral("repo");
    state.repositories.append(repository);
    state.manualOrder.append(repository.path);
    const auto json = relay::mergeAppStateIntoJson(state, {{QStringLiteral("futureField"), 7}});
    QCOMPARE(json.value(QStringLiteral("selectedRepositoryPath")), QJsonValue::Null);
    QCOMPARE(json.value(QStringLiteral("futureField")).toInt(), 7);
    const auto restored = relay::appStateFromJson(json);
    QCOMPARE(restored.accounts.front().githubIdText, QStringLiteral("octo"));
    QCOMPARE(restored.repositories.front().path, QStringLiteral("/tmp/repo"));
  }
};

QTEST_APPLESS_MAIN(DomainTest)
#include "tst_domain.moc"
