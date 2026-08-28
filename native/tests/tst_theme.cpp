#include "relay/item_delegates.hpp"
#include "relay/list_models.hpp"
#include "relay/theme.hpp"

#include <QTest>

class ThemeTest final : public QObject {
  Q_OBJECT

 private slots:
  void paletteUsesRelaySemanticColors() {
    const QPalette palette = relay::theme::palette();
    QCOMPARE(palette.color(QPalette::WindowText), QColor(0x19, 0x20, 0x1e));
    QCOMPARE(palette.color(QPalette::Base), QColor(0xff, 0xff, 0xff));
    QCOMPARE(palette.color(QPalette::Highlight), QColor(0xe5, 0xf1, 0xeb));
    QCOMPARE(palette.color(QPalette::HighlightedText), QColor(0x19, 0x20, 0x1e));
    QCOMPARE(palette.color(QPalette::Link), QColor(0x17, 0x6b, 0x4b));
  }

  void exposesCompleteColorTokenSet() {
    const relay::theme::Colors& colors = relay::theme::colors();
    QCOMPARE(colors.ink.name(), QStringLiteral("#19201e"));
    QCOMPARE(colors.muted.name(), QStringLiteral("#69736f"));
    QCOMPARE(colors.line.name(), QStringLiteral("#dfe3df"));
    QCOMPARE(colors.lineSoft.name(), QStringLiteral("#e9ece9"));
    QCOMPARE(colors.panel.name(), QStringLiteral("#ffffff"));
    QCOMPARE(colors.canvas.name(), QStringLiteral("#f6f7f4"));
    QCOMPARE(colors.soft.name(), QStringLiteral("#f0f2ee"));
    QCOMPARE(colors.green.name(), QStringLiteral("#176b4b"));
    QCOMPARE(colors.greenDeep.name(), QStringLiteral("#0f5238"));
    QCOMPARE(colors.greenWash.name(), QStringLiteral("#e5f1eb"));
    QCOMPARE(colors.orange.name(), QStringLiteral("#d2763c"));
    QCOMPARE(colors.outerBackground.name(), QStringLiteral("#e9ece7"));
    QCOMPARE(colors.avatarCoral.name(), QStringLiteral("#bb6248"));
    QCOMPARE(colors.avatarViolet.name(), QStringLiteral("#725a9f"));
    QCOMPARE(colors.avatarBlue.name(), QStringLiteral("#46779e"));
  }

  void styleSheetContainsTypographyScale() {
    const QString sheet = relay::theme::styleSheet();
    for (const QString& size : {QStringLiteral("font-size: 10px"),
                                QStringLiteral("font-size: 11px"),
                                QStringLiteral("font-size: 12px"),
                                QStringLiteral("font-size: 13px"),
                                QStringLiteral("font-size: 15px"),
                                QStringLiteral("font-size: 17px"),
                                QStringLiteral("font-size: 20px")}) {
      QVERIFY2(sheet.contains(size), qPrintable(size));
    }
    QCOMPARE(relay::theme::Metrics::textBadge, 10);
    QCOMPARE(relay::theme::Metrics::textBody, 13);
    QCOMPARE(relay::theme::Metrics::textCode, 12);
  }

  void styleSheetCoversShellControlsAndOverlays() {
    const QString sheet = relay::theme::styleSheet();
    for (const QString& selector : {QStringLiteral("#titleRow"),
                                    QStringLiteral("#macTitleStrip"),
                                    QStringLiteral("#actionRow"),
                                    QStringLiteral("#sidebar"),
                                    QStringLiteral("#contentTabs"),
                                    QStringLiteral("#statusBar"),
                                    QStringLiteral("#repositoryList"),
                                    QStringLiteral("#changedFileList"),
                                    QStringLiteral("#commitFileList"),
                                    QStringLiteral("#historyList"),
                                    QStringLiteral("#modalBackdrop"),
                                    QStringLiteral("#modalCard"),
                                    QStringLiteral("#toast")}) {
      QVERIFY2(sheet.contains(selector), qPrintable(selector));
    }
  }

  void repositorySelectionIsTintOnly() {
    const QString sheet = relay::theme::styleSheet();
    const qsizetype start = sheet.indexOf(QStringLiteral("#repositoryList::item:selected"));
    QVERIFY(start >= 0);
    const qsizetype end = sheet.indexOf(QChar{u'}'}, start);
    QVERIFY(end > start);
    const QString rule = sheet.mid(start, end - start);
    QVERIFY(rule.contains(QStringLiteral("background: #e1e9e3")));
    QVERIFY(!rule.contains(QStringLiteral("border-left")));
  }

  void delegateSizeHintsUseSharedMetrics() {
    relay::RepositoryListModel repositories;
    relay::RepositorySummary repository;
    repository.path = QStringLiteral("/relay");
    repository.name = QStringLiteral("relay");
    repository.owner = QStringLiteral("owner");
    repositories.setRepositories({repository});

    relay::ChangedFileListModel files;
    relay::ChangedFile file;
    file.path = QStringLiteral("src/main.cpp");
    file.name = QStringLiteral("main.cpp");
    file.directory = QStringLiteral("src");
    files.setFiles({file});

    QStyleOptionViewItem option;
    option.rect = QRect{0, 0, 300, 60};
    relay::RepositoryItemDelegate repositoryDelegate;
    relay::ChangedFileItemDelegate fileDelegate;
    relay::CommitFileItemDelegate commitFileDelegate;
    QCOMPARE(repositoryDelegate.sizeHint(option, repositories.index(0)).height(),
             relay::theme::Metrics::repositoryRowHeight);
    QCOMPARE(fileDelegate.sizeHint(option, files.index(0)).height(),
             relay::theme::Metrics::fileRowHeight);
    QCOMPARE(commitFileDelegate.sizeHint(option, files.index(0)).height(),
             relay::theme::Metrics::commitFileRowHeight);
  }
};

QTEST_APPLESS_MAIN(ThemeTest)
#include "tst_theme.moc"
