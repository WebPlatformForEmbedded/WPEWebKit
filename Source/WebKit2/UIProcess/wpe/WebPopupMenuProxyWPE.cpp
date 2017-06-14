/*
 * Copyright (C) 2017 Garmin International
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <iostream>
#include <stdio.h>
#include <cairo.h>
#include <wpe/input.h>
#include <wpe/view-backend.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wtf/RunLoop.h>

#include "WebPopupMenuProxyWPE.h"
#include "GraphicsContext.h"
#include "RenderTheme.h"
#include "CSSValueKeywords.h"
#include "WPEView.h"

static const int32_t separatorHeight = 2;
static const int32_t padding = 5;
static const int32_t borderThickness = 2;
static const int32_t scrollWidth = 4;
static const float scrollDistanceThreshold = 5;

namespace WebKit {
WebPopupMenuProxyWPE::WebPopupMenuProxyWPE(struct wpe_view_backend* backend, WebPopupMenuProxy::Client& client, WKWPE::View& page)
    : WebPopupMenuProxy(client)
    , m_page(page)
    , m_backend(backend)
{
    WebCore::FontCascadeDescription fontDescription;
    fontDescription.setComputedSize(12);
    m_font = WebCore::FontCascade(fontDescription, 0, 0);
    m_font.update(0);
}


WebPopupMenuProxyWPE::~WebPopupMenuProxyWPE()
{
    cleanup();
}

void WebPopupMenuProxyWPE::showPopupMenu(const WebCore::IntRect& rect, WebCore::TextDirection dir, double pageScaleFactor, const Vector<WebPopupItem>& items, const PlatformPopupMenuData& data, int32_t selectedIndex)
{
    m_rect = rect;
    m_textDirection = dir;
    m_offset = 0;
    m_showScroll = false;
    m_pointerInput.initialize();
    m_touchInput.initialize();
    m_axisInput.initialize();
    m_touchTrackId = -1;
    m_highlightItem = nullptr;

    int32_t index = 0;
    for (auto const& i : items) {
        m_items.append(MenuItem(i, index));
        index++;
    }

    highlighItemOrNext(&m_items.first());

    if ((size_t)selectedIndex < m_items.size() && m_items.at(selectedIndex).isSelectable())
        m_highlightItem = &m_items.at(selectedIndex);

    if (!m_highlightItem) {
        // Nothing is selectable
        if (m_client)
            m_client->valueChangedForPopupMenu(this, -1);

        m_items.clear();
        return;
    }

    positionItems();

    // TODO: Does this correctly account for scrolling of content?
    int32_t belowHeight = m_page.size().height() - rect.maxY();
    int32_t aboveHeight = rect.y();
    int32_t physicalHeight = m_virtualHeight + borderThickness * 2;

    if (belowHeight > physicalHeight) {
        m_rect.setY(rect.maxY());
        m_rect.setHeight(physicalHeight);
    } else if (aboveHeight > physicalHeight) {
        m_rect.setY(rect.y() - physicalHeight);
        m_rect.setHeight(physicalHeight);
    } else if (belowHeight >= aboveHeight) {
        m_rect.setY(rect.maxY());
        m_rect.setHeight(belowHeight);
        m_showScroll = true;
    } else {
        m_rect.setY(0);
        m_rect.setHeight(aboveHeight);
        m_showScroll = true;
    }

    m_popup = wpe_view_backend_create_popup(m_backend, m_rect.x(), m_rect.y(), &s_popupClient, &s_inputClient, this);

    centerItem(m_highlightItem);

    m_canRedraw = true;
    m_needsRedraw = true;
    redraw();
}

void WebPopupMenuProxyWPE::hidePopupMenu()
{
    cleanup();
}

void WebPopupMenuProxyWPE::cancelTracking()
{
    cleanup();
}

void WebPopupMenuProxyWPE::cleanup()
{
    if (m_popup)
        wpe_popup_destroy(m_popup);
    m_popup = nullptr;

    for (auto i : m_buffers) {
        wpe_buffer_destroy(i->buffer);
        delete i;
    }

    m_buffers.clear();
    m_items.clear();
    m_highlightItem = nullptr;
}

WebPopupMenuProxyWPE::Buffer* WebPopupMenuProxyWPE::getBuffer()
{
    for (auto i : m_buffers) {
        if (i->is_free)
            return i;
    }

    Buffer* b = new Buffer();
    b->buffer = wpe_view_backend_alloc_buffer(m_backend, &s_bufferClient, b, WPE_FOURCC_ARGB8888, m_rect.width(), m_rect.height());
    b->is_free = true;
    m_buffers.append(b);

    return b;
}

void WebPopupMenuProxyWPE::redraw()
{
    if (!m_needsRedraw || !m_canRedraw || !m_popup)
        return;

    Buffer* buffer = getBuffer();

    struct wpe_buffer_info info;
    wpe_buffer_get_info(buffer->buffer, &info);

    RefPtr<cairo_surface_t> surface = adoptRef(cairo_image_surface_create_for_data(static_cast<unsigned char*>(info.data), CAIRO_FORMAT_ARGB32,
                info.width, info.height, info.stride));

    WebCore::GraphicsContext ctx(cairo_create(surface.get()));

    WebCore::IntRect border(0, 0, info.width, info.height);
    WebCore::Path path;
    path.addRect(border);

    ctx.setStrokeThickness(borderThickness * 2);
    ctx.setStrokeColor(WebCore::makeRGB(244, 244, 244));
    ctx.strokePath(path);

    WebCore::IntRect content = contentRect();

    if (m_showScroll) {
        auto scrollBarRect = scrollRect();
        auto trackRect = scrollTrackRect();

        ctx.setFillColor(WebCore::makeRGB(224, 224, 224));
        ctx.fillRect(scrollBarRect);

        WebCore::IntRect scrollThumbRect(trackRect.x(), trackRect.y() + trackRect.height() * m_offset / m_virtualHeight,
                trackRect.width(), trackRect.height() * content.height() / m_virtualHeight);

        ctx.setFillColor(WebCore::makeRGB(0, 149, 255));
        ctx.fillRect(scrollThumbRect);
    }

    ctx.clip(content);

    ctx.setFillColor(WebCore::Color::white);
    ctx.fillRect(content);

    auto highlightColor = WebCore::makeRGB(0, 149, 255);

    ctx.setFillColor(WebCore::Color::black);

    for (auto const& i : m_items) {
        WebCore::IntDim highlightDim = virtualToPhysicalDim(i.highlightDim);
        WebCore::IntDim dim = virtualToPhysicalDim(i.dim);

        if (content.yDimension().intersects(highlightDim)) {
            if (&i == m_highlightItem) {
                WebCore::GraphicsContextStateSaver saver(ctx);

                ctx.setFillColor(highlightColor);
                ctx.fillRect(WebCore::IntRect(border.xDimension(), highlightDim));
            }

            switch (i.item.m_type) {
                case WebPopupItem::Separator:
                    ctx.fillRect(WebCore::IntRect(border.xDimension(), dim));
                    break;

                case WebPopupItem::Item:
                    WebCore::TextRun text(i.item.m_text);
                    ctx.drawText(m_font, text, WebCore::FloatPoint(border.x() + padding, dim.max()));
                    break;
            }
        }
    }

    m_needsRedraw = false;
    m_canRedraw = false;

    wpe_popup_attach_buffer(m_popup, buffer->buffer);
}

void WebPopupMenuProxyWPE::positionItems()
{
    int32_t top = 0;

    for (auto& i : m_items) {
        i.dim.setStart(top + padding);

        switch (i.item.m_type) {
            case WebPopupItem::Separator:
                i.dim.setLength(separatorHeight);
                break;

            case WebPopupItem::Item:
                i.dim.setLength(m_font.fontMetrics().height());
                break;
        }

        i.highlightDim.setStart(top);
        i.highlightDim.setLength(i.dim.length() + padding * 2);

        top = i.highlightDim.max();
    }

    m_virtualHeight = top;
}

WebPopupMenuProxyWPE::MenuItem* WebPopupMenuProxyWPE::itemAtPosition(int32_t position)
{
    for (auto& i : m_items)
        if (i.highlightDim.contains(position))
            return &i;
    return nullptr;
}

const WebPopupMenuProxyWPE::MenuItem* WebPopupMenuProxyWPE::itemAtPosition(int32_t position) const
{
    for (const auto& i : m_items)
        if (i.highlightDim.contains(position))
            return &i;
    return nullptr;
}

void WebPopupMenuProxyWPE::highlightItem(const WebPopupMenuProxyWPE::MenuItem* item)
{
    if (!item)
        return;

    if (item != m_highlightItem && item->isSelectable()) {
        m_highlightItem = item;
        m_needsRedraw = true;
    }
}

void WebPopupMenuProxyWPE::centerItem(const WebPopupMenuProxyWPE::MenuItem* item)
{
    if (!item)
        return;

    auto content = contentRect();

    setOffset(item->highlightDim.start() - (content.height() - item->highlightDim.length()) / 2);
}

void WebPopupMenuProxyWPE::selectItem(const WebPopupMenuProxyWPE::MenuItem* item)
{
    highlightItem(item);

    if (m_client) {
        if (item)
            m_client->valueChangedForPopupMenu(this, item->index);
        else
            m_client->valueChangedForPopupMenu(this, -1);
    }
    cleanup();
}

WebCore::IntDim WebPopupMenuProxyWPE::virtualToPhysicalDim(const WebCore::IntDim& d) const
{
    auto rect = contentRect();
    WebCore::IntDim dim = d;
    dim.move(rect.y() - m_offset);
    return dim;
}

int32_t WebPopupMenuProxyWPE::physicalToVirtualY(uint32_t y) const
{
    auto rect = contentRect();
    return y + m_offset - rect.y();
}

int32_t WebPopupMenuProxyWPE::scrollToVirtualY(int32_t y) const
{
    if (!m_showScroll)
        return 0;

    auto rect = scrollTrackRect();
    return (y - rect.y()) * m_virtualHeight / rect.height();
}

void WebPopupMenuProxyWPE::setOffset(int32_t newOffset)
{
    auto content = contentRect();
    newOffset = std::max(newOffset, 0);
    newOffset = std::min(newOffset, m_virtualHeight - content.height());

    if (m_offset != newOffset) {
        m_offset = newOffset;
        m_needsRedraw = true;
    }
}

void WebPopupMenuProxyWPE::handleInputEvent(WebPopupMenuProxyWPE::InputState& state, WebPopupMenuProxyWPE::InputEvent event, int32_t x, int32_t y)
{
    auto content = contentRect();
    auto scroll = scrollRect();

    if (event == InputNone)
        return;

    if (!state.isDown) {
        if (event == InputMotion) {
            if (content.contains(x, y)) {
                int32_t virtualY = physicalToVirtualY(y);
                highlightItem(itemAtPosition(virtualY));
            }
        } else if (event == InputDown) {
            state.isDown = true;
            state.isScrolling = false;
            state.startPosition.setX(x);
            state.startPosition.setY(y);
            state.startOffset = m_offset;
        }
    } else {
        bool inContent = content.contains(state.startPosition.x(), state.startPosition.y());
        bool inScroll = scroll.contains(state.startPosition.x(), state.startPosition.y());

        if (!state.isScrolling) {
            float distance = (WebCore::FloatPoint(x, y) - WebCore::FloatPoint(state.startPosition)).diagonalLength();

            if (distance >= scrollDistanceThreshold)
                state.isScrolling = true;
        }

        if (event == InputUp)
            state.isDown = false;


        if (state.isScrolling) {
            if (inContent) {
                setOffset(state.startOffset - (y - state.startPosition.y()));
            } else if (inScroll) {
                setOffset(state.startOffset + (y - state.startPosition.y()) * m_virtualHeight / content.height());
            }
        } else if (event == InputUp) {
            // Click
            if (inContent) {
                int32_t virtualY = physicalToVirtualY(state.startPosition.y());
                const auto* item = itemAtPosition(virtualY);

                if (item && item->isSelectable())
                    selectItem(item);
            } else if (inScroll) {
                int32_t virtualY = scrollToVirtualY(state.startPosition.y());
                setOffset(virtualY - content.height() / 2);
            }
        }
    }
}

void WebPopupMenuProxyWPE::handleAxisEvent(const struct wpe_input_axis_event* event)
{
    auto content = contentRect();
    auto scroll = scrollRect();

    if (content.contains(event->x, event->y)) {
        setOffset(m_offset - event->value);
    } else if (scroll.contains(event->x, event->y)) {
        auto rect = scrollTrackRect();
        setOffset(m_offset - event->value * m_virtualHeight / rect.height());
    }
    handleInputEvent(m_axisInput, InputMotion, event->x, event->y);
}

void WebPopupMenuProxyWPE::handleKeyboardEvent(const struct wpe_input_keyboard_event* event)
{
    if (event->pressed && event->modifiers == 0) {
        switch (event->keyCode) {
        case XKB_KEY_Return:
            if (m_highlightItem->isSelectable())
                selectItem(m_highlightItem);
            break;
        case XKB_KEY_Up:
            highlightItemOrPrev(prevItem(m_highlightItem));
            centerItem(m_highlightItem);
            break;
        case XKB_KEY_Down:
            highlighItemOrNext(nextItem(m_highlightItem));
            centerItem(m_highlightItem);
            break;
        case XKB_KEY_Page_Up:
            moveHighlightUpPage();
            break;
        case XKB_KEY_Page_Down:
            moveHighlightDownPage();
            break;
        }
    }
}

void WebPopupMenuProxyWPE::handleTouchEvent(const struct wpe_input_touch_event* event)
{
    if (m_touchTrackId == -1) {
        for (uint64_t i = 0; i < event->touchpoints_length; i++) {
            auto* t = &event->touchpoints[i];
            if (t->type == wpe_input_touch_event_type_down) {
                m_touchTrackId = t->id;
                handleInputEvent(m_touchInput, InputDown, t->x, t->y);
                break;
            }
        }
    } else {
        for (uint64_t i = 0; i < event->touchpoints_length; i++) {
            auto* t = &event->touchpoints[i];
            if (t->id == m_touchTrackId) {
                if (t->type == wpe_input_touch_event_type_up) {
                    handleInputEvent(m_touchInput, InputUp, t->x, t->y);
                    m_touchTrackId = -1;
                } else if (t->type == wpe_input_touch_event_type_motion) {
                    handleInputEvent(m_touchInput, InputMotion, t->x, t->y);
                }
                break;
            }
        }
    }
}

WebCore::IntRect WebPopupMenuProxyWPE::contentRect() const
{
    WebCore::IntRect content(0, 0, m_rect.width(), m_rect.height());
    content.inflate(-(borderThickness));
    if (m_showScroll)
        content.setWidth(content.width() - (padding * 2 + scrollWidth));
    return content;
}

WebCore::IntRect WebPopupMenuProxyWPE::scrollRect() const
{
    if (!m_showScroll)
        return WebCore::IntRect(0, 0, 0, 0);

    int32_t scrollBarWidth = 2 * padding + scrollWidth;
    return WebCore::IntRect(m_rect.width() - (borderThickness + scrollBarWidth), borderThickness,
            scrollBarWidth, m_rect.height() - borderThickness * 2);
}

WebCore::IntRect WebPopupMenuProxyWPE::scrollTrackRect() const
{
    if (!m_showScroll)
        return WebCore::IntRect(0, 0, 0, 0);

    auto rect = scrollRect();

    return WebCore::IntRect(rect.x() + padding, rect.y() + padding,
            rect.width() - padding * 2, rect.height() - padding * 2);
}

void WebPopupMenuProxyWPE::highlighItemOrNext(const WebPopupMenuProxyWPE::MenuItem* item)
{
    for (; item; item = nextItem(item)) {
        if (item->isSelectable()) {
            highlightItem(item);
            break;
        }
    }
}

void WebPopupMenuProxyWPE::highlightItemOrPrev(const WebPopupMenuProxyWPE::MenuItem* item)
{
    for (; item; item = prevItem(item)) {
        if (item->isSelectable()) {
            highlightItem(item);
            break;
        }
    }
}

void WebPopupMenuProxyWPE::moveHighlightUpPage()
{
    auto content = contentRect();
    auto dim = virtualToPhysicalDim(m_highlightItem->highlightDim);

    if (!dim.inside(content.yDimension())) {
        setOffset(dim.max() - content.height());
    } else {
        auto const* item = itemAtPosition(m_offset);
        if (item)
            highlightItemOrPrev(itemAtPosition(m_offset));
        else
            highlighItemOrNext(&m_items.first());
        setOffset(m_highlightItem->highlightDim.max() - content.height());
    }
}

void WebPopupMenuProxyWPE::moveHighlightDownPage()
{
    auto content = contentRect();
    auto dim = virtualToPhysicalDim(m_highlightItem->highlightDim);

    if (!dim.inside(content.yDimension())) {
        setOffset(dim.start());
    } else {
        auto const* item = itemAtPosition(m_offset + content.height() - 1);
        if (item)
            highlighItemOrNext(item);
        else
            highlightItemOrPrev(&m_items.last());
        setOffset(m_highlightItem->highlightDim.start());
    }
}

WebPopupMenuProxyWPE::MenuItem* WebPopupMenuProxyWPE::nextItem(const WebPopupMenuProxyWPE::MenuItem* item)
{
    size_t idx = item->index + 1;

    if (idx < m_items.size())
        return &m_items.at(idx);
    return nullptr;
}

WebPopupMenuProxyWPE::MenuItem* WebPopupMenuProxyWPE::prevItem(const MenuItem* item)
{
    ssize_t idx = item->index - 1;

    if (idx >= 0)
        return &m_items.at(idx);
    return nullptr;
}

const struct wpe_input_client WebPopupMenuProxyWPE::s_inputClient = {
    // handle_keyboard_event
    [](void* data, struct wpe_input_keyboard_event* event)
    {
        auto* popup = static_cast<WebPopupMenuProxyWPE*>(data);

        popup->handleKeyboardEvent(event);
        popup->redraw();
    },
    // handle_pointer_event
    [](void* data, struct wpe_input_pointer_event* event)
    {
        auto* popup = static_cast<WebPopupMenuProxyWPE*>(data);
        bool isMotionEvent = event->type == wpe_input_pointer_event_type_motion;
        bool isClickEvent = (event->type == wpe_input_pointer_event_type_button && event->button == 1);
        bool isDownEvent = isClickEvent && event->state;
        bool isUpEvent = isClickEvent && !event->state;
        InputEvent e = InputNone;

        if (isMotionEvent) {
            e = InputMotion;
        } else if (isDownEvent) {
            e = InputDown;
        } else if (isUpEvent) {
            e = InputUp;
        }

        popup->handleInputEvent(popup->m_pointerInput, e, event->x, event->y);
        popup->redraw();
    },
    // handle_axis_event
    [](void* data, struct wpe_input_axis_event* event)
    {
        auto* popup = static_cast<WebPopupMenuProxyWPE*>(data);
        popup->handleAxisEvent(event);
        popup->redraw();
    },
    // handle_touch_event
    [](void* data, struct wpe_input_touch_event* event)
    {
        auto* popup = static_cast<WebPopupMenuProxyWPE*>(data);
        popup->handleTouchEvent(event);
        popup->redraw();
    },
};

const struct wpe_popup_client WebPopupMenuProxyWPE::s_popupClient = {
    // dismissed
    [](void* data)
    {
        auto* popup = static_cast<WebPopupMenuProxyWPE*>(data);
        popup->selectItem(nullptr);
    },
    // frame_displayed
    [](void* data)
    {
        auto* popup = static_cast<WebPopupMenuProxyWPE*>(data);
        popup->m_canRedraw = true;
        popup->redraw();
    },
};

const struct wpe_buffer_client WebPopupMenuProxyWPE::s_bufferClient = {
    // release
    [](void* data)
    {
        auto* buffer = static_cast<WebPopupMenuProxyWPE::Buffer*>(data);
        buffer->is_free = true;
    }
};

} // namespace WebKit

