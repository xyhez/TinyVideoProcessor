#include "multiviewrenderer.h"

#include <algorithm>

namespace {
constexpr int kMaxLogicalSlots = 4;
constexpr int kGrid2x2Columns = 2;
constexpr int kGrid2x2Rows = 2;
constexpr int kSlotBorderWidth = 2;
constexpr int kActiveSlotBorderWidth = 4;

constexpr float kBackgroundRed = 0.035f;
constexpr float kBackgroundGreen = 0.04f;
constexpr float kBackgroundBlue = 0.045f;
constexpr float kSlotRed = 0.07f;
constexpr float kSlotGreen = 0.075f;
constexpr float kSlotBlue = 0.08f;
constexpr float kEmptySlotRed = 0.055f;
constexpr float kEmptySlotGreen = 0.06f;
constexpr float kEmptySlotBlue = 0.065f;
constexpr float kBorderRed = 0.23f;
constexpr float kBorderGreen = 0.25f;
constexpr float kBorderBlue = 0.28f;
constexpr float kActiveBorderRed = 0.16f;
constexpr float kActiveBorderGreen = 0.48f;
constexpr float kActiveBorderBlue = 0.92f;
}

MultiViewRenderer::MultiViewRenderer(QWidget* parent)
    : QOpenGLWidget(parent) {
    setMinimumSize(320, 240);
}

bool MultiViewRenderer::Viewport::IsBound() const {
    return streamId != InvalidStreamId;
}

void MultiViewRenderer::SetLayout(Layout layout) {
    if (layout_ == layout) {
        return;
    }

    layout_ = layout;
    update();
}

MultiViewRenderer::Layout MultiViewRenderer::CurrentLayout() const {
    return layout_;
}

void MultiViewRenderer::SetViewSlots(const QMap<int, StreamId>& viewSlots) {
    QMap<int, StreamId> normalizedViewSlots;
    for (auto it = viewSlots.cbegin(); it != viewSlots.cend(); ++it) {
        if (!IsLogicalSlotIndex(it.key()) || it.value() == InvalidStreamId) {
            continue;
        }
        normalizedViewSlots.insert(it.key(), it.value());
    }

    if (viewSlots_ == normalizedViewSlots) {
        return;
    }

    viewSlots_ = normalizedViewSlots;
    update();
}

QMap<int, MultiViewRenderer::StreamId> MultiViewRenderer::ViewSlots() const {
    return viewSlots_;
}

void MultiViewRenderer::SetActiveStream(StreamId streamId) {
    if (activeStreamId_ == streamId) {
        return;
    }

    activeStreamId_ = streamId;
    update();
}

MultiViewRenderer::StreamId MultiViewRenderer::ActiveStream() const {
    return activeStreamId_;
}

int MultiViewRenderer::MaxVisibleSlots() const {
    switch (layout_) {
    case Layout::Single:
        return 1;
    case Layout::Grid2x2:
        return kMaxLogicalSlots;
    }

    return 1;
}

QVector<MultiViewRenderer::Viewport> MultiViewRenderer::Viewports() const {
    QVector<Viewport> viewports;
    viewports.reserve(MaxVisibleSlots());

    for (int slotIndex = 0; slotIndex < MaxVisibleSlots(); ++slotIndex) {
        viewports.append(ViewportForSlot(slotIndex));
    }

    return viewports;
}

MultiViewRenderer::Viewport MultiViewRenderer::ViewportForSlot(int slotIndex) const {
    if (!IsLogicalSlotIndex(slotIndex) || slotIndex >= MaxVisibleSlots()) {
        return {};
    }

    const QRect qtRect = QtRectForSlot(slotIndex);
    return Viewport {
        slotIndex,
        viewSlots_.value(slotIndex, InvalidStreamId),
        qtRect,
        ToOpenGLViewportRect(qtRect)
    };
}

void MultiViewRenderer::initializeGL() {
    initializeOpenGLFunctions();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(kBackgroundRed, kBackgroundGreen, kBackgroundBlue, 1.0f);
}

void MultiViewRenderer::paintGL() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width(), height());
    glClearColor(kBackgroundRed, kBackgroundGreen, kBackgroundBlue, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_SCISSOR_TEST);
    for (const Viewport& viewport : Viewports()) {
        DrawViewport(viewport);
    }
    glDisable(GL_SCISSOR_TEST);
}

bool MultiViewRenderer::IsLogicalSlotIndex(int slotIndex) const {
    return slotIndex >= 0 && slotIndex < kMaxLogicalSlots;
}

QRect MultiViewRenderer::QtRectForSlot(int slotIndex) const {
    if (width() <= 0 || height() <= 0) {
        return {};
    }

    switch (layout_) {
    case Layout::Single:
        return slotIndex == 0 ? rect() : QRect();
    case Layout::Grid2x2: {
        if (!IsLogicalSlotIndex(slotIndex)) {
            return {};
        }

        const int row = slotIndex / kGrid2x2Columns;
        const int column = slotIndex % kGrid2x2Columns;
        const int left = column * width() / kGrid2x2Columns;
        const int right = (column + 1) * width() / kGrid2x2Columns;
        const int top = row * height() / kGrid2x2Rows;
        const int bottom = (row + 1) * height() / kGrid2x2Rows;
        return QRect(left, top, right - left, bottom - top);
    }
    }

    return rect();
}

QRect MultiViewRenderer::ToOpenGLViewportRect(const QRect& qtRect) const {
    if (qtRect.isEmpty() || height() <= 0) {
        return {};
    }

    return QRect(qtRect.x(),
                 height() - qtRect.y() - qtRect.height(),
                 qtRect.width(),
                 qtRect.height());
}

void MultiViewRenderer::DrawViewport(const Viewport& viewport) {
    if (viewport.glViewportRect.isEmpty()) {
        return;
    }

    if (viewport.IsBound()) {
        FillOpenGLRect(viewport.glViewportRect, kSlotRed, kSlotGreen, kSlotBlue, 1.0f);
    } else {
        FillOpenGLRect(viewport.glViewportRect, kEmptySlotRed, kEmptySlotGreen, kEmptySlotBlue, 1.0f);
    }

    const bool active = viewport.IsBound() && viewport.streamId == activeStreamId_;
    DrawViewportBorder(viewport.glViewportRect, active);
}

void MultiViewRenderer::FillOpenGLRect(const QRect& rect,
                                       float red,
                                       float green,
                                       float blue,
                                       float alpha) {
    if (rect.isEmpty()) {
        return;
    }

    glScissor(rect.x(), rect.y(), rect.width(), rect.height());
    glClearColor(red, green, blue, alpha);
    glClear(GL_COLOR_BUFFER_BIT);
}

void MultiViewRenderer::DrawViewportBorder(const QRect& rect, bool active) {
    if (rect.isEmpty()) {
        return;
    }

    const int targetBorderWidth = active ? kActiveSlotBorderWidth : kSlotBorderWidth;
    const int borderWidth = std::max(1, std::min({targetBorderWidth, rect.width(), rect.height()}));
    const float borderRed = active ? kActiveBorderRed : kBorderRed;
    const float borderGreen = active ? kActiveBorderGreen : kBorderGreen;
    const float borderBlue = active ? kActiveBorderBlue : kBorderBlue;
    const int x = rect.x();
    const int y = rect.y();
    const int width = rect.width();
    const int height = rect.height();

    FillOpenGLRect(QRect(x, y, borderWidth, height), borderRed, borderGreen, borderBlue, 1.0f);
    FillOpenGLRect(QRect(x + width - borderWidth, y, borderWidth, height),
                   borderRed,
                   borderGreen,
                   borderBlue,
                   1.0f);
    FillOpenGLRect(QRect(x, y, width, borderWidth), borderRed, borderGreen, borderBlue, 1.0f);
    FillOpenGLRect(QRect(x, y + height - borderWidth, width, borderWidth),
                   borderRed,
                   borderGreen,
                   borderBlue,
                   1.0f);
}
