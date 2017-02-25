#include "config.h"
#include "WPEViewClient.h"

#include "WPEView.h"
#include "WKAPICast.h"
#include "WKBase.h"

using namespace WebKit;

namespace WKWPE {

void ViewClient::frameDisplayed(View& view)
{
    if (m_client.frameDisplayed)
        m_client.frameDisplayed(toAPI(&view), m_client.base.clientInfo);
}

void ViewClient::viewbackendStopped(View& view)
{
    if (m_client.backendStopped)
        m_client.backendStopped(toAPI(&view), m_client.base.clientInfo);
}

} // namespace WKWPE
