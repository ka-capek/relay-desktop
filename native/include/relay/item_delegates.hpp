#pragma once

#include <QStyledItemDelegate>

namespace relay {

class RepositoryItemDelegate final : public QStyledItemDelegate {
 public:
  explicit RepositoryItemDelegate(QObject* parent = nullptr);

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                               const QModelIndex& index) const override;
};

class ChangedFileItemDelegate final : public QStyledItemDelegate {
 public:
  explicit ChangedFileItemDelegate(QObject* parent = nullptr);

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                               const QModelIndex& index) const override;
};

class HistoryCommitItemDelegate final : public QStyledItemDelegate {
 public:
  explicit HistoryCommitItemDelegate(QObject* parent = nullptr);

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                               const QModelIndex& index) const override;
};

class CommitFileItemDelegate final : public QStyledItemDelegate {
 public:
  explicit CommitFileItemDelegate(QObject* parent = nullptr);

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                               const QModelIndex& index) const override;
};

}  // namespace relay
