/*
 * Copyright (C) 2011, 2014 Igalia S.L.
 * Copyright (C) 2011 Apple Inc.
 * Copyright (C) 2012 Samsung Electronics
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "PluginProcessProxy.h"

#if ENABLE(PLUGIN_PROCESS)

#include "PluginProcessCreationParameters.h"
#include "ProcessExecutablePath.h"
#include <WebCore/FileSystem.h>
#include <sys/wait.h>
#include <wtf/text/CString.h>
#include <wtf/text/WTFString.h>
#include <syslog.h>

#if PLATFORM(GTK) || PLATFORM(EFL) || PLATFORM(WPE)
#include <glib.h>
#include <wtf/glib/GUniquePtr.h>
#endif

#if PLATFORM(GTK) ||PLATFORM(WPE)
#include "Module.h"
#endif

using namespace WebCore;

namespace WebKit {

void PluginProcessProxy::platformGetLaunchOptions(ProcessLauncher::LaunchOptions& launchOptions, const PluginProcessAttributes& pluginProcessAttributes)
{
    syslog(LOG_INFO, "FILE= %s, FUN=%s", __FILE__, __FUNCTION__);
    launchOptions.processType = ProcessLauncher::ProcessType::Plugin64;

    launchOptions.extraInitializationData.add("plugin-path", pluginProcessAttributes.moduleInfo.path);
      syslog(LOG_INFO, "+++++platformGetLauchOption. set to pluign64... FILE= %s, FUN=%s", __FILE__, __FUNCTION__);
#if PLATFORM(GTK)
    if (pluginProcessAttributes.moduleInfo.requiresGtk2)
        launchOptions.extraInitializationData.add("requires-gtk2", emptyString());
#endif
}

void PluginProcessProxy::platformInitializePluginProcess(PluginProcessCreationParameters&)
{
}

#if PLATFORM(GTK)
static bool pluginRequiresGtk2(const String& pluginPath)
{
    std::unique_ptr<Module> module = std::make_unique<Module>(pluginPath);
    if (!module->load())
        return false;
    return module->functionPointer<gpointer>("gtk_object_get_type");
}
#endif

#if PLUGIN_ARCHITECTURE(X11) || PLUGIN_ARCHITECTURE(WayLand)
bool PluginProcessProxy::scanPlugin(const String& pluginPath, RawPluginMetaData& result)
{
	syslog(LOG_INFO, "FILE= %s, FUN=%s", __FILE__, __FUNCTION__);
#if PLATFORM(GTK) || PLATFORM(EFL) || PLATFORM(WPE)
    String pluginProcessPath = executablePathOfPluginProcess();
    syslog(LOG_INFO, "FILE= %s, FUN=%s, pluingProcessPath=%s", __FILE__, __FUNCTION__, pluginProcessPath.utf8().data());
#if PLATFORM(GTK)
    bool requiresGtk2 = pluginRequiresGtk2(pluginPath);
    if (requiresGtk2) {
#if ENABLE(PLUGIN_PROCESS_GTK2)
        pluginProcessPath.append('2');
        if (!fileExists(pluginProcessPath))
            return false;
#else
        return false;
#endif
    }
#endif

    CString binaryPath = fileSystemRepresentation(pluginProcessPath);
    CString pluginPathCString = fileSystemRepresentation(pluginPath);

    char* argv[4];
    argv[0] = const_cast<char*>(binaryPath.data());
    argv[1] = const_cast<char*>("-scanPlugin");
    argv[2] = const_cast<char*>(pluginPathCString.data());
    argv[3] = nullptr;
    syslog (LOG_INFO, "#####After filesystemRepresation path and path argv0 = %s, argv1=%s, argv2=%s,argv3=%s", argv[0], argv[1],argv[2],argv[3]);

    syslog(LOG_INFO, "######argv[0] ==%s", argv[0]);
    syslog(LOG_INFO, "######argv[1] ==%s", argv[1]);
    syslog(LOG_INFO, "######argv[2] ==%s", argv[2]);
    syslog(LOG_INFO, "######argv[3] ==%s", argv[3]);

    // If the disposition of SIGCLD signal is set to SIG_IGN (default)
    // then the signal will be ignored and g_spawn_sync() will not be
    // able to return the status.
    // As a consequence, we make sure that the disposition is set to
    // SIG_DFL before calling g_spawn_sync().
#if defined(SIGCLD)
    struct sigaction action;
    sigaction(SIGCLD, 0, &action);
    if (action.sa_handler == SIG_IGN) {
        action.sa_handler = SIG_DFL;
        sigaction(SIGCLD, &action, 0);
    }
#endif

    int status;
    GUniqueOutPtr<char> stdOut;
    GUniqueOutPtr<GError> error;
    if (!g_spawn_sync(nullptr, argv, nullptr, G_SPAWN_STDERR_TO_DEV_NULL, nullptr, nullptr, &stdOut.outPtr(), nullptr, &status, &error.outPtr())) {
        {
            WTFLogAlways("Failed to launch %s: %s", argv[0], error->message);
            syslog(LOG_INFO, "Fail to lauch  PlugininProcess launced..%s: %s", argv[0], error->message );
        }

        return false;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
        {
            syslog(LOG_INFO, "Error scanning plugin err 1) %s, %s returned %d exit status", argv[2], argv[0], status );
            WTFLogAlways("Error scanning plugin %s, %s returned %d exit status", argv[2], argv[0], status);
        }
        return false;
    }

    if (!stdOut) {
        {
            WTFLogAlways("Error scanning plugin %s, %s didn't write any output to stdout", argv[2], argv[0]);
            syslog(LOG_INFO, "Error scanning plugin %s, %s didn't write any output to stdout", argv[2], argv[0] );
        }
        return false;
    }

    Vector<String> lines;
    String::fromUTF8(stdOut.get()).split(UChar('\n'), true, lines);

    if (lines.size() < 3) {
        WTFLogAlways("Error scanning plugin %s, too few lines of output provided", argv[2]);
        {
            syslog(LOG_INFO, "Fail toError scanning plugin %s, too few lines of output provided", argv[2] );
            return false;
        }
    }

	syslog(LOG_INFO, "PLuginProcessPRoxyUnix file PlugininProcess launced..---start to read name ,descriptin andetc !!!!");
    result.name.swap(lines[0]);
    result.description.swap(lines[1]);
    result.mimeDescription.swap(lines[2]);

    syslog(LOG_INFO, "---result.name  ==%s", result.name.utf8().data());
       syslog(LOG_INFO, "---result.description==%s", result.description.utf8().data());
       syslog(LOG_INFO, "---result.mimedesctiptin ==%s", result.mimeDescription.utf8().data());




#if PLATFORM(GTK)
    result.requiresGtk2 = requiresGtk2;
#endif
    syslog(LOG_INFO, "retrun !mimeDescripton.isEmpty().. !!!!");
    return !result.mimeDescription.isEmpty();
#else // PLATFORM(GTK) || PLATFORM(EFL)
    return false;
#endif // PLATFORM(GTK) || PLATFORM(EFL)
}
#endif // PLUGIN_ARCHITECTURE(X11) || PLUGIN_ARCHITECTURE(WayLand))

} // namespace WebKit

#endif // ENABLE(PLUGIN_PROCESS)
