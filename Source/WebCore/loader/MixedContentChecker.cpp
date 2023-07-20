/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2013-2023 Apple Inc. All rights reserved.
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
#include "DocumentInlines.h"
#include "FrameDestructionObserverInlines.h"
#include "FrameLoader.h"
#include "LegacySchemeRegistry.h"
#include "LocalFrame.h"
#include "LocalFrameLoaderClient.h"
#include "Quirks.h"
#include "SecurityOrigin.h"

#if PLATFORM(IOS_FAMILY)
#include <wtf/cocoa/RuntimeApplicationChecksCocoa.h>
#endif

namespace WebCore {

static WTF::Vector<WTF::KeyValuePair<WTF::String, WTF::String>> m_whitelist = {};

static bool wildcardMatch(const String& pattern, const String& url)
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

static bool isWhitelisted(const String& origin, const String& domain)
{
    for (auto kvPair : m_whitelist) {
        if (wildcardMatch(kvPair.key, origin) && wildcardMatch(kvPair.value, domain)) {
            return true;
        }
    }
    return false;
}

static bool isMixedContent(const Document& document, const WTF::URL& url)
{
    // FIXME: Use document.isSecureContext(), instead of comparing against "https" scheme, when all ports stop using loopback in LayoutTests
    // sandboxed iframes have an opaque origin so we should perform the mixed content check considering the origin
    // the iframe would have had if it were not sandboxed.
    if (document.securityOrigin().protocol() == "https"_s || (document.securityOrigin().isOpaque() && document.url().protocolIs("https"_s)))
        return !SecurityOrigin::isSecure(url);

    return false;
}

static bool foundMixedContentInFrameTree(const LocalFrame& frame, const URL& url)
{
    RefPtr document = frame.document();

    while (document) {
        if (isMixedContent(*document, url))
            return true;

        RefPtr frame = document->frame();
        if (!frame || frame->isMainFrame())
            break;

        RefPtr abstractParentFrame = frame->tree().parent();
        RELEASE_ASSERT_WITH_MESSAGE(abstractParentFrame, "Should never have a parentless non main frame");
        if (auto* parentFrame = dynamicDowncast<LocalFrame>(abstractParentFrame.get()))
            document = parentFrame->document();
        else {
            // FIXME: <rdar://116259764> Make mixed content checks work correctly with site isolated iframes.
            document = nullptr;
        }
    }

    return false;
}

static void logConsoleWarning(const LocalFrame& frame, bool allowed, ASCIILiteral action, const URL& target)
{
    auto errorString = allowed ? " was allowed to "_s : " was not allowed to "_s;
    auto message = makeString((allowed ? ""_s : "[blocked] "_s), "The page at "_s, frame.document()->url().stringCenterEllipsizedToLength(), errorString, action, " insecure content from "_s, target.stringCenterEllipsizedToLength(), ".\n"_s);
    frame.protectedDocument()->addConsoleMessage(MessageSource::Security, MessageLevel::Warning, message);
}

static void logConsoleWarningForUpgrade(const LocalFrame& frame, bool blocked, const URL& target, bool isUpgradingIPAddressAndLocalhostEnabled)
{
    auto isUpgradingLocalhostDisabled = !isUpgradingIPAddressAndLocalhostEnabled && SecurityOrigin::isLocalhostAddress(target.host());
    ASCIILiteral errorString = [&] {
    if (blocked)
        return "blocked and must"_s;
    if (isUpgradingLocalhostDisabled)
        return "not upgraded to HTTPS and must be served from the local host."_s;
    return "automatically upgraded and should"_s;
    }();

    auto message = makeString((!blocked ? ""_s : "[blocked] "_s), "The page at "_s, frame.document()->url().stringCenterEllipsizedToLength(), " requested insecure content from "_s, target.stringCenterEllipsizedToLength(), ". This content was "_s, errorString, !isUpgradingLocalhostDisabled ? " be served over HTTPS.\n"_s : "\n"_s);
    frame.document()->addConsoleMessage(MessageSource::Security, MessageLevel::Warning, message);
}

static bool isUpgradeMixedContentEnabled(Document& document)
{
#if PLATFORM(IOS_FAMILY)
    static bool shouldBlockOptionallyBlockableMixedContent = linkedOnOrAfterSDKWithBehavior(SDKAlignedBehavior::BlockOptionallyBlockableMixedContent);
    return shouldBlockOptionallyBlockableMixedContent && document.settings().upgradeMixedContentEnabled();
#else
    return document.settings().upgradeMixedContentEnabled();
#endif
}

static bool frameAndAncestorsCanDisplayInsecureContent(LocalFrame& frame, MixedContentChecker::ContentType type, const URL& url)
{
    if (!frame.document() || isUpgradeMixedContentEnabled(*frame.document()))
        return true;

    if (!foundMixedContentInFrameTree(frame, url))
        return true;

    RefPtr document = frame.document();
    if (isWhitelisted(document->securityOrigin().toString(), url.protocolHostAndPort())) {
        logConsoleWarning(frame, true, "display"_s, url);
        return true;
    }

    if (!document->checkedContentSecurityPolicy()->allowRunningOrDisplayingInsecureContent(url))
        return false;

    bool allowed = !document->isStrictMixedContentMode() && (frame.settings().allowDisplayOfInsecureContent() || type == MixedContentChecker::ContentType::ActiveCanWarn) && !frame.document()->geolocationAccessed();
    logConsoleWarning(frame, allowed, "display"_s, url);

    if (allowed) {
        document->setFoundMixedContent(SecurityContext::MixedContentType::Inactive);
        frame.checkedLoader()->client().didDisplayInsecureContent();
    }

    return allowed;
}

bool MixedContentChecker::frameAndAncestorsCanRunInsecureContent(LocalFrame& frame, SecurityOrigin& securityOrigin, const URL& url, ShouldLogWarning shouldLogWarning)
{
    if (!frame.document() || isUpgradeMixedContentEnabled(*frame.document()))
        return true;

    if (!foundMixedContentInFrameTree(frame, url))
        return true;

    if (isWhitelisted(securityOrigin.toString(), url.protocolHostAndPort())) {
        logConsoleWarning(frame, true, "run"_s, url);
        return true;
    }

    RefPtr document = frame.document();
    if (!document->checkedContentSecurityPolicy()->allowRunningOrDisplayingInsecureContent(url))
        return false;

    bool allowed = !document->isStrictMixedContentMode() && frame.settings().allowRunningOfInsecureContent() && !frame.document()->geolocationAccessed() && !frame.document()->secureCookiesAccessed();
    if (LIKELY(shouldLogWarning == ShouldLogWarning::Yes))
        logConsoleWarning(frame, allowed, "run"_s, url);

    if (allowed) {
        document->setFoundMixedContent(SecurityContext::MixedContentType::Active);
        frame.checkedLoader()->client().didRunInsecureContent(securityOrigin);
    }

    return allowed;
}

static bool destinationIsImageAudioOrVideo(FetchOptions::Destination destination)
{
    return destination == FetchOptions::Destination::Audio || destination == FetchOptions::Destination::Image || destination == FetchOptions::Destination::Video;
}

bool MixedContentChecker::shouldUpgradeInsecureContent(LocalFrame& frame, IsUpgradable isUpgradable, const URL& url, FetchOptions::Mode mode, FetchOptions::Destination destination, Initiator initiator)
{
    RefPtr document = frame.document();
    if (!document || !isUpgradeMixedContentEnabled(*document) || isUpgradable != IsUpgradable::Yes)
        return false;

    // https://www.w3.org/TR/mixed-content/#upgrade-algorithm
    // Editor’s Draft, 23 February 2023
    // 4.1. Upgrade a mixed content request to a potentially trustworthy URL, if appropriate
    //
    // The request should not be upgraded if:
    // 4.1.3 § 4.3 Does settings prohibit mixed security contexts? returns "Does Not Restrict Mixed Security Contents" when applied to request’s client.
    if (!foundMixedContentInFrameTree(frame, url))
        return false;

    auto shouldUpgradeIPAddressAndLocalhostForTesting = document->settings().iPAddressAndLocalhostMixedContentUpgradeTestingEnabled();

    // The request's URL is not upgraded in the following cases.
    // 4.1.1 request’s URL is a potentially trustworthy URL.
    if (url.protocolIs("https"_s)
        // 4.1.2 request’s URL’s host is an IP address.
        || (!shouldUpgradeIPAddressAndLocalhostForTesting && URL::hostIsIPAddress(url.host()))
        // 4.1.4 request’s destination is not "image", "audio", or "video".
        || (!destinationIsImageAudioOrVideo(destination))
        // 4.1.5 request’s destination is "image" and request’s initiator is "imageset".
        || (destination == FetchOptions::Destination::Image && initiator == Initiator::Imageset)
        // and CORS is excluded
        || (mode == FetchOptions::Mode::Cors && !(document->quirks().needsRelaxedCorsMixedContentCheckQuirk() && destinationIsImageAudioOrVideo(destination))))
        return false;
    logConsoleWarningForUpgrade(frame, /* blocked */ false, url, shouldUpgradeIPAddressAndLocalhostForTesting);
    return true;
}

static bool shouldBlockInsecureContent(LocalFrame& frame, const URL& url, MixedContentChecker::IsUpgradable isUpgradable)
{
    RefPtr document = frame.document();
    if (!document || !isUpgradeMixedContentEnabled(*document))
        return false;
    if (!foundMixedContentInFrameTree(frame, url))
        return false;
    if ((LegacySchemeRegistry::schemeIsHandledBySchemeHandler(url.protocol()) || SecurityOrigin::isLocalhostAddress(url.host())) && isUpgradable == MixedContentChecker::IsUpgradable::Yes)
        return false;
    logConsoleWarningForUpgrade(frame, /* blocked */ true, url, document->settings().iPAddressAndLocalhostMixedContentUpgradeTestingEnabled());
    return true;
}

bool MixedContentChecker::shouldBlockRequestForDisplayableContent(LocalFrame& frame, const URL& url, ContentType type, IsUpgradable isUpgradable)
{
    if (shouldBlockInsecureContent(frame, url, isUpgradable))
        return true;
    return !frameAndAncestorsCanDisplayInsecureContent(frame, type, url);
}

bool MixedContentChecker::shouldBlockRequestForRunnableContent(LocalFrame& frame, SecurityOrigin& securityOrigin, const URL& url, ShouldLogWarning shouldLogWarning)
{
    if (shouldBlockInsecureContent(frame, url, IsUpgradable::No))
        return true;
    return !frameAndAncestorsCanRunInsecureContent(frame, securityOrigin, url, shouldLogWarning);
}

void MixedContentChecker::checkFormForMixedContent(LocalFrame& frame, const URL& url)
{
    // Unconditionally allow javascript: URLs as form actions as some pages do this and it does not introduce
    // a mixed content issue.
    if (url.protocolIsJavaScript())
        return;

    if (!isMixedContent(*frame.document(), url))
        return;

    auto message = makeString("The page at "_s, frame.document()->url().stringCenterEllipsizedToLength(), " contains a form which targets an insecure URL "_s, url.stringCenterEllipsizedToLength(), ".\n"_s);
    frame.protectedDocument()->addConsoleMessage(MessageSource::Security, MessageLevel::Warning, message);

    frame.checkedLoader()->client().didDisplayInsecureContent();
}
void MixedContentChecker::addMixedContentWhitelistEntry(const String& origin, const String& domain)
{
    m_whitelist.append(makeKeyValuePair(origin, domain));
}

void MixedContentChecker::removeMixedContentWhitelistEntry(const String& origin, const String& domain)
{
    for (size_t i = 0; i < m_whitelist.size(); i++) {
        if (m_whitelist[i].key == origin && m_whitelist[i].value == domain) {
            m_whitelist.remove(i);
            break;
        }
    }
}

void MixedContentChecker::resetMixedContentWhitelist()
{
    m_whitelist.clear();

}
} // namespace WebCore

