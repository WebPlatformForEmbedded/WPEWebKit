/*
 * Copyright (C) 2014 Igalia S.L.
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

#ifndef ThreadedCompositor_h
#define ThreadedCompositor_h

#if USE(COORDINATED_GRAPHICS_THREADED)

#include "CompositingManager.h"
#include "CoordinatedGraphicsScene.h"
#include "SimpleViewportController.h"
#include <WebCore/GLContext.h>
#include <WebCore/IntSize.h>
#include <WebCore/TransformationMatrix.h>
#include <wtf/Atomics.h>
#include <wtf/Condition.h>
#include <wtf/FastMalloc.h>
#include <wtf/Lock.h>
#include <wtf/Noncopyable.h>
#include <wtf/ThreadSafeRefCounted.h>

#if USE(REQUEST_ANIMATION_FRAME_DISPLAY_MONITOR)
#include <WebCore/DisplayRefreshMonitor.h>
#endif

#if PLATFORM(WPE) && PLATFORM(WAYLAND)
#include <WebCore/WaylandSurface.h>
//KEYBOARD SUPPORT
#include <WPE/Input/Handling.h>
#include "WebPage.h"
//KEYBOARD SUPPORT
#endif



#if PLATFORM(WPE) && PLATFORM(WAYLAND)
#include <WebCore/WaylandSurface.h>
#endif

namespace WebCore {
struct CoordinatedGraphicsState;
}

namespace WebKit {

class CoordinatedGraphicsScene;
class CoordinatedGraphicsSceneClient;
class WebPage;

//KEYBOARD SUPPORT
#if PLATFORM(WPE) && PLATFORM(WAYLAND)
class ThreadedCompositor : public ThreadSafeRefCounted<ThreadedCompositor>, public SimpleViewportController::Client, public CoordinatedGraphicsSceneClient, public CompositingManager::Client, public WPE::Input::Client {
#else
class ThreadedCompositor : public ThreadSafeRefCounted<ThreadedCompositor>, public SimpleViewportController::Client, public CoordinatedGraphicsSceneClient, public CompositingManager::Client {
#endif
//KEYBOARD SUPPORT
    WTF_MAKE_NONCOPYABLE(ThreadedCompositor);
    WTF_MAKE_FAST_ALLOCATED;
public:
    class Client {
    public:
        virtual void setVisibleContentsRect(const WebCore::FloatRect&, const WebCore::FloatPoint&, float) = 0;
        virtual void purgeBackingStores() = 0;
        virtual void renderNextFrame() = 0;
        virtual void commitScrollOffset(uint32_t layerID, const WebCore::IntSize& offset) = 0;
//KEYBOARD SUPPORT
#if PLATFORM(WPE) && PLATFORM(WAYLAND)
        virtual WebPage* getWebPage() = 0;
#endif
//KEYBOARD SUPPORT
    };

    static Ref<ThreadedCompositor> create(Client*, WebPage&);
    virtual ~ThreadedCompositor();

    void setNeedsDisplay();

    void setNativeSurfaceHandleForCompositing(uint64_t);
    void setDeviceScaleFactor(float);

    void updateSceneState(const WebCore::CoordinatedGraphicsState&);

    void didChangeViewportSize(const WebCore::IntSize&);
    void didChangeViewportAttribute(const WebCore::ViewportAttributes&);
    void didChangeContentsSize(const WebCore::IntSize&);
    void scrollTo(const WebCore::IntPoint&);
    void scrollBy(const WebCore::IntSize&);

    RefPtr<WebCore::DisplayRefreshMonitor> createDisplayRefreshMonitor(PlatformDisplayID);
#if PLATFORM(WPE) && PLATFORM(WAYLAND)
    void requestFrame();
    void didFrameComplete();
//KEYBOARD SUPPORT
    void handleKeyboardEvent(WPE::Input::KeyboardEvent&&) override;
    void handlePointerEvent(WPE::Input::PointerEvent&& event) override;
    void handleAxisEvent(WPE::Input::AxisEvent&& event) override;
    void handleTouchEvent(WPE::Input::TouchEvent&& event) override;
//KEYBOARD SUPPORT

#endif
private:
    ThreadedCompositor(Client*, WebPage&);

    // CoordinatedGraphicsSceneClient
    void purgeBackingStores() override;
    void renderNextFrame() override;
    void updateViewport() override;
    void commitScrollOffset(uint32_t layerID, const WebCore::IntSize& offset) override;

    // CompositingManager::Client
    virtual void releaseBuffer(uint32_t) override;
    virtual void frameComplete() override;

    void renderLayerTree();
    void scheduleDisplayImmediately();
    void didChangeVisibleRect() override;

    bool ensureGLContext();
    WebCore::GLContext* glContext();
    SimpleViewportController* viewportController() { return m_viewportController.get(); }

    void callOnCompositingThread(std::function<void()>&&);
    void createCompositingThread();
    void runCompositingThread();
    void terminateCompositingThread();
    static void compositingThreadEntry(void*);

    Client* m_client;
    RefPtr<CoordinatedGraphicsScene> m_scene;
    std::unique_ptr<SimpleViewportController> m_viewportController;

#if PLATFORM(WPE)
#if PLATFORM(WAYLAND)
    std::unique_ptr<WebCore::WaylandSurface> m_surface;
#else
    std::unique_ptr<WebCore::PlatformDisplayWPE::Surface> m_surface;
#endif
#endif
    std::unique_ptr<WebCore::GLContext> m_context;

    WebCore::IntSize m_viewportSize;
    float m_deviceScaleFactor;
    uint64_t m_nativeSurfaceHandle;

    ThreadIdentifier m_threadIdentifier;
    Condition m_initializeRunLoopCondition;
    Lock m_initializeRunLoopConditionLock;
    Condition m_terminateRunLoopCondition;
    Lock m_terminateRunLoopConditionLock;

    std::unique_ptr<CompositingManager> m_compositingManager;

#if USE(REQUEST_ANIMATION_FRAME_DISPLAY_MONITOR)
    class DisplayRefreshMonitor : public WebCore::DisplayRefreshMonitor {
    public:
        DisplayRefreshMonitor(ThreadedCompositor&);

        virtual bool requestRefreshCallback() override;

        bool requiresDisplayRefreshCallback();
        void dispatchDisplayRefreshCallback();
        void invalidate();

    private:
        void displayRefreshCallback();
        RunLoop::Timer<DisplayRefreshMonitor> m_displayRefreshTimer;
        ThreadedCompositor* m_compositor;
    };
    RefPtr<DisplayRefreshMonitor> m_displayRefreshMonitor;
#endif

    class CompositingRunLoop {
        WTF_MAKE_NONCOPYABLE(CompositingRunLoop);
        WTF_MAKE_FAST_ALLOCATED;
    public:
        CompositingRunLoop(std::function<void ()>&&);

        void callOnCompositingRunLoop(std::function<void ()>&&);

        bool isActive();
        void scheduleUpdate();
        void stopUpdates();

        void updateCompleted();

        RunLoop& runLoop() { return m_runLoop; }

    private:
        enum class UpdateState {
            Completed,
            InProgress,
            PendingAfterCompletion,
        };

        void updateTimerFired();

        RunLoop& m_runLoop;
        RunLoop::Timer<CompositingRunLoop> m_updateTimer;
        std::function<void ()> m_updateFunction;
        Atomic<UpdateState> m_updateState;
    };
    std::unique_ptr<CompositingRunLoop> m_compositingRunLoop;

    Atomic<bool> m_clientRendersNextFrame;
    Atomic<bool> m_coordinateUpdateCompletionWithClient;
//KEYBOARD SUPPORT
#if PLATFORM(WPE) && PLATFORM(WAYLAND)
    WebPage& webpage;
#endif
//KEYBOARD SUPPORT      
};

} // namespace WebKit

#endif

#endif // ThreadedCompositor_h
