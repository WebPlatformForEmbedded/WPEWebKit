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

#ifndef WebPopupMenuProxyWPE_h
#define WebPopupMenuProxyWPE_h

#include <wtf/Vector.h>
#include "WebPopupMenuProxy.h"
#include "WebPopupItem.h"
#include "IntDim.h"
#include "IntRect.h"
#include "FloatPoint.h"
#include "FontCascade.h"

struct wpe_view_backend;
struct wpe_popup;
struct wpe_buffer;
struct wpe_input_client;
struct wpe_popup_client;
struct wpe_buffer_client;
struct wpe_input_axis_event;
struct wpe_input_keyboard_event;
struct wpe_input_touch_event;

namespace WKWPE {
class View;
};

namespace WebCore {
class GraphicsContext;
};

namespace WebKit {

class WebPageProxy;
class PageClientImpl;

class WebPopupMenuProxyWPE : public WebPopupMenuProxy {
public:
    static Ref<WebPopupMenuProxyWPE> create(struct wpe_view_backend* backend, WebPopupMenuProxy::Client& client, WKWPE::View& page)
    {
        return adoptRef(*new WebPopupMenuProxyWPE(backend, client, page));
    }
    virtual ~WebPopupMenuProxyWPE();

    virtual void showPopupMenu(const WebCore::IntRect&, WebCore::TextDirection, double pageScaleFactor, const Vector<WebPopupItem>&, const PlatformPopupMenuData&, int32_t selectedIndex) override;
    virtual void hidePopupMenu() override;
    virtual void cancelTracking() override;

private:
    enum InputEvent {
        InputMotion,
        InputDown,
        InputUp,
        InputNone
    };

    struct InputState {
        bool isDown = false;
        bool isScrolling = false;
        WebCore::IntPoint startPosition;
        int32_t startOffset;

        void initialize() {
            isDown = false;
        }
    };

    struct MenuItem {
        MenuItem(const WebPopupItem& i, int32_t idx)
            : item(i)
            , index(idx)
        {}

        WebPopupItem item;
        WebCore::IntDim dim;
        WebCore::IntDim highlightDim;
        int32_t index;

        bool isSelectable() const
        {
            return item.m_type == WebPopupItem::Item;
        }
    };

    WebPopupMenuProxyWPE(struct wpe_view_backend*, WebPopupMenuProxy::Client&, WKWPE::View&);

    WKWPE::View& m_page;
    struct wpe_view_backend* m_backend;
    struct wpe_popup* m_popup {nullptr};
    bool m_needsRedraw {true};
    bool m_canRedraw {true};
    WebCore::IntRect m_rect;
    WebCore::TextDirection m_textDirection;

    const MenuItem* m_highlightItem;
    WebCore::FontCascade m_font;
    int32_t m_offset = 0;
    int32_t m_virtualHeight = 0;
    bool m_showScroll = false;
    bool m_running = false;

    InputState m_pointerInput;
    InputState m_touchInput;
    InputState m_axisInput;
    int m_touchTrackId = -1;

    struct Buffer {
        struct wpe_buffer* buffer;
        bool is_free;
    };

    Vector<Buffer*> m_buffers;

    Vector<MenuItem> m_items;

    void cleanup();
    Buffer* getBuffer();
    void redraw();
    void positionItems();

    WebCore::IntDim virtualToPhysicalDim(const WebCore::IntDim&) const;
    int32_t physicalToVirtualY(uint32_t) const;
    int32_t scrollToVirtualY(int32_t) const;
    void setOffset(int32_t);
    void handleInputEvent(InputState&, InputEvent, int32_t, int32_t);
    void handleAxisEvent(const struct wpe_input_axis_event*);
    void handleKeyboardEvent(const struct wpe_input_keyboard_event*);
    void handleTouchEvent(const struct wpe_input_touch_event*);
    WebCore::IntRect contentRect() const;
    WebCore::IntRect scrollRect() const;
    WebCore::IntRect scrollTrackRect() const;
    MenuItem* itemAtPosition(int32_t);
    const MenuItem* itemAtPosition(int32_t) const;
    void highlightItem(const MenuItem*);
    void highlighItemOrNext(const MenuItem*);
    void highlightItemOrPrev(const MenuItem*);
    void centerItem(const MenuItem*);
    void selectItem(const MenuItem*);
    void moveHighlightUpPage();
    void moveHighlightDownPage();
    MenuItem* nextItem(const MenuItem*);
    MenuItem* prevItem(const MenuItem*);


    static const struct wpe_input_client s_inputClient;
    static const struct wpe_popup_client s_popupClient;
    static const struct wpe_buffer_client s_bufferClient;
};

} // namespace WebKit

#endif // WebPopupMenuProxyWPE_h

