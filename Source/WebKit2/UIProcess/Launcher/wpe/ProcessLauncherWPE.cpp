/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 * Portions Copyright (c) 2010 Motorola Mobility, Inc.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY MOTOROLA INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MOTOROLA INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ProcessLauncher.h"

#include "Connection.h"
#include "ProcessExecutablePath.h"
#include <WebCore/FileSystem.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <wpe/renderer-host.h>
#include <wtf/RunLoop.h>
#include <wtf/text/CString.h>
#include <wtf/text/WTFString.h>
#include <wtf/glib/GLibUtilities.h>
#include <wtf/glib/GUniquePtr.h>
#include <syslog.h>
// FIXME: Merge with ProcessLauncherGtk?

using namespace WebCore;

namespace WebKit {

static void childSetupFunction(gpointer userData)
{
	syslog(LOG_INFO, "File= %s, FUN= %s ---", __FILE__, __FUNCTION__);
    int socket = GPOINTER_TO_INT(userData);
    close(socket);
}

void ProcessLauncher::launchProcess()
{
	syslog(LOG_INFO, "File= %s, FUN= %s ---", __FILE__, __FUNCTION__);
	GPid pid = 0;

    IPC::Connection::SocketPair socketPair = IPC::Connection::createPlatformConnection(IPC::Connection::ConnectionOptions::SetCloexecOnServer);

    String executablePath, pluginPath;       //add espial pluign path
    CString realExecutablePath, realPluginPath; // add espial
    WTFLogAlways("creating a new Process: %d: ", (int)m_launchOptions.processType);
    switch (m_launchOptions.processType) {
    case ProcessLauncher::ProcessType::Web:
        executablePath = executablePathOfWebProcess();
        break;

//Espial start
#if ENABLE(NETSCAPE_PLUGIN_API)
    case ProcessLauncher::ProcessType::Plugin64:
    case ProcessLauncher::ProcessType::Plugin32:
        executablePath = executablePathOfPluginProcess();
//#if ENABLE(PLUGIN_PROCESS_GTK2)
//        if (m_launchOptions.extraInitializationData.contains("requires-gtk2"))
//            executablePath.append('2');
//#endif
    	syslog(LOG_INFO, "File= %s, FUN= ...in plugin64/32%s ---", __FILE__, __FUNCTION__);
        pluginPath = m_launchOptions.extraInitializationData.get("plugin-path");
        realPluginPath = fileSystemRepresentation(pluginPath);
        break;
#endif        
// Espial end

    case ProcessLauncher::ProcessType::Network:
        executablePath = executablePathOfNetworkProcess();
        break;
#if ENABLE(DATABASE_PROCESS)
    case ProcessLauncher::ProcessType::Database:
        executablePath = executablePathOfDatabaseProcess();
        break;
#endif
    default:
        ASSERT_NOT_REACHED();
        return;
    }

    realExecutablePath = fileSystemRepresentation(executablePath);
    GUniquePtr<gchar> wkSocket(g_strdup_printf("%d", socketPair.client));
    GUniquePtr<gchar> wpeSocket;

    unsigned nargs = 4; // size of the argv array for g_spawn_async()
    if (m_launchOptions.processType == ProcessLauncher::ProcessType::Web) {
        wpeSocket = GUniquePtr<gchar>(g_strdup_printf("%d", wpe_renderer_host_create_client()));
        nargs = 5;
    }

#if ENABLE(DEVELOPER_MODE)
    Vector<CString> prefixArgs;
    if (!m_launchOptions.processCmdPrefix.isNull()) {
        Vector<String> splitArgs;
        m_launchOptions.processCmdPrefix.split(' ', splitArgs);
        for (auto it = splitArgs.begin(); it != splitArgs.end(); it++)
            prefixArgs.append(it->utf8());
        nargs += prefixArgs.size();
    }
#endif

    char** argv = g_newa(char*, nargs);
    unsigned i = 0;
#if ENABLE(DEVELOPER_MODE)
    // If there's a prefix command, put it before the rest of the args.
    for (auto it = prefixArgs.begin(); it != prefixArgs.end(); it++)
        argv[i++] = const_cast<char*>(it->data());
#endif
    argv[i++] = const_cast<char*>(realExecutablePath.data());
    argv[i++] = wkSocket.get();
    if (m_launchOptions.processType == ProcessLauncher::ProcessType::Web)
        argv[i++] = wpeSocket.get();

    argv[i++] = const_cast<char*>(realPluginPath.data()); // Espial add

    argv[i++] = nullptr;
    //add debug info
    syslog(LOG_INFO,"before async, parameters are:");
    syslog(LOG_INFO,"argv[0]=,%s", argv[0]);
    syslog(LOG_INFO,"argv[1]=,%s", argv[1]);
    syslog(LOG_INFO,"argv[2]=,%s", argv[2]);
    syslog(LOG_INFO,"argv[3]=,%s", argv[3]);
    WTFLogAlways("creating a new Process: %s: ...", executablePath.utf8().data());
    GUniqueOutPtr<GError> error;
    if (!g_spawn_async(nullptr, argv, nullptr, static_cast<GSpawnFlags>(G_SPAWN_LEAVE_DESCRIPTORS_OPEN | G_SPAWN_DO_NOT_REAP_CHILD), childSetupFunction, GINT_TO_POINTER(socketPair.server), &pid, &error.outPtr())) {
        g_printerr("Unable to fork a new WebProcess: %s.\n", error->message);
        ASSERT_NOT_REACHED();
    }

    // Don't expose the parent socket to potential future children.
    while (fcntl(socketPair.client, F_SETFD, FD_CLOEXEC) == -1)
        RELEASE_ASSERT(errno != EINTR);

    g_child_watch_add(pid, [](GPid pid, gint, gpointer) { g_spawn_close_pid(pid); }, nullptr);

    close(socketPair.client);
    m_processIdentifier = pid;
    syslog(LOG_INFO,"Sucessed fork a new WebProcess, pid=%d: ",pid);
    WTFLogAlways("Sucessed fork a new Process: %s, pid=%d: ", executablePath.utf8().data(), pid);

    // We've finished launching the process, message back to the main run loop.
    RefPtr<ProcessLauncher> protector(this);
    IPC::Connection::Identifier serverSocket = socketPair.server;
    RunLoop::main().dispatch([protector, pid, serverSocket] {
        protector->didFinishLaunchingProcess(pid, serverSocket);
    });
}

void ProcessLauncher::terminateProcess()
{
	syslog(LOG_INFO, "File= %s, FUN= %s ---", __FILE__, __FUNCTION__);
    if (m_isLaunching) {
        invalidate();
        return;
    }

    if (!m_processIdentifier)
        return;

    kill(m_processIdentifier, SIGKILL);
    m_processIdentifier = 0;
}

void ProcessLauncher::platformInvalidate()
{
}

} // namespace WebKit
