#include "relay/diff_model.hpp"

#include <QTest>

class DiffModelTest final : public QObject {
  Q_OBJECT

 private slots:
  void preservesElectronParserSemantics() {
    relay::DiffModel model;
    model.setDiff(QStringLiteral(
        "diff --git a/file.txt b/file.txt\n"
        "index 1111111..2222222 100644\n"
        "--- a/file.txt\n"
        "+++ b/file.txt\n"
        "@@ -10,3 +20,4 @@ section\n"
        " context\n"
        "-old value\n"
        "+new value\n"
        "\\ No newline at end of file\n"
        " final context"));

    QCOMPARE(model.rowCount(), 6);

    const relay::DiffLine hunk = model.lineAt(0);
    QCOMPARE(static_cast<int>(hunk.kind), static_cast<int>(relay::DiffLineKind::hunk));
    QCOMPARE(hunk.oldLine, QString{});
    QCOMPARE(hunk.newLine, QString{});
    QCOMPARE(hunk.text, QStringLiteral("@@ -10,3 +20,4 @@ section"));

    const relay::DiffLine context = model.lineAt(1);
    QCOMPARE(static_cast<int>(context.kind), static_cast<int>(relay::DiffLineKind::plain));
    QCOMPARE(context.oldLine, QStringLiteral("10"));
    QCOMPARE(context.newLine, QStringLiteral("20"));
    QCOMPARE(context.text, QStringLiteral("context"));

    const relay::DiffLine removal = model.lineAt(2);
    QCOMPARE(static_cast<int>(removal.kind), static_cast<int>(relay::DiffLineKind::removal));
    QCOMPARE(removal.oldLine, QStringLiteral("11"));
    QCOMPARE(removal.newLine, QString{});
    QCOMPARE(removal.text, QStringLiteral("-old value"));

    const relay::DiffLine addition = model.lineAt(3);
    QCOMPARE(static_cast<int>(addition.kind), static_cast<int>(relay::DiffLineKind::addition));
    QCOMPARE(addition.oldLine, QString{});
    QCOMPARE(addition.newLine, QStringLiteral("21"));
    QCOMPARE(addition.text, QStringLiteral("+new value"));

    const relay::DiffLine marker = model.lineAt(4);
    QCOMPARE(static_cast<int>(marker.kind), static_cast<int>(relay::DiffLineKind::plain));
    QCOMPARE(marker.oldLine, QString{});
    QCOMPARE(marker.newLine, QString{});
    QCOMPARE(marker.text, QStringLiteral("\\ No newline at end of file"));

    const relay::DiffLine finalContext = model.lineAt(5);
    QCOMPARE(finalContext.oldLine, QStringLiteral("12"));
    QCOMPARE(finalContext.newLine, QStringLiteral("22"));
    QCOMPARE(finalContext.text, QStringLiteral("final context"));
  }

  void filtersEveryElectronMetadataPrefix() {
    relay::DiffModel model;
    model.setDiff(QStringLiteral(
        "diff --git a/old b/new\n"
        "index 123..456 100644\n"
        "--- a/old\n"
        "+++ b/new\n"
        "new file mode 100644\n"
        "deleted file mode 100644\n"
        "old mode 100644\n"
        "new mode 100755\n"
        "similarity index 100%\n"
        "dissimilarity index 80%\n"
        "rename from old\n"
        "rename to new\n"
        "copy from old\n"
        "copy to new\n"
        "@@ -1 +1 @@\n"
        "-before\n"
        "+after"));

    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.isHunk(0));
    QCOMPARE(model.lineAt(1).text, QStringLiteral("-before"));
    QCOMPARE(model.lineAt(2).text, QStringLiteral("+after"));
  }

  void retainsJavascriptSplitTrailingLine() {
    relay::DiffModel model;
    model.setDiff(QStringLiteral("@@ -1 +1 @@\n same\n"));

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.lineAt(1).text, QStringLiteral("same"));
    QCOMPARE(model.lineAt(2).text, QString{});
    QCOMPARE(model.lineAt(2).oldLine, QStringLiteral("2"));
    QCOMPARE(model.lineAt(2).newLine, QStringLiteral("2"));
  }

  void exposesAccessibleLineDescriptions() {
    relay::DiffModel model;
    model.setDiff(QStringLiteral("@@ -4 +8 @@\n-old\n+new"));

    const QModelIndex removed = model.index(1, relay::DiffModel::textColumn);
    const QModelIndex added = model.index(2, relay::DiffModel::textColumn);
    QVERIFY(removed.data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("Removed line 4")));
    QVERIFY(added.data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("Added line 8")));
    QCOMPARE(model.headerData(relay::DiffModel::oldLineColumn, Qt::Horizontal).toString(),
             QStringLiteral("Old line"));
  }

  void parsesOneHundredThousandLinesWithoutRowWidgets() {
    constexpr int addedLineCount = 100'000;
    QString diff = QStringLiteral("@@ -0,0 +1,100000 @@\n");
    diff.reserve(1'200'000);
    for (int line = 1; line <= addedLineCount; ++line) {
      diff.append(QChar{u'+'});
      diff.append(QString::number(line));
      if (line != addedLineCount) {
        diff.append(QChar{u'\n'});
      }
    }

    relay::DiffModel model;
    model.setDiff(std::move(diff));

    QCOMPARE(model.rowCount(), addedLineCount + 1);
    QVERIFY(model.isHunk(0));
    const relay::DiffLine first = model.lineAt(1);
    const relay::DiffLine middle = model.lineAt(50'000);
    const relay::DiffLine last = model.lineAt(addedLineCount);
    QCOMPARE(first.newLine, QStringLiteral("1"));
    QCOMPARE(middle.newLine, QStringLiteral("50000"));
    QCOMPARE(last.newLine, QStringLiteral("100000"));
    QCOMPARE(last.text, QStringLiteral("+100000"));
  }
};

QTEST_APPLESS_MAIN(DiffModelTest)
#include "tst_diff_model.moc"
