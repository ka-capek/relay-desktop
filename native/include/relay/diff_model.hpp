#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QString>
#include <QStringView>

namespace relay {

enum class DiffLineKind { plain, addition, removal, hunk };

struct DiffLine {
  QString oldLine;
  QString newLine;
  DiffLineKind kind{DiffLineKind::plain};
  QString text;
};

class DiffModel final : public QAbstractTableModel {
 public:
  enum Column { oldLineColumn, newLineColumn, textColumn, columnCountValue };

  enum Role {
    kindRole = Qt::UserRole + 1,
    oldLineNumberRole,
    newLineNumberRole,
    lineTextRole,
  };

  explicit DiffModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index,
                              int role = Qt::DisplayRole) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                    int role = Qt::DisplayRole) const override;
  [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  void setDiff(QString diff);
  void clear();

  [[nodiscard]] const QString& diff() const noexcept;
  [[nodiscard]] DiffLine lineAt(int row) const;
  [[nodiscard]] DiffLineKind kindAt(int row) const noexcept;
  [[nodiscard]] QStringView textAt(int row) const noexcept;
  [[nodiscard]] bool isHunk(int row) const noexcept;

 private:
  struct Row {
    qsizetype textOffset{};
    qsizetype textLength{};
    qint64 oldLine{-1};
    qint64 newLine{-1};
    DiffLineKind kind{DiffLineKind::plain};
  };

  [[nodiscard]] const Row* rowAt(int row) const noexcept;
  [[nodiscard]] QString accessibleText(const Row& row) const;
  void parse();

  QString diff_;
  QList<Row> rows_;
};

}  // namespace relay

Q_DECLARE_METATYPE(relay::DiffLineKind)
