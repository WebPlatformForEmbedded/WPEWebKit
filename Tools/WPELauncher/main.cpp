#include <WebKit/WKContext.h>
#include <WebKit/WKFramePolicyListener.h>
#include <WebKit/WKPageGroup.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKRetainPtr.h>
#include <WebKit/WKString.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKView.h>
#include <glib.h>
#include <getopt.h>

static WKViewRef createView(WKPageConfigurationRef);

static WKPageUIClientV6 createPageUIClient()
{
    WKPageUIClientV6 pageUIClient = {
        { 6, nullptr },
        nullptr, // createNewPage_deprecatedForUseWithV0
        nullptr, // showPage
        nullptr, // close
        nullptr, // takeFocus
        nullptr, // focus
        nullptr, // unfocus
        nullptr, // runJavaScriptAlert_deprecatedForUseWithV0
        nullptr, // runJavaScriptConfirm_deprecatedForUseWithV0
        nullptr, // runJavaScriptPrompt_deprecatedForUseWithV0
        nullptr, // setStatusText
        nullptr, // mouseDidMoveOverElement_deprecatedForUseWithV0
        nullptr, // missingPluginButtonClicked_deprecatedForUseWithV0
        nullptr, // didNotHandleKeyEvent
        nullptr, // didNotHandleWheelEvent
        nullptr, // toolbarsAreVisible
        nullptr, // setToolbarsAreVisible
        nullptr, // menuBarIsVisible
        nullptr, // setMenuBarIsVisible
        nullptr, // statusBarIsVisible
        nullptr, // setStatusBarIsVisible
        nullptr, // isResizable
        nullptr, // setIsResizable
        nullptr, // getWindowFrame
        nullptr, // setWindowFrame
        nullptr, // runBeforeUnloadConfirmPanel
        nullptr, // didDraw
        nullptr, // pageDidScroll
        nullptr, // exceededDatabaseQuota
        nullptr, // runOpenPanel
        nullptr, // decidePolicyForGeolocationPermissionRequest
        nullptr, // headerHeight
        nullptr, // footerHeight
        nullptr, // drawHeader
        nullptr, // drawFooter
        nullptr, // printFrame
        nullptr, // runModal
        nullptr, // unused1
        nullptr, // saveDataToFileInDownloadsFolder
        nullptr, // shouldInterruptJavaScript_unavailable
        nullptr, // createNewPage_deprecatedForUseWithV1
        nullptr, // mouseDidMoveOverElement
        nullptr, // decidePolicyForNotificationPermissionRequest
        nullptr, // unavailablePluginButtonClicked_deprecatedForUseWithV1
        nullptr, // showColorPicker
        nullptr, // hideColorPicker
        nullptr, // unavailablePluginButtonClicked
        nullptr, // pinnedStateDidChange
        nullptr, // didBeginTrackingPotentialLongMousePress
        nullptr, // didRecognizeLongMousePress
        nullptr, // didCancelTrackingPotentialLongMousePress
        nullptr, // isPlayingAudioDidChange
        nullptr, // decidePolicyForUserMediaPermissionRequest
        nullptr, // didClickAutoFillButton
        nullptr, // runJavaScriptAlert
        nullptr, // runJavaScriptConfirm
        nullptr, // runJavaScriptPrompt
        nullptr, // mediaSessionMetadataDidChange
        // createNewPage
        [](WKPageRef, WKPageConfigurationRef pageConfiguration, WKNavigationActionRef, WKWindowFeaturesRef, const void*) -> WKPageRef {
            auto view = createView(pageConfiguration);
            auto page = WKViewGetPage(view);
            WKRetain(page);
            return page;
        },
    };
    return pageUIClient;
}

static WKPageNavigationClientV0 createPageNavigationClient()
{
    WKPageNavigationClientV0 navigationClient = {
        { 0, nullptr },
        // decidePolicyForNavigationAction
        [](WKPageRef, WKNavigationActionRef, WKFramePolicyListenerRef listener, WKTypeRef, const void*) {
            WKFramePolicyListenerUse(listener);
        },
        // decidePolicyForNavigationResponse
        [](WKPageRef, WKNavigationResponseRef, WKFramePolicyListenerRef listener, WKTypeRef, const void*) {
            WKFramePolicyListenerUse(listener);
        },
        nullptr, // decidePolicyForPluginLoad
        nullptr, // didStartProvisionalNavigation
        nullptr, // didReceiveServerRedirectForProvisionalNavigation
        nullptr, // didFailProvisionalNavigation
        nullptr, // didCommitNavigation
        nullptr, // didFinishNavigation
        nullptr, // didFailNavigation
        nullptr, // didFailProvisionalLoadInSubframe
        nullptr, // didFinishDocumentLoad
        nullptr, // didSameDocumentNavigation
        nullptr, // renderingProgressDidChange
        nullptr, // canAuthenticateAgainstProtectionSpace
        nullptr, // didReceiveAuthenticationChallenge
        nullptr, // webProcessDidCrash
        nullptr, // copyWebCryptoMasterKey
        nullptr, // didBeginNavigationGesture
        nullptr, // willEndNavigationGesture
        nullptr, // didEndNavigationGesture
        nullptr, // didRemoveNavigationGestureSnapshot
    };
    return navigationClient;
}

static WKViewRef createView(WKPageConfigurationRef pageConfiguration)
{
    auto view = WKViewCreate(pageConfiguration);
    auto page = WKViewGetPage(view);

    auto pageUIClient = createPageUIClient();
    WKPageSetPageUIClient(page, &pageUIClient.base);

    auto pageNavigationClient = createPageNavigationClient();
    WKPageSetPageNavigationClient(page, &pageNavigationClient.base);

    return view;
}

class LauncherOptions {
public:
    LauncherOptions(int argc, char* const* argv);
    const char* getUserAgent() const { return m_userAgent; };
    const char* getUrl() const { return m_url; };
private:
    const char* m_userAgent;
    const char* m_url;
};

const char* defaultUrl = "http://www.webkit.org/blog-files/3d-transforms/poster-circle.html";

LauncherOptions::LauncherOptions(int argc, char* const* argv) :
        m_userAgent(0),
        m_url(defaultUrl)
{
    int c;

    while (1)
    {
        static struct option long_options[] =
        {
            {"user_agent",    required_argument, 0, 'a'},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long (argc, argv, "a:", long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c)
        {
          case 'a':
            fprintf (stderr, "Setting User Agent to '%s'\n", optarg);
            m_userAgent = optarg;
            break;
          case 0:
          case '?':
            break;

          default:
            abort ();
        }
    }

    /* Any remaining command line argument (not options). */
    if (optind < argc)
    {
        m_url = argv[optind];
        fprintf(stderr, "Setting initial URL: %s\n", m_url);
    }
}

int main(int argc, char* argv[])
{
    GMainLoop* loop = g_main_loop_new(g_main_context_default(), FALSE);

    auto context = adoptWK(WKContextCreate());
    auto pageGroupIdentifier = adoptWK(WKStringCreateWithUTF8CString("WPEPageGroup"));
    auto pageGroup = adoptWK(WKPageGroupCreateWithIdentifier(pageGroupIdentifier.get()));
    auto pageConfiguration = adoptWK(WKPageConfigurationCreate());
    WKPageConfigurationSetContext(pageConfiguration.get(), context.get());
    WKPageConfigurationSetPageGroup(pageConfiguration.get(), pageGroup.get());

    auto view = adoptWK(createView(pageConfiguration.get()));

    LauncherOptions opts(argc, argv);

    auto shellURL = adoptWK(WKURLCreateWithUTF8CString(opts.getUrl()));

    WKPageRef page = WKViewGetPage(view.get());

    if (opts.getUserAgent())
    {
        auto userAgent = adoptWK(WKStringCreateWithUTF8CString(opts.getUserAgent()));
        WKPageSetCustomUserAgent(page, userAgent.get());
    }

    WKPageLoadURL(page, shellURL.get());

    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    return 0;
}
