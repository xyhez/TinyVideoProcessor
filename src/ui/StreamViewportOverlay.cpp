#include "ui/StreamViewportOverlay.h"

#include "infrastructure/rendering/multiviewrenderer.h"

#include <QColor>
#include <QEvent>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace {

constexpr int kViewportInset = 7;
constexpr int kPanelHorizontalPadding = 7;
constexpr int kPanelVerticalPadding = 4;
constexpr int kPanelSpacing = 5;
constexpr int kBadgeHorizontalPadding = 5;
constexpr int kBadgeSpacing = 4;
constexpr int kMinimumViewportWidth = 72;
constexpr int kMinimumViewportHeight = 44;
constexpr int kMinimumTitleWidth = 24;
constexpr int kMinimumMetricsViewportWidth = 160;
constexpr int kMinimumMetricsViewportHeight = 96;

struct StatusBadge {
    QString text;
    QColor color;
};

QColor WithAlpha(QColor color, int alpha) {
    color.setAlpha(alpha);
    return color;
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

QVector<StatusBadge> BuildStatusBadges(
    const StreamPresentationState& state,
    const QPalette& palette) {
    QVector<StatusBadge> badges;
    if (!state.lastError.isEmpty()) {
        badges.append({QStringLiteral("最近错误"), palette.color(QPalette::BrightText)});
    }
    if (state.isActiveVideo) {
        badges.append({QStringLiteral("主画面"), palette.color(QPalette::Highlight)});
    }
    if (state.isActiveAudio) {
        badges.append({QStringLiteral("音频"), palette.color(QPalette::Link)});
    }
    if (state.isRealtime) {
        badges.append({QStringLiteral("LIVE"), palette.color(QPalette::Link)});
    }
    return badges;
}

int BadgeWidth(const QFontMetrics& metrics, const QString& text) {
    return metrics.horizontalAdvance(text) + kBadgeHorizontalPadding * 2;
}

QString RuntimeMetricText(const StreamPresentationState& state) {
    QStringList parts;
    if (state.runtimeMetrics.hasFrameSize) {
        parts.append(QStringLiteral("%1×%2")
                         .arg(state.runtimeMetrics.frameWidth)
                         .arg(state.runtimeMetrics.frameHeight));
    }
    if (state.runtimeMetrics.hasPreviewFps) {
        parts.append(QStringLiteral("%1 FPS")
                         .arg(state.runtimeMetrics.previewFps, 0, 'f', 1));
    }
    return parts.join(QStringLiteral(" · "));
}

void DrawBadge(QPainter& painter,
               const QRect& rect,
               const QString& text,
               const QColor& color) {
    painter.setPen(WithAlpha(color, 170));
    painter.setBrush(WithAlpha(color, 42));
    painter.drawRoundedRect(rect.adjusted(0, 0, -1, -1), 2.0, 2.0);
    painter.setPen(color);
    painter.drawText(rect, Qt::AlignCenter, text);
}

int DrawTitlePanel(QPainter& painter,
                   const QRect& viewportRect,
                   const StreamPresentationState& state,
                   const QPalette& palette) {
    const QFont bodyFont = painter.font();
    QFont titleFont = bodyFont;
    titleFont.setBold(true);
    painter.setFont(titleFont);
    const QFontMetrics titleMetrics(titleFont);
    const QFontMetrics bodyMetrics(bodyFont);
    const QString metricText = RuntimeMetricText(state);
    QFont statusFont = bodyFont;
    statusFont.setBold(true);
    const QFontMetrics statusMetrics(statusFont);
    const bool hasStatusBadges = !BuildStatusBadges(state, palette).isEmpty();
    const int metricsPanelHeight = titleMetrics.height()
        + kPanelSpacing
        + bodyMetrics.height()
        + kPanelVerticalPadding * 2;
    const int metricsAndStatusRequiredHeight = kViewportInset
        + metricsPanelHeight
        + (hasStatusBadges
            ? kPanelSpacing + statusMetrics.height() + kPanelVerticalPadding * 2
            : 0)
        + kViewportInset;
    const bool showMetrics = !metricText.isEmpty()
        && viewportRect.width() >= kMinimumMetricsViewportWidth
        && viewportRect.height() >= kMinimumMetricsViewportHeight
        && viewportRect.height() >= metricsAndStatusRequiredHeight;
    const int metricHeight = showMetrics
        ? kPanelSpacing + bodyMetrics.height()
        : 0;
    const int panelHeight =
        titleMetrics.height() + metricHeight + kPanelVerticalPadding * 2;
    const QRect panelRect(
        viewportRect.left() + kViewportInset,
        viewportRect.top() + kViewportInset,
        qMax(0, viewportRect.width() - kViewportInset * 2),
        panelHeight);
    if (panelRect.width() <= kPanelHorizontalPadding * 2
        || panelRect.bottom() > viewportRect.bottom() - kViewportInset) {
        return viewportRect.top();
    }

    const QColor background = WithAlpha(palette.color(QPalette::Window), 210);
    const QColor border = WithAlpha(palette.color(QPalette::ButtonText), 95);
    painter.setPen(border);
    painter.setBrush(background);
    painter.drawRoundedRect(panelRect.adjusted(0, 0, -1, -1), 3.0, 3.0);

    const QString sourceText = SourceKindLabel(state.sourceKind);
    const int sourceWidth = BadgeWidth(titleMetrics, sourceText);
    const QRect contentRect(
        panelRect.left() + kPanelHorizontalPadding,
        panelRect.top() + kPanelVerticalPadding,
        qMax(0, panelRect.width() - kPanelHorizontalPadding * 2),
        titleMetrics.height());
    int titleLeft = contentRect.left();
    int titleWidth = contentRect.width();
    if (contentRect.width()
        >= sourceWidth + kPanelSpacing + kMinimumTitleWidth) {
        const QRect sourceRect(
            contentRect.left(),
            contentRect.top(),
            sourceWidth,
            contentRect.height());
        DrawBadge(
            painter,
            sourceRect,
            sourceText,
            palette.color(QPalette::Link));
        titleLeft = sourceRect.right() + kPanelSpacing;
        titleWidth = qMax(0, contentRect.right() - titleLeft + 1);
    }

    const QRect titleRect(
        titleLeft,
        contentRect.top(),
        titleWidth,
        contentRect.height());
    painter.setPen(palette.color(QPalette::Text));
    painter.drawText(
        titleRect,
        Qt::AlignLeft | Qt::AlignVCenter,
        titleMetrics.elidedText(
            state.displayName,
            Qt::ElideRight,
            titleRect.width()));

    if (showMetrics) {
        painter.setFont(bodyFont);
        painter.setPen(palette.color(QPalette::Link));
        const QRect metricRect(
            contentRect.left(),
            contentRect.bottom() + kPanelSpacing + 1,
            contentRect.width(),
            bodyMetrics.height());
        painter.drawText(
            metricRect,
            Qt::AlignLeft | Qt::AlignVCenter,
            bodyMetrics.elidedText(
                metricText,
                Qt::ElideRight,
                metricRect.width()));
    }

    return panelRect.bottom();
}

void DrawStatusPanel(QPainter& painter,
                     const QRect& viewportRect,
                     const StreamPresentationState& state,
                     const QPalette& palette,
                     int titlePanelBottom) {
    const QVector<StatusBadge> badges = BuildStatusBadges(state, palette);
    if (badges.isEmpty()) {
        return;
    }

    QFont statusFont = painter.font();
    statusFont.setBold(true);
    painter.setFont(statusFont);
    const QFontMetrics metrics(statusFont);
    const int badgeHeight = metrics.height() + kPanelVerticalPadding * 2;
    const int availableWidth = qMax(0, viewportRect.width() - kViewportInset * 2);
    if (availableWidth <= 0) {
        return;
    }

    int totalWidth = 0;
    int visibleCount = 0;
    for (const StatusBadge& badge : badges) {
        const int width = BadgeWidth(metrics, badge.text);
        const int nextWidth = totalWidth
            + (visibleCount > 0 ? kBadgeSpacing : 0)
            + width;
        if (nextWidth > availableWidth) {
            break;
        }
        totalWidth = nextWidth;
        visibleCount += 1;
    }
    if (visibleCount == 0) {
        return;
    }

    int x = viewportRect.left() + kViewportInset;
    const int y = viewportRect.bottom() - kViewportInset - badgeHeight + 1;
    if (y <= titlePanelBottom + kPanelSpacing) {
        return;
    }

    for (int index = 0; index < visibleCount; ++index) {
        const StatusBadge& badge = badges.at(index);
        const int width = BadgeWidth(metrics, badge.text);
        DrawBadge(
            painter,
            QRect(x, y, width, badgeHeight),
            badge.text,
            badge.color);
        x += width + kBadgeSpacing;
    }
}

} // namespace

StreamViewportOverlay::StreamViewportOverlay(MultiViewRenderer* renderer)
    : QWidget(renderer)
    , renderer_(renderer)
{
    Q_ASSERT(renderer_);
    setObjectName(QStringLiteral("streamViewportOverlay"));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);

    if (!renderer_) {
        return;
    }

    renderer_->installEventFilter(this);
    SyncToRenderer();
    show();
}

void StreamViewportOverlay::SetPresentationStates(
    const QMap<int, StreamPresentationState>& states) {
    presentationStates_ = states;
    SyncToRenderer();
}

bool StreamViewportOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == renderer_ && event) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Show:
            SyncToRenderer();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void StreamViewportOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    if (!renderer_) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    for (const MultiViewRenderer::Viewport& viewport : renderer_->Viewports()) {
        if (!viewport.IsBound()
            || viewport.qtRect.width() < kMinimumViewportWidth
            || viewport.qtRect.height() < kMinimumViewportHeight) {
            continue;
        }

        const auto stateIt = presentationStates_.constFind(viewport.streamId);
        if (stateIt == presentationStates_.cend()) {
            continue;
        }

        painter.save();
        painter.setClipRect(viewport.qtRect.adjusted(1, 1, -1, -1));
        const int titlePanelBottom =
            DrawTitlePanel(painter, viewport.qtRect, stateIt.value(), palette());
        DrawStatusPanel(
            painter,
            viewport.qtRect,
            stateIt.value(),
            palette(),
            titlePanelBottom);
        painter.restore();
    }
}

void StreamViewportOverlay::SyncToRenderer() {
    if (!renderer_) {
        return;
    }

    const QRect targetRect = renderer_->rect();
    if (geometry() != targetRect) {
        setGeometry(targetRect);
    }
    raise();
    update();
}
