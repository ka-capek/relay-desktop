#pragma once

#include "relay/conflict_resolution.hpp"
#include "relay/domain.hpp"

#include <QAbstractTableModel>
#include <QDialog>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableView;

namespace relay {

// Short names for the two versions of a conflicted file, by where each comes
// from. A rebase swaps Git's sides: the current one is the branch rebased onto.
struct ConflictSides {
  QString current;
  QString incoming;
};
[[nodiscard]] ConflictSides conflictSides(const Repository& repository);

// The two versions of a conflicted file side by side, one row per line.
// Common lines appear on both sides; a block's shorter side is padded with
// empty rows so that both sides stay aligned.
class ConflictLinesModel final : public QAbstractTableModel {
  Q_OBJECT
 public:
  enum Column { currentNumberColumn, currentTextColumn, incomingNumberColumn, incomingTextColumn, columnCountValue };
  enum Role {
    blockRole = Qt::UserRole + 1,  // conflict block index, or -1 for common lines
    lineRole,                      // whether the cell holds a line
    chosenRole,                    // whether the line goes into the result
    decidedRole,
  };

  explicit ConflictLinesModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
  [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
  bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

  void setFile(const ConflictFile& file);
  void setSides(const ConflictSides& sides);
  [[nodiscard]] const ConflictFile& file() const noexcept;
  [[nodiscard]] const QList<ConflictChoice>& choices() const noexcept;
  [[nodiscard]] int blockCount() const noexcept;
  [[nodiscard]] int decidedCount() const;
  [[nodiscard]] int firstRow(int block) const;
  [[nodiscard]] int blockAt(int row) const;
  [[nodiscard]] QString result() const;

  // Whole-block decisions; each one marks the block decided.
  void chooseBlock(int block, bool current, bool incoming);
  void setIncomingFirst(int block, bool incomingFirst);
  // Returns the block to undecided, so it keeps its conflict markers.
  void resetBlock(int block);

 signals:
  void choicesChanged();

 private:
  struct Row {
    int segment{};
    int block{-1};
    int current{-1};   // line index in the segment (common) or block side
    int incoming{-1};
    qint64 currentNumber{};
    qint64 incomingNumber{};
  };
  [[nodiscard]] const QString* lineText(const Row& row, bool incoming) const;
  void blockChanged(int block);

  ConflictFile file_;
  ConflictSides sides_;
  QList<Row> rows_;
  QList<int> blockRows_;
  QList<int> blockSegments_;
  QList<ConflictChoice> choices_;
};

// Resolves one conflicted text file line by line. It only emits requests;
// MainWindow routes them to RelayController and reports results back.
class ConflictResolverDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit ConflictResolverDialog(const QString& filePath, QWidget* parent = nullptr);

  [[nodiscard]] QString filePath() const;
  [[nodiscard]] ConflictLinesModel* linesModel() const noexcept;
  void setCodeFontSize(int pixels);
  // Accepts the dialog once the file is no longer conflicted.
  void setRepository(const Repository& repository);
  void setFile(const ConflictFile& file);
  void setBusy(bool busy);
  void showError(const QString& message);
  [[nodiscard]] QString resultText() const;

  void reject() override;

 signals:
  void saveRequested(QString filePath, QString contentHash, QString content);

 private:
  void showBlock(int block);
  void updateState();
  void refreshResult();
  void setManualEditing(bool editing);

  QString filePath_;
  ConflictLinesModel* model_{};
  QTableView* table_{};
  QLabel* position_{};
  QPushButton* previous_{};
  QPushButton* next_{};
  QPushButton* useCurrent_{};
  QPushButton* useIncoming_{};
  QPushButton* useBoth_{};
  QPushButton* useNeither_{};
  QCheckBox* incomingFirst_{};
  QPushButton* reset_{};
  QPushButton* editResult_{};
  QPlainTextEdit* result_{};
  QLabel* status_{};
  QLabel* error_{};
  QPushButton* save_{};
  int block_{-1};
  bool loaded_{};
  bool busy_{};
  bool lineEndingsCrLf_{};
  bool updatingResult_{};
};

}  // namespace relay
