/*
 * Copyright (C) 2003, 2006, 2013, 2017 Apple Inc.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "Logging.h"

#include <wtf/text/WTFString.h>

#if PLATFORM(COCOA)
#include <notify.h>
#include <wtf/BlockPtr.h>
#elif PLATFORM(WPE)
#include <glib.h>
#include <gio/gio.h>
#include <wtf/FileSystem.h>
#include <wtf/glib/GRefPtr.h>
#endif

namespace PAL {

void registerNotifyCallback(ASCIILiteral notifyID, Function<void()>&& callback)
{
#if PLATFORM(COCOA)
    int token;
    notify_register_dispatch(notifyID.characters(), &token, dispatch_get_main_queue(), makeBlockPtr([callback = WTFMove(callback)](int) {
        callback();
    }).get());
#elif PLATFORM(WPE)
    using namespace FileSystem;

    // Triggers callback with "touch <user config dir>/notify/<notifyID>".
    CString notifyFilePath = fileSystemRepresentation(pathByAppendingComponents(StringView::fromLatin1(g_get_user_config_dir()), {"notify"_s, notifyID}));
    GRefPtr<GFile> notifyFile = adoptGRef(g_file_new_for_path(notifyFilePath.data()));
    GFileMonitor* monitor = g_file_monitor_file(notifyFile.get(), G_FILE_MONITOR_NONE, nullptr, nullptr);

    g_signal_connect(monitor, "changed", G_CALLBACK(+[](GFileMonitor*, GFile* file, GFile*, GFileMonitorEvent event, WTF::Function<void()> *callback) {
        const char *path = g_file_get_path(file);
        if ((nullptr == path) || !g_file_test(path, G_FILE_TEST_EXISTS)) {
            return;
        }
        if ((G_FILE_MONITOR_EVENT_CREATED == event) || (G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED == event)) {
            (*callback)();
        }
        // We are not releasing the allocated memory for the Function object, as this code will get called each time the signal is raised
        // No "unregisterNotifyCallback" available to do proper cleanup (signal disconnection and memory release)
    }), new WTF::Function<void()>(WTFMove(callback)));
#else
    UNUSED_PARAM(notifyID);
    UNUSED_PARAM(callback);
#endif
}

#if !LOG_DISABLED || !RELEASE_LOG_DISABLED

#define DEFINE_PAL_LOG_CHANNEL(name) DEFINE_LOG_CHANNEL(name, LOG_CHANNEL_WEBKIT_SUBSYSTEM)
PAL_LOG_CHANNELS(DEFINE_PAL_LOG_CHANNEL)

#endif // !LOG_DISABLED || !RELEASE_LOG_DISABLED

} // namespace WebCore
