/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
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
#include "PluginProcessManager.h"

#if ENABLE(NETSCAPE_PLUGIN_API)

#include "PluginProcessProxy.h"
#include <wtf/CryptographicallyRandomNumber.h>
#include <wtf/StdLibExtras.h>
#include <wtf/text/WTFString.h>
#include <syslog.h>
#include<execinfo.h>

namespace WebKit {

PluginProcessManager& PluginProcessManager::singleton()
{
    int n=10;
    void *stack[10];
    WTFGetBacktrace(stack, &n);
     syslog(LOG_INFO, "File= %s, FUNCTION = %s", __FILE__, __FUNCTION__);
     syslog(LOG_INFO, "++++++PPProcessMain  stacksize: %d", n);
     for (int i=0; i<n;i++)
     syslog(LOG_INFO, "PluginProcessManager::singleton, %d, %s", i, *backtrace_symbols(stack+i, 1));
    static NeverDestroyed<PluginProcessManager> pluginProcessManager;

    return pluginProcessManager;
}

PluginProcessManager::PluginProcessManager()
#if PLATFORM(COCOA)
    : m_processSuppressionDisabledForPageCounter([this](RefCounterEvent event) { updateProcessSuppressionDisabled(event); })
#endif
{
}

uint64_t PluginProcessManager::pluginProcessToken(const PluginModuleInfo& pluginModuleInfo, PluginProcessType pluginProcessType, PluginProcessSandboxPolicy pluginProcessSandboxPolicy)
{
	syslog(LOG_INFO, "see if we know token alrady...File= %s, FUNCTION = %s", __FILE__, __FUNCTION__);
    // See if we know this token already.
    for (size_t i = 0; i < m_pluginProcessTokens.size(); ++i) {
        const PluginProcessAttributes& attributes = m_pluginProcessTokens[i].first;

        if (attributes.moduleInfo.path == pluginModuleInfo.path
            && attributes.processType == pluginProcessType
            && attributes.sandboxPolicy == pluginProcessSandboxPolicy)
        {
        	syslog(LOG_INFO, "We know the token resturn second");
        	return m_pluginProcessTokens[i].second;
        }
    }

    uint64_t token;
    while (true) {
        cryptographicallyRandomValues(&token, sizeof(token));

        if (m_knownTokens.isValidValue(token) && !m_knownTokens.contains(token))
            break;
    }
    syslog(LOG_INFO, "set pluginginprocessAttributes  and tocken append");
    PluginProcessAttributes attributes;
    attributes.moduleInfo = pluginModuleInfo;
    attributes.processType = pluginProcessType;
    attributes.sandboxPolicy = pluginProcessSandboxPolicy;

    m_pluginProcessTokens.append(std::make_pair(WTFMove(attributes), token));
    m_knownTokens.add(token);
    syslog(LOG_INFO, "token=====%ld", (uint64_t)token);
    return token;
}

void PluginProcessManager::getPluginProcessConnection(uint64_t pluginProcessToken, PassRefPtr<Messages::WebProcessProxy::GetPluginProcessConnection::DelayedReply> reply)
{
	syslog(LOG_INFO, " ---- will getOrCreatePluginProcessProxy File= %s, FUNCTION = %s   ", __FILE__, __FUNCTION__);
    ASSERT(pluginProcessToken);
    syslog(LOG_INFO, "PluginProcess token=====%ld", (uint64_t)pluginProcessToken);
    PluginProcessProxy* pluginProcess = getOrCreatePluginProcess(pluginProcessToken);
    syslog(LOG_INFO, "--- will getOrCreateProcessConnection File= %s, FUNCTION = %s    ", __FILE__, __FUNCTION__);
    pluginProcess->getPluginProcessConnection(reply);
}

void PluginProcessManager::removePluginProcessProxy(PluginProcessProxy* pluginProcessProxy)
{
    size_t vectorIndex = m_pluginProcesses.find(pluginProcessProxy);
    ASSERT(vectorIndex != notFound);

    m_pluginProcesses.remove(vectorIndex);
}

void PluginProcessManager::fetchWebsiteData(const PluginModuleInfo& plugin, std::function<void (Vector<String>)> completionHandler)
{
    PluginProcessProxy* pluginProcess = getOrCreatePluginProcess(pluginProcessToken(plugin, PluginProcessTypeNormal, PluginProcessSandboxPolicyNormal));

    pluginProcess->fetchWebsiteData(WTFMove(completionHandler));
}

void PluginProcessManager::deleteWebsiteData(const PluginModuleInfo& plugin, std::chrono::system_clock::time_point modifiedSince, std::function<void ()> completionHandler)
{
    PluginProcessProxy* pluginProcess = getOrCreatePluginProcess(pluginProcessToken(plugin, PluginProcessTypeNormal, PluginProcessSandboxPolicyNormal));

    pluginProcess->deleteWebsiteData(modifiedSince, WTFMove(completionHandler));
}

void PluginProcessManager::deleteWebsiteDataForHostNames(const PluginModuleInfo& plugin, const Vector<String>& hostNames, std::function<void ()> completionHandler)
{
    PluginProcessProxy* pluginProcess = getOrCreatePluginProcess(pluginProcessToken(plugin, PluginProcessTypeNormal, PluginProcessSandboxPolicyNormal));
    pluginProcess->deleteWebsiteDataForHostNames(hostNames, WTFMove(completionHandler));
}

PluginProcessProxy* PluginProcessManager::getOrCreatePluginProcess(uint64_t pluginProcessToken)
{
	syslog(LOG_INFO, "File= %s, FUNCTION = %s", __FILE__, __FUNCTION__);
    for (size_t i = 0; i < m_pluginProcesses.size(); ++i) {
        if (m_pluginProcesses[i]->pluginProcessToken() == pluginProcessToken)
            return m_pluginProcesses[i].get();
    }

    for (size_t i = 0; i < m_pluginProcessTokens.size(); ++i) {
        auto& attributesAndToken = m_pluginProcessTokens[i];
        if (attributesAndToken.second == pluginProcessToken) {
            syslog(LOG_INFO, "PluingProcesManager: process type=%d", attributesAndToken.first.processType);
            auto pluginProcess = PluginProcessProxy::create(this, attributesAndToken.first, attributesAndToken.second);
            PluginProcessProxy* pluginProcessPtr = pluginProcess.ptr();

            m_pluginProcesses.append(WTFMove(pluginProcess));
            return pluginProcessPtr;
        }
    }

    return nullptr;
}

} // namespace WebKit

#endif // ENABLE(NETSCAPE_PLUGIN_API)
