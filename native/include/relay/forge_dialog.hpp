#pragma once
#include "relay/forge_types.hpp"
#include <QDialog>
#include <QList>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QSortFilterProxyModel;
class QStringListModel;

namespace relay {
class RelayController;
struct AppState;

class ForgeDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit ForgeDialog(RelayController* controller, QWidget* parent = nullptr);
  [[nodiscard]] QString cloneUrl() const;
  [[nodiscard]] QString repositoryName() const;
 private:
  void updateAccounts(const AppState& state);
  void loadRepositories();
  void clearRepositories();
  void updateSelection();
  RelayController* controller_;
  QComboBox* accounts_;
  QComboBox* kind_;
  QLineEdit* server_;
  QLineEdit* token_;
  QLabel* message_;
  QPushButton* connect_;
  QPushButton* remove_;
  QPushButton* refresh_;
  QPushButton* clone_;
  QPushButton* open_;
  QListView* repositories_;
  QStringListModel* model_;
  QSortFilterProxyModel* proxy_;
  QList<ForgeRepository> entries_;
  QString cloneUrl_;
  QString repositoryName_;
};
}  // namespace relay
