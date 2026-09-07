#include "relay/list_models.hpp"

#include <QSignalSpy>
#include <QMimeData>
#include <QTest>

#include <memory>

namespace {

QDateTime date(const QString& value) {
  return QDateTime::fromString(value, Qt::ISODate);
}

relay::RepositorySummary repository(QString path, QString name, QString owner,
                                    std::optional<QDateTime> firstCommit = std::nullopt,
                                    std::optional<QDateTime> latestCommit = std::nullopt) {
  return relay::RepositorySummary{
      .path = std::move(path),
      .name = std::move(name),
      .owner = std::move(owner),
      .branch = QStringLiteral("main"),
      .changes = 2,
      .latestCommit = std::move(latestCommit),
      .firstCommit = std::move(firstCommit),
  };
}

relay::ChangedFile file(QString path, relay::FileStatus status = relay::FileStatus::modified,
                        qsizetype added = 1, qsizetype removed = 2, bool binary = false) {
  const qsizetype slash = path.lastIndexOf(QChar{u'/'});
  return relay::ChangedFile{
      .path = path,
      .name = slash >= 0 ? path.mid(slash + 1) : path,
      .directory = slash >= 0 ? path.left(slash) : QString{},
      .status = status,
      .added = added,
      .removed = removed,
      .binary = binary,
  };
}

relay::HistoryCommit commit(QString fullHash, QString title, QString author,
                            QString email, QDateTime when) {
  relay::HistoryCommit result;
  result.fullHash = std::move(fullHash);
  result.hash = result.fullHash.left(7);
  result.title = std::move(title);
  result.author = std::move(author);
  result.email = std::move(email);
  result.date = std::move(when);
  return result;
}

QString repositoryPathAt(const relay::RepositoryListModel& model, int row) {
  return model.index(row).data(relay::RepositoryListModel::pathRole).toString();
}

}  // namespace

class ListModelsTest final : public QObject {
  Q_OBJECT

 private slots:
  void graphPreservesEdgesAcrossPagesAndHidesOnFilter() {
    const auto item = [](QString hash, QStringList parents) {
      relay::HistoryCommit commit;
      commit.fullHash = hash;
      commit.title = hash;
      commit.parents = parents;
      return commit;
    };
    relay::HistoryCommitListModel model;
    model.setGraphEnabled(true);
    model.resetHistory({item(QStringLiteral("M"), {QStringLiteral("A"), QStringLiteral("B")}),
                        item(QStringLiteral("A"), {QStringLiteral("R")})});
    QCOMPARE(model.graphRowAt(0)->parents, QList<int>({0, 1}));
    QCOMPARE(model.graphRowAt(1)->lane, 0);
    QCOMPARE(model.graphRowAt(1)->passing.size(), 1);
    static_cast<void>(model.appendCommits({item(QStringLiteral("B"), {QStringLiteral("R")}),
                                         item(QStringLiteral("R"), {})}));
    QCOMPARE(model.graphRowAt(0)->parents, QList<int>({0, 1}));
    QCOMPARE(model.graphRowAt(2)->lane, 1);
    QCOMPARE(model.graphRowAt(2)->parents, QList<int>{0});
    QCOMPARE(model.graphRowAt(3)->lane, 0);
    QVERIFY(model.graphRowAt(3)->parents.isEmpty());
    model.setSearch(QStringLiteral("B"));
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(!model.graphRowAt(0));
    model.setSearch({});
    QVERIFY(model.graphRowAt(0));
  }

  void repositoryManualOrderAndNaturalTieBreak() {
    relay::RepositoryListModel model;
    model.setRepositories({
        repository(QStringLiteral("/repo10"), QStringLiteral("repo10"), QStringLiteral("owner")),
        repository(QStringLiteral("/repo2"), QStringLiteral("repo2"), QStringLiteral("owner")),
        repository(QStringLiteral("/placed"), QStringLiteral("placed"), QStringLiteral("owner")),
    });
    model.setOrder({relay::RepositoryOrderMode::manual, relay::SortDirection::ascending},
                   {QStringLiteral("/placed")});

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(repositoryPathAt(model, 0), QStringLiteral("/placed"));
    QCOMPARE(repositoryPathAt(model, 1), QStringLiteral("/repo2"));
    QCOMPARE(repositoryPathAt(model, 2), QStringLiteral("/repo10"));
    QVERIFY(model.canManuallyReorder());

    QVERIFY(model.moveRepository(QStringLiteral("/repo10"), QStringLiteral("/placed")));
    QCOMPARE(repositoryPathAt(model, 0), QStringLiteral("/repo10"));
    QCOMPARE(model.manualOrder(),
             QStringList({QStringLiteral("/repo10"), QStringLiteral("/placed"),
                          QStringLiteral("/repo2")}));
  }

  void repositoryFilterAndSortSemantics() {
    relay::RepositoryListModel model;
    const QDateTime oldDate = date(QStringLiteral("2020-01-01T00:00:00Z"));
    const QDateTime newDate = date(QStringLiteral("2025-01-01T00:00:00Z"));
    model.setRepositories({
        repository(QStringLiteral("/work/Zeta"), QStringLiteral("Zeta"),
                   QStringLiteral("Acme"), oldDate, newDate),
        repository(QStringLiteral("/personal/alpha"), QStringLiteral("alpha"),
                   QStringLiteral("Me"), newDate, oldDate),
        repository(QStringLiteral("/empty"), QStringLiteral("empty"),
                   QStringLiteral("Me")),
    });

    model.setOrder({relay::RepositoryOrderMode::age, relay::SortDirection::descending});
    QCOMPARE(repositoryPathAt(model, 0), QStringLiteral("/personal/alpha"));
    QCOMPARE(repositoryPathAt(model, 1), QStringLiteral("/work/Zeta"));
    QCOMPARE(repositoryPathAt(model, 2), QStringLiteral("/empty"));

    model.setOrder({relay::RepositoryOrderMode::latest, relay::SortDirection::ascending});
    QCOMPARE(repositoryPathAt(model, 0), QStringLiteral("/personal/alpha"));
    QCOMPARE(repositoryPathAt(model, 1), QStringLiteral("/work/Zeta"));
    QCOMPARE(repositoryPathAt(model, 2), QStringLiteral("/empty"));

    model.setFilter(QStringLiteral("ACME/zeta"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(repositoryPathAt(model, 0), QStringLiteral("/work/Zeta"));
    QVERIFY(!model.canManuallyReorder());
  }

  void repositoryDragDropPublishesManualOrder() {
    relay::RepositoryListModel model;
    model.setRepositories({
        repository(QStringLiteral("/a"), QStringLiteral("a"), QStringLiteral("owner")),
        repository(QStringLiteral("/b"), QStringLiteral("b"), QStringLiteral("owner")),
        repository(QStringLiteral("/c"), QStringLiteral("c"), QStringLiteral("owner")),
    });
    model.setOrder({relay::RepositoryOrderMode::manual, relay::SortDirection::ascending},
                   {QStringLiteral("/a"), QStringLiteral("/b"), QStringLiteral("/c")});
    QSignalSpy changed{&model, &relay::RepositoryListModel::manualOrderChanged};
    std::unique_ptr<QMimeData> data{model.mimeData({model.index(0)})};
    QVERIFY(model.dropMimeData(data.get(), Qt::MoveAction, 2, 0, {}));
    QCOMPARE(model.manualOrder(),
             QStringList({QStringLiteral("/b"), QStringLiteral("/a"), QStringLiteral("/c")}));
    QCOMPARE(changed.count(), 1);
    QVERIFY(model.flags({}).testFlag(Qt::ItemIsDropEnabled));

    model.setFilter(QStringLiteral("owner"));
    QVERIFY(!model.dropMimeData(data.get(), Qt::MoveAction, 1, 0, {}));
  }

  void descendingNameSortKeepsStableAscendingPathTieBreak() {
    relay::RepositoryListModel model;
    model.setRepositories({
        repository(QStringLiteral("/same-b"), QStringLiteral("same"), QStringLiteral("owner")),
        repository(QStringLiteral("/zulu"), QStringLiteral("zulu"), QStringLiteral("owner")),
        repository(QStringLiteral("/same-a"), QStringLiteral("same"), QStringLiteral("owner")),
    });
    model.setOrder({relay::RepositoryOrderMode::name, relay::SortDirection::descending});

    QCOMPARE(repositoryPathAt(model, 0), QStringLiteral("/zulu"));
    QCOMPARE(repositoryPathAt(model, 1), QStringLiteral("/same-a"));
    QCOMPARE(repositoryPathAt(model, 2), QStringLiteral("/same-b"));
  }

  void repositoryRolesIncludePinnedAccountAndAccessibility() {
    relay::RepositoryListModel model;
    model.setRepositories({repository(QStringLiteral("/relay"), QStringLiteral("relay"),
                                      QStringLiteral("ka-capek"))});
    QSignalSpy changed{&model, &QAbstractItemModel::dataChanged};
    model.setPinnedAccountIds({{QStringLiteral("/relay"), QStringLiteral("account-1")}});

    const QModelIndex index = model.index(0);
    QCOMPARE(index.data(relay::RepositoryListModel::pinnedAccountIdRole).toString(),
             QStringLiteral("account-1"));
    QCOMPARE(index.data(relay::RepositoryListModel::changeCountRole).toLongLong(), 2);
    QVERIFY(index.data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("ka-capek/relay")));
    QCOMPARE(changed.count(), 1);
  }

  void changedFilesTrackCheckStateByPath() {
    relay::ChangedFileListModel model;
    model.setFiles({file(QStringLiteral("src/a.cpp")), file(QStringLiteral("src/b.cpp"))});
    QCOMPARE(model.aggregateCheckState(), Qt::Checked);
    QCOMPARE(model.checkedCount(), 2);

    QSignalSpy changed{&model, &QAbstractItemModel::dataChanged};
    QVERIFY(model.setData(model.index(0), Qt::Unchecked, Qt::CheckStateRole));
    QCOMPARE(model.aggregateCheckState(), Qt::PartiallyChecked);
    QCOMPARE(model.checkedPaths(), QStringList({QStringLiteral("src/b.cpp")}));
    QCOMPARE(changed.count(), 1);

    model.setFiles({file(QStringLiteral("src/b.cpp")), file(QStringLiteral("src/c.cpp"))},
                   relay::FileCheckPolicy::preserve);
    QCOMPARE(model.checkedPaths(), QStringList({QStringLiteral("src/b.cpp")}));
    model.setAllChecked(false);
    QCOMPARE(model.aggregateCheckState(), Qt::Unchecked);
  }

  void changedFileRolesAreDelegateReady() {
    relay::ChangedFileListModel model;
    model.setFiles({file(QStringLiteral("assets/logo.png"), relay::FileStatus::added,
                         0, 0, true)});
    const QModelIndex index = model.index(0);
    QCOMPARE(index.data(relay::ChangedFileListModel::nameRole).toString(),
             QStringLiteral("logo.png"));
    QCOMPARE(index.data(relay::ChangedFileListModel::directoryRole).toString(),
             QStringLiteral("assets"));
    QCOMPARE(index.data(relay::ChangedFileListModel::statusCodeRole).toString(),
             QStringLiteral("A"));
    QVERIFY(index.data(relay::ChangedFileListModel::binaryRole).toBool());
    QVERIFY(index.data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("included")));
    QVERIFY(model.flags(index).testFlag(Qt::ItemIsUserCheckable));
  }

  void historyDeduplicatesAndAppendsProgressively() {
    relay::HistoryCommitListModel model;
    const QDateTime firstDay = date(QStringLiteral("2026-08-26T12:00:00Z"));
    const QDateTime secondDay = date(QStringLiteral("2026-08-25T12:00:00Z"));
    const auto first = commit(QStringLiteral("aaaaaaaaaaaaaaaa"), QStringLiteral("First"),
                              QStringLiteral("Alice"), QStringLiteral("alice@example.com"),
                              firstDay);
    const auto second = commit(QStringLiteral("bbbbbbbbbbbbbbbb"), QStringLiteral("Second"),
                               QStringLiteral("Bob"), QStringLiteral("bob@example.com"),
                               firstDay);
    const auto third = commit(QStringLiteral("cccccccccccccccc"), QStringLiteral("Third"),
                              QStringLiteral("Carol"), QStringLiteral("carol@example.com"),
                              secondDay);

    model.resetHistory({first, first}, QStringLiteral("anchor"), false);
    QCOMPARE(model.rowCount(), 1);
    QSignalSpy inserted{&model, &QAbstractItemModel::rowsInserted};
    QCOMPARE(model.appendCommits({first, second, second, third}), 2);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(model.anchor(), QStringLiteral("anchor"));
    QVERIFY(!model.endOfHistory());
    QVERIFY(model.index(0).data(relay::HistoryCommitListModel::startsDayGroupRole).toBool());
    QVERIFY(!model.index(1).data(relay::HistoryCommitListModel::startsDayGroupRole).toBool());
    QVERIFY(model.index(2).data(relay::HistoryCommitListModel::startsDayGroupRole).toBool());
  }

  void historySearchCoversMessageAuthorEmailAndHash() {
    relay::HistoryCommitListModel model;
    model.resetHistory({
        commit(QStringLiteral("aaaaaaaa12345678"), QStringLiteral("Fix parser"),
               QStringLiteral("Alice"), QStringLiteral("alice@example.com"),
               date(QStringLiteral("2026-08-26T12:00:00Z"))),
        commit(QStringLiteral("bbbbbbbb12345678"), QStringLiteral("Write docs"),
               QStringLiteral("Bob"), QStringLiteral("bob@work.test"),
               date(QStringLiteral("2026-08-25T12:00:00Z"))),
    });

    for (const QString& query : {QStringLiteral("parser"), QStringLiteral("ALICE"),
                                 QStringLiteral("work.test"), QStringLiteral("bbbbbbbb")}) {
      model.setSearch(query);
      QCOMPARE(model.rowCount(), 1);
    }
    model.setSearch(QStringLiteral("missing"));
    QCOMPARE(model.rowCount(), 0);

    const auto matching = commit(QStringLiteral("dddddddd12345678"),
                                 QStringLiteral("Missing link"), QStringLiteral("Dana"),
                                 QStringLiteral("dana@example.com"),
                                 date(QStringLiteral("2026-08-24T12:00:00Z")));
    QCOMPARE(model.appendCommits({matching}), 1);
    QCOMPARE(model.rowCount(), 1);
  }

  void historyPageCarriesAnchorAndEndState() {
    relay::HistoryCommitListModel model;
    relay::HistoryPage page;
    page.anchor = QStringLiteral("head-hash");
    page.endOfHistory = true;
    page.commits = {commit(QStringLiteral("aaaaaaaa12345678"), QStringLiteral("Only"),
                           QStringLiteral("Alice"), QStringLiteral("alice@example.com"),
                           date(QStringLiteral("2026-08-26T12:00:00Z")))};
    model.resetPage(page);

    QCOMPARE(model.anchor(), QStringLiteral("head-hash"));
    QVERIFY(model.endOfHistory());
    QVERIFY(model.index(0).data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("Alice")));
  }

  void commitFilesExposeBinaryAndDeltaRolesWithoutChecks() {
    relay::CommitFileListModel model;
    model.setFiles({file(QStringLiteral("dist/archive.bin"), relay::FileStatus::deleted,
                         4, 9, true)});
    const QModelIndex index = model.index(0);
    QCOMPARE(index.data(relay::CommitFileListModel::statusCodeRole).toString(),
             QStringLiteral("D"));
    QCOMPARE(index.data(relay::CommitFileListModel::addedCountRole).toLongLong(), 4);
    QCOMPARE(index.data(relay::CommitFileListModel::removedCountRole).toLongLong(), 9);
    QVERIFY(index.data(relay::CommitFileListModel::binaryRole).toBool());
    QVERIFY(index.data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("binary")));
    QVERIFY(!model.flags(index).testFlag(Qt::ItemIsUserCheckable));
    QVERIFY(!index.data(Qt::CheckStateRole).isValid());
  }
};

QTEST_APPLESS_MAIN(ListModelsTest)
#include "tst_list_models.moc"
