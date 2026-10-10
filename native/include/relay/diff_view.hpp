#pragma once

#include "relay/diff_model.hpp"
#include "relay/domain.hpp"

#include <QFont>
#include <QTableView>

class QAction;
class QPaintEvent;

namespace relay {

class DiffView final : public QTableView {
 public:
  explicit DiffView(QWidget* parent = nullptr);

  void setDiff(QString diff);
  void setPreview(const FilePreview& preview);
  void clearDiff();
  void setCodeFontSize(int pixels);

  [[nodiscard]] DiffModel* diffModel() noexcept;
  [[nodiscard]] const DiffModel* diffModel() const noexcept;
  [[nodiscard]] QAction* copyAction() const noexcept;

  void copySelection();

  [[nodiscard]] QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void rebuildSpansAndWidths();
  // Shown when a file has no text diff; empty after clearDiff().
  QString emptyText_;

  DiffModel* model_{};
  QAction* copyAction_{};
  QFont codeFont_;
  QImage before_;
  QImage after_;
};

}  // namespace relay
