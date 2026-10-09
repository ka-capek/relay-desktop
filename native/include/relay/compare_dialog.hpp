#pragma once

#include "relay/domain.hpp"

#include <QDialog>

class QComboBox;
class QLabel;
class QListView;
class QPushButton;
class QTabWidget;

namespace relay {

class CommitFileListModel;
class DiffView;
class HistoryCommitListModel;

// Compares two branches without changing the working tree. The dialog only
// emits requests; MainWindow routes them to RelayController.
class CompareDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit CompareDialog(QWidget* parent = nullptr);
  // Lists the repository's branches, keeping the current choices if they exist.
  void setRepository(const Repository& repository);
  void selectBranches(const QString& base, const QString& compare);
  void showComparison(const BranchComparison& comparison);
  void showFileDiff(const QString& from, const QString& to, const QString& filePath, const QString& diff);
  void setDiffFontSize(int pixels);
 signals:
  void comparisonRequested(QString base, QString compare);
  void fileDiffRequested(QString from, QString to, QString filePath);
  void mergeRequested(QString ref);
 private:
  void request();
  QComboBox* base_{};
  QComboBox* compare_{};
  QLabel* summary_{};
  QTabWidget* tabs_{};
  HistoryCommitListModel* ahead_{};
  HistoryCommitListModel* behind_{};
  CommitFileListModel* files_{};
  QListView* fileList_{};
  DiffView* diff_{};
  QPushButton* merge_{};
  QString currentBranch_;
  QString diffFrom_;
  QString diffTo_;
};

}  // namespace relay
