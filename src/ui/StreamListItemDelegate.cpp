#include "ui/StreamListItemDelegate.h"

#include "ui/StreamPresentationState.h"

#include <QApplication>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QVariant>
#include <QVector>

namespace {

constexpr int kItemHeight = 58;
constexpr int kHorizontalPadding = 9;
constexpr int kVerticalPadding = 5;
constexpr int kLineSpacing = 3;
constexpr int kBadgeHorizontalPadding = 5;
constexpr int kBadgeSpacing = 4;
constexpr int kActiveBarWidth = 3;

struct StatusBadge {
    QString text;
    QColor color;
};

QPalette::ColorGroup ResolveColorGroup(const QStyleOptionViewItem& option) {
    if (!(option.state & QStyle::State_Enabled)) {
        return QPalette::Disabled;
    }
    return option.state & QStyle::State_Active
        ? QPalette::Active
        : QPalette::Inactive;
}

QString SourceKindLabel(StreamSourceKind sourceKind) {
    switch (sourceKind) {
    case StreamSourceKind::File:
        return QStringLiteral("FILE");
    case StreamSourceKind::Camera:
        return QStringLiteral("CAM");
    case StreamSourceKind::Rtsp:
        return QStringLiteral("RTSP");
    case StreamSourceKind::Unknown:
        return QStringLiteral("MEDIA");
    }
    return QStringLiteral("MEDIA");
}

QColor WithAlpha(QColor color, int alpha) {
    color.setAlpha(alpha);
    return color;
}

QVector<StatusBadge> BuildStatusBadges(
    const StreamPresentationState& state,
    const QPalette& palette,
    QPalette::ColorGroup colorGroup) {
    const QColor accent = palette.color(colorGroup, QPalette::Highlight);
    const QColor technical = palette.color(colorGroup, QPalette::Link);
    const QColor negative = palette.color(colorGroup, QPalette::BrightText);
    const QColor secondary = palette.color(colorGroup, QPalette::ButtonText);

    QVector<StatusBadge> badges;
    if (!state.lastError.isEmpty()) {
        badges.append({QStringLiteral("最近错误"), negative});
    }
    if (state.isActiveVideo) {
        badges.append({QStringLiteral("主画面"), accent});
    }
    if (state.isActiveAudio) {
        badges.append({QStringLiteral("音频"), technical});
    }
    if (state.isRealtime) {
        badges.append({QStringLiteral("LIVE"), technical});
    }
    if (badges.isEmpty()) {
        badges.append({QStringLiteral("已加载"), secondary});
    }
    return badges;
}

int DrawBadge(QPainter* painter,
              int x,
              const QRect& lineRect,
              const QString& text,
              const QColor& color,
              const QFontMetrics& metrics) {
    const int width = metrics.horizontalAdvance(text) + kBadgeHorizontalPadding * 2;
    const QRect badgeRect(x, lineRect.top(), width, lineRect.height());

    painter->setPen(WithAlpha(color, 150));
    painter->setBrush(WithAlpha(color, 28));
    painter->drawRoundedRect(badgeRect.adjusted(0, 1, -1, -1), 2.0, 2.0);
    painter->setPen(color);
    painter->drawText(badgeRect, Qt::AlignCenter, text);
    return width;
}

} // namespace

StreamListItemDelegate::StreamListItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void StreamListItemDelegate::paint(
    QPainter* painter,
    const QStyleOptionViewItem& option,
    const QModelIndex& index) const {
    const QVariant presentationData = index.data(kStreamPresentationStateRole);
    if (!presentationData.isValid()
        || !presentationData.canConvert<StreamPresentationState>()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    const StreamPresentationState state =
        presentationData.value<StreamPresentationState>();

    QStyleOptionViewItem itemOption(option);
    initStyleOption(&itemOption, index);
    itemOption.text.clear();
    itemOption.icon = QIcon();

    QStyle* style = itemOption.widget
        ? itemOption.widget->style()
        : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &itemOption, painter, itemOption.widget);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QPalette::ColorGroup colorGroup = ResolveColorGroup(itemOption);
    const QColor primaryText = itemOption.palette.color(colorGroup, QPalette::Text);
    const QColor secondaryText =
        itemOption.palette.color(colorGroup, QPalette::ButtonText);
    const QColor sourceColor = itemOption.palette.color(colorGroup, QPalette::Link);
    const QColor accent = itemOption.palette.color(colorGroup, QPalette::Highlight);

    if (state.isActiveVideo) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(accent);
        painter->drawRect(QRect(
            option.rect.left(),
            option.rect.top() + 3,
            kActiveBarWidth,
            option.rect.height() - 6));
    }

    const QRect contentRect = option.rect.adjusted(
        kHorizontalPadding,
        kVerticalPadding,
        -kHorizontalPadding,
        -kVerticalPadding);
    const int lineHeight = (contentRect.height() - kLineSpacing) / 2;
    const QRect titleLine(
        contentRect.left(),
        contentRect.top(),
        contentRect.width(),
        lineHeight);
    const QRect statusLine(
        contentRect.left(),
        titleLine.bottom() + kLineSpacing,
        contentRect.width(),
        lineHeight);

    QFont badgeFont = itemOption.font;
    badgeFont.setBold(true);
    painter->setFont(badgeFont);
    const QFontMetrics badgeMetrics(badgeFont);
    const QString sourceLabel = SourceKindLabel(state.sourceKind);
    const int sourceWidth =
        badgeMetrics.horizontalAdvance(sourceLabel) + kBadgeHorizontalPadding * 2;
    const QRect sourceRect(
        titleLine.left(),
        titleLine.top(),
        sourceWidth,
        titleLine.height());

    painter->setPen(WithAlpha(sourceColor, 150));
    painter->setBrush(WithAlpha(sourceColor, 28));
    painter->drawRoundedRect(sourceRect.adjusted(0, 1, -1, -1), 2.0, 2.0);
    painter->setPen(sourceColor);
    painter->drawText(sourceRect, Qt::AlignCenter, sourceLabel);

    QFont metaFont = itemOption.font;
    painter->setFont(metaFont);
    const QFontMetrics metaMetrics(metaFont);
    QString metaText = QStringLiteral("#%1").arg(state.streamId);
    if (state.slotIndex >= 0) {
        metaText += QStringLiteral(" · S%1").arg(state.slotIndex + 1);
    }
    const int metaWidth = metaMetrics.horizontalAdvance(metaText);
    const QRect metaRect(
        titleLine.right() - metaWidth + 1,
        titleLine.top(),
        metaWidth,
        titleLine.height());
    painter->setPen(secondaryText);
    painter->drawText(metaRect, Qt::AlignRight | Qt::AlignVCenter, metaText);

    QFont nameFont = itemOption.font;
    nameFont.setBold(true);
    painter->setFont(nameFont);
    const QFontMetrics nameMetrics(nameFont);
    const QRect nameRect(
        sourceRect.right() + 7,
        titleLine.top(),
        qMax(0, metaRect.left() - sourceRect.right() - 14),
        titleLine.height());
    painter->setPen(primaryText);
    painter->drawText(
        nameRect,
        Qt::AlignLeft | Qt::AlignVCenter,
        nameMetrics.elidedText(state.displayName, Qt::ElideRight, nameRect.width()));

    QFont statusFont = itemOption.font;
    painter->setFont(statusFont);
    const QFontMetrics statusMetrics(statusFont);
    const QVector<StatusBadge> badges =
        BuildStatusBadges(state, itemOption.palette, colorGroup);
    int badgeX = statusLine.left();
    for (const StatusBadge& badge : badges) {
        const int badgeWidth =
            statusMetrics.horizontalAdvance(badge.text) + kBadgeHorizontalPadding * 2;
        if (badgeX + badgeWidth > statusLine.right() + 1) {
            const QRect ellipsisRect(
                badgeX,
                statusLine.top(),
                qMax(0, statusLine.right() - badgeX + 1),
                statusLine.height());
            painter->setPen(secondaryText);
            painter->drawText(
                ellipsisRect,
                Qt::AlignLeft | Qt::AlignVCenter,
                QStringLiteral("…"));
            break;
        }
        badgeX += DrawBadge(
            painter,
            badgeX,
            statusLine,
            badge.text,
            badge.color,
            statusMetrics);
        badgeX += kBadgeSpacing;
    }

    painter->restore();
}

QSize StreamListItemDelegate::sizeHint(
    const QStyleOptionViewItem& option,
    const QModelIndex& index) const {
    QSize hint = QStyledItemDelegate::sizeHint(option, index);
    hint.setHeight(qMax(hint.height(), kItemHeight));
    return hint;
}
