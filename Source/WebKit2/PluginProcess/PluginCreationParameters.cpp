/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
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
#include "PluginCreationParameters.h"
#include <syslog.h>

#if ENABLE(NETSCAPE_PLUGIN_API)

#include "ArgumentCoders.h"

namespace WebKit {

PluginCreationParameters::PluginCreationParameters()
    : pluginInstanceID(0)
    , windowNPObjectID(0)
    , contentsScaleFactor(1)
    , isPrivateBrowsingEnabled(false)
    , isMuted(false)
    , asynchronousCreationIncomplete(false)
    , artificialPluginInitializationDelayEnabled(false)
    , isAcceleratedCompositingEnabled(false)
{
}

void PluginCreationParameters::encode(IPC::Encoder& encoder) const
{
    encoder << pluginInstanceID;
    encoder << windowNPObjectID;
    encoder << parameters;
    encoder << userAgent;
    encoder << contentsScaleFactor;
    encoder << isPrivateBrowsingEnabled;
    encoder << isMuted;
    encoder << asynchronousCreationIncomplete;
    encoder << artificialPluginInitializationDelayEnabled;
    encoder << isAcceleratedCompositingEnabled;
}

bool PluginCreationParameters::decode(IPC::Decoder& decoder, PluginCreationParameters& result)
{
#define return_false syslog(LOG_INFO, "decode fail: %s:%d", __FILE__, __LINE__)

    if (!decoder.decode(result.pluginInstanceID) || !result.pluginInstanceID)
        return_false;

    if (!decoder.decode(result.windowNPObjectID))
        return_false;

    if (!decoder.decode(result.parameters))
        return_false;

    if (!decoder.decode(result.userAgent))
        return_false;

    if (!decoder.decode(result.contentsScaleFactor))
        return_false;

    if (!decoder.decode(result.isPrivateBrowsingEnabled))
        return_false;

    if (!decoder.decode(result.isMuted))
        return_false;

    if (!decoder.decode(result.asynchronousCreationIncomplete))
        return_false;

    if (!decoder.decode(result.artificialPluginInitializationDelayEnabled))
        return_false;

    if (!decoder.decode(result.isAcceleratedCompositingEnabled))
        return_false;
    syslog(LOG_INFO, "decode succeed");
    return true;
}


} // namespace WebKit

#endif // ENABLE(NETSCAPE_PLUGIN_API)
