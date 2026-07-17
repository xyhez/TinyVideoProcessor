#ifndef UI_STREAMLISTITEMDELEGATE_H
#define UI_STREAMLISTITEMDELEGATE_H

#include <QStyledItemDelegate>

/**
 * @brief StreamListItemDelegate 绘制紧凑的视频源信息与状态徽章
 *
 * Delegate 只读取 QModelIndex 中的展示数据，不访问 Controller，
 * 也不为每一行创建独立 QWidget。
 */
class StreamListItemDelegate final : public QStyledItemDelegate
{
public:
    explicit StreamListItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};

#endif // UI_STREAMLISTITEMDELEGATE_H
