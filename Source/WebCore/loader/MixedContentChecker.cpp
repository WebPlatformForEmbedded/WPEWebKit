/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2013-2017 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "MixedContentChecker.h"

#include "ContentSecurityPolicy.h"
#include "Document.h"
#include "Frame.h"
#include "FrameDestructionObserverInlines.h"
#include "FrameLoader.h"
#include "FrameLoaderClient.h"
#include "SecurityOrigin.h"
#include "SecurityPolicy.h"
#include "Settings.h"
#include <wtf/text/CString.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

WTF::Vector<WTF::KeyValuePair<WTF::String, WTF::String>> MixedContentChecker::m_whitelist = {};

// static
bool MixedContentChecker::isMixedContent(SecurityOrigin& securityOrigin, const URL& url)
{
    if (securityOrigin.protocol() != "https"_s)
        return false; // We only care about HTTPS security origins.

    // We're in a secure context, so |url| is mixed content if it's insecure.
    return !SecurityOrigin::isSecure(url);
}

static void logWarning(const Frame& frame, bool allowed, ASCIILiteral action, const URL& target)
{
    const char* errorString = allowed ? " was allowed to " : " was not allowed to ";
    auto message = makeString((allowed ? "" : "[blocked] "), "The page at ", frame.document()->url().stringCenterEllipsizedToLength(), errorString, action, " insecure content from ", target.stringCenterEllipsizedToLength(), ".\n");
    frame.document()->addConsoleMessage(MessageSource::Security, MessageLevel::Warning, message);
}

bool MixedContentChecker::canDisplayInsecureContent(Frame& frame, SecurityOrigin& securityOrigin, ContentType type, const URL& url, AlwaysDisplayInNonStrictMode alwaysDisplayInNonStrictMode)
{
    if (!isMixedContent(securityOrigin, url))
        return true;

    if (isWhitelisted(securityOrigin.toString(), url.protocolHostAndPort())) {
        logWarning(frame, true, "display"_s, url);
        return true;
    }

    if (!frame.document()->contentSecurityPolicy()->allowRunningOrDisplayingInsecureContent(url))
        return false;

    if (SecurityPolicy::isAccessAllowed(securityOrigin, url)) {
        logWarning(frame, true, "display"_s, url);
        return true;
    }

    bool isStrictMode = frame.document()->isStrictMixedContentMode();
    if (!isStrictMode && alwaysDisplayInNonStrictMode == AlwaysDisplayInNonStrictMode::Yes)
        return true;

    bool allowed = !isStrictMode && (frame.settings().allowDisplayOfInsecureContent() || type == ContentType::ActiveCanWarn) && !frame.document()->geolocationAccessed();
    logWarning(frame, allowed, "display"_s, url);

    if (allowed) {
        frame.document()->setFoundMixedContent(SecurityContext::MixedContentType::Inactive);
        frame.loader().client().didDisplayInsecureContent();
    }

    return allowed;
}

bool MixedContentChecker::canRunInsecureContent(Frame& frame, SecurityOrigin& securityOrigin, const URL& url)
{
    if (!isMixedContent(securityOrigin, url))
        return true;

    if (isWhitelisted(securityOrigin.toString(), url.protocolHostAndPort())) {
        logWarning(frame, true, "run"_s, url);
        return true;
    }

    if (!frame.document()->contentSecurityPolicy()->allowRunningOrDisplayingInsecureContent(url))
        return false;

    if (SecurityPolicy::isAccessAllowed(securityOrigin, url)) {
        logWarning(frame, true, "run"_s, url);
        return true;
    }

    bool allowed = !frame.document()->isStrictMixedContentMode() && frame.settings().allowRunningOfInsecureContent() && !frame.document()->geolocationAccessed() && !frame.document()->secureCookiesAccessed();
    logWarning(frame, allowed, "run"_s, url);

    if (allowed) {
        frame.document()->setFoundMixedContent(SecurityContext::MixedContentType::Active);
        frame.loader().client().didRunInsecureContent(securityOrigin, url);
    }

    return allowed;
}

void MixedContentChecker::checkFormForMixedContent(Frame& frame, SecurityOrigin& securityOrigin, const URL& url)
{
    // Unconditionally allow javascript: URLs as form actions as some pages do this and it does not introduce
    // a mixed content issue.
    if (url.protocolIsJavaScript())
        return;

    if (!isMixedContent(securityOrigin, url))
        return;

    auto message = makeString("The page at ", frame.document()->url().stringCenterEllipsizedToLength(), " contains a form which targets an insecure URL ", url.stringCenterEllipsizedToLength(), ".\n");
    frame.document()->addConsoleMessage(MessageSource::Security, MessageLevel::Warning, message);

    frame.loader().client().didDisplayInsecureContent();
}

std::optional<String> MixedContentChecker::checkForMixedContentInFrameTree(const Frame& frame, const URL& url)
{
    auto* document = frame.document();

    while (document) {
        RELEASE_ASSERT_WITH_MESSAGE(document->frame(), "An unparented document tried to connect to a websocket with url: %s", url.string().utf8().data());
        
        auto* frame = document->frame();
        if (isMixedContent(document->securityOrigin(), url))
            return makeString("The page at ", document->url().stringCenterEllipsizedToLength(), " was blocked from connecting insecurely to ", url.stringCenterEllipsizedToLength(), " either because the protocol is insecure or the page is embedded from an insecure page.");

        if (frame->isMainFrame())
            break;

        frame = frame->tree().parent();
        RELEASE_ASSERT_WITH_MESSAGE(frame, "Should never have a parentless non main frame");
        document = frame->document();
    }
    
    return std::nullopt;
}

bool wildcardMatch(const String& pattern, const String& url)
{
    int patternLen = pattern.length();
    int patternPos = 0;
    int urlLen = url.length();
    int urlPos = 0;
    int wildcardPos = -1;
    int wildcardMatchEnd = 0;

    while (urlPos < urlLen) {
        if (patternPos < patternLen && pattern[patternPos] == url[urlPos]) {
            // characters match
            patternPos++;
            urlPos++;
        } else if (patternPos < patternLen && pattern[patternPos] == '*') {
            // mark wildcard position, start matching the rest of the pattern
            wildcardPos = patternPos;
            wildcardMatchEnd = urlPos;
            patternPos++;
        } else if (wildcardPos != -1) {
            // no match, but we have a wildcard - assume wildcard handles a match to this position,
            // revert patternPos to after last *
            patternPos = wildcardPos + 1;
            wildcardMatchEnd++;
            urlPos = wildcardMatchEnd;
        } else {
            // no match, no wildcard - pattern does not match
            return false;
        }
    }

    // url matches so far, and we're at the end of it
    // skip any remaining wildcards
    while (patternPos < patternLen && pattern[patternPos] == '*') {
        patternPos++;
    }
    // if we're at the end of pattern, that's a match
    // otherwise, the remaining part of the pattern can't be matched
    return patternPos == patternLen;
}

//static
bool MixedContentChecker::isWhitelisted(const String& origin, const String& domain)
{
    for (auto kvPair : m_whitelist) {
        if (wildcardMatch(kvPair.key, origin) && wildcardMatch(kvPair.value, domain)) {
            return true;
        }
    }
    return false;
}

//static
void MixedContentChecker::addMixedContentWhitelistEntry(const String& origin, const String& domain)
{
    MixedContentChecker::m_whitelist.append(makeKeyValuePair(origin, domain));
}

//static
void MixedContentChecker::removeMixedContentWhitelistEntry(const String& origin, const String& domain)
{
    for (size_t i = 0; i < MixedContentChecker::m_whitelist.size(); i++) {
        if (MixedContentChecker::m_whitelist[i].key == origin && MixedContentChecker::m_whitelist[i].value == domain) {
            MixedContentChecker::m_whitelist.remove(i);
            break;
        }
    }
}

//static
void MixedContentChecker::resetMixedContentWhitelist()
{
    MixedContentChecker::m_whitelist.clear();
}

} // namespace WebCore
