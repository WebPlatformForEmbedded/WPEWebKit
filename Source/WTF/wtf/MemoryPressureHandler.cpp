/*
 * Copyright (C) 2011-2017 Apple Inc. All Rights Reserved.
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
#include <wtf/MemoryPressureHandler.h>

#if OS(LINUX)
#include <unistd.h>
#endif
#include <fnmatch.h>
#include <wtf/Logging.h>
#include <wtf/MemoryFootprint.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/RAMSize.h>
#include <wtf/text/StringToIntegerConversion.h>

namespace WTF {

WTF_EXPORT_PRIVATE bool MemoryPressureHandler::ReliefLogger::s_loggingEnabled = false;

#if PLATFORM(IOS_FAMILY)
static const double s_conservativeThresholdFraction = 0.5;
static const double s_strictThresholdFraction = 0.65;
#else
static const double s_conservativeThresholdFraction = 0.8;
static const double s_strictThresholdFraction = 0.9;
#endif
static const std::optional<double> s_killThresholdFraction;
static const Seconds s_pollInterval = 30_s;

// This file contains the amount of video memory used, and will be filled by some other
// platform component. It's a text file containing an unsigned integer value.
static String s_GPUMemoryFile;
static ssize_t s_envBaseThresholdVideo = 0;

static bool isWebProcess()
{
    static bool result = false;
    static bool initialized = false;

    if (!initialized) {
        initialized = true;

        FILE* file = fopen("/proc/self/cmdline", "r");
        if (!file)
            return result;

        char* buffer = nullptr;
        size_t size = 0;
        if (getline(&buffer, &size, file) != -1)
            result = !fnmatch("*WPEWebProcess*", buffer, 0);

        free(buffer);
        fclose(file);
    }

    return result;
}

static size_t memoryFootprintVideo()
{
    if (!isWebProcess() || s_GPUMemoryFile.isEmpty())
        return 0;

    FILE* file = fopen(s_GPUMemoryFile.utf8().data(), "r");
    if (!file)
        return 0;

    char* buffer = nullptr;
    size_t size = 0;
    size_t footprint = 0;
    if (getline(&buffer, &size, file) != -1)
        sscanf(buffer, "%u", &footprint);

    free(buffer);
    fclose(file);

    return footprint;
}

MemoryPressureHandler& MemoryPressureHandler::singleton()
{
    static LazyNeverDestroyed<MemoryPressureHandler> memoryPressureHandler;
    static std::once_flag onceKey;
    std::call_once(onceKey, [&] {
        memoryPressureHandler.construct();
    });
    return memoryPressureHandler;
}

MemoryPressureHandler::MemoryPressureHandler()
#if OS(LINUX) || OS(FREEBSD)
    : m_holdOffTimer(RunLoop::main(), this, &MemoryPressureHandler::holdOffTimerFired)
#elif OS(WINDOWS)
    : m_windowsMeasurementTimer(RunLoop::main(), this, &MemoryPressureHandler::windowsMeasurementTimerFired)
#endif
{
#if PLATFORM(COCOA)
    setDispatchQueue(dispatch_get_main_queue());
#endif

    // If this is the WebProcess, Check whether the env var WPE_POLL_MAX_MEMORY_GPU_FILE exists, containing the file
    // that we need to poll to get the video memory used, and whether WPE_POLL_MAX_MEMORY_GPU exists, overriding the
    // limit for video memory set by the API.
    if (isWebProcess()) {
        s_GPUMemoryFile = String::fromLatin1(getenv("WPE_POLL_MAX_MEMORY_GPU_FILE"));
        String s = String::fromLatin1(getenv("WPE_POLL_MAX_MEMORY_GPU"));
        if (!s.isEmpty()) {
            String value = s.stripWhiteSpace().convertToLowercaseWithoutLocale();
            size_t units = 1;
            if (value.endsWith('k'))
                units = KB;
            else if (value.endsWith('m'))
                units = MB;
            if (units != 1)
                value = value.substring(0, value.length() - 1);
            s_envBaseThresholdVideo = parseInteger<size_t>(value).value_or(0) * units;
            if (s_envBaseThresholdVideo)
                m_configuration.baseThresholdVideo = s_envBaseThresholdVideo;
        }
    }
}

void MemoryPressureHandler::setShouldUsePeriodicMemoryMonitor(bool use)
{
    if (!isFastMallocEnabled()) {
        // If we're running with FastMalloc disabled, some kind of testing or debugging is probably happening.
        // Let's be nice and not enable the memory kill mechanism.
        return;
    }

    if (use) {
        m_measurementTimer = makeUnique<RunLoop::Timer<MemoryPressureHandler>>(RunLoop::main(), this, &MemoryPressureHandler::measurementTimerFired);
        m_measurementTimer->startRepeating(m_configuration.pollInterval);
    } else
        m_measurementTimer = nullptr;
}

#if !RELEASE_LOG_DISABLED
static const char* toString(MemoryUsagePolicy policy)
{
    switch (policy) {
    case MemoryUsagePolicy::Unrestricted: return "Unrestricted";
    case MemoryUsagePolicy::Conservative: return "Conservative";
    case MemoryUsagePolicy::Strict: return "Strict";
    }
    ASSERT_NOT_REACHED();
    return "";
}
#endif

static size_t thresholdForMemoryKillOfActiveProcess(unsigned tabCount)
{
#if CPU(ADDRESS64)
    size_t baseThreshold = ramSize() > 16 * GB ? 15 * GB : 7 * GB;
    return baseThreshold + tabCount * GB;
#else
    UNUSED_PARAM(tabCount);
    return std::min(3 * GB, static_cast<size_t>(ramSize() * 0.9));
#endif
}

static size_t thresholdForMemoryKillOfInactiveProcess(unsigned tabCount)
{
#if CPU(ADDRESS64)
    size_t baseThreshold = 3 * GB + tabCount * GB;
#else
    size_t baseThreshold = tabCount > 1 ? 3 * GB : 2 * GB;
#endif
    return std::min(baseThreshold, static_cast<size_t>(ramSize() * 0.9));
}

void MemoryPressureHandler::setPageCount(unsigned pageCount)
{
    if (singleton().m_pageCount == pageCount)
        return;
    singleton().m_pageCount = pageCount;
}

std::optional<size_t> MemoryPressureHandler::thresholdForMemoryKill(MemoryType type)
{
    if (m_configuration.killThresholdFraction)
        return (*m_configuration.killThresholdFraction) * (type == MemoryType::Normal ? m_configuration.baseThreshold : m_configuration.baseThresholdVideo);
    else {
        // Don't kill the process if no killThreshold was set.
        return std::nullopt;
    }

    switch (m_processState) {
    case WebsamProcessState::Inactive:
        return thresholdForMemoryKillOfInactiveProcess(m_pageCount);
    case WebsamProcessState::Active:
        return thresholdForMemoryKillOfActiveProcess(m_pageCount);
    }
    return std::nullopt;
}

size_t MemoryPressureHandler::thresholdForPolicy(MemoryUsagePolicy policy, MemoryType type)
{
    switch (policy) {
    case MemoryUsagePolicy::Unrestricted:
        return 0;
    case MemoryUsagePolicy::Conservative:
        return m_configuration.conservativeThresholdFraction * (type == MemoryType::Normal ? m_configuration.baseThreshold : m_configuration.baseThresholdVideo);
    case MemoryUsagePolicy::Strict:
        return m_configuration.strictThresholdFraction * (type == MemoryType::Normal ? m_configuration.baseThreshold : m_configuration.baseThresholdVideo);
    default:
        ASSERT_NOT_REACHED();
        return 0;
    }
}

MemoryUsagePolicy MemoryPressureHandler::policyForFootprints(size_t footprint, size_t footprintVideo)
{
    if (footprint >= thresholdForPolicy(MemoryUsagePolicy::Strict, MemoryType::Normal) || footprintVideo >= thresholdForPolicy(MemoryUsagePolicy::Strict, MemoryType::Video))
        return MemoryUsagePolicy::Strict;
    if (footprint >= thresholdForPolicy(MemoryUsagePolicy::Conservative, MemoryType::Normal) || footprintVideo >= thresholdForPolicy(MemoryUsagePolicy::Conservative, MemoryType::Video))
        return MemoryUsagePolicy::Conservative;
    return MemoryUsagePolicy::Unrestricted;
}

MemoryUsagePolicy MemoryPressureHandler::currentMemoryUsagePolicy()
{
    return policyForFootprints(memoryFootprint(), memoryFootprintVideo());
}

void MemoryPressureHandler::shrinkOrDie(size_t killThreshold, size_t killThresholdVideo)
{
    RELEASE_LOG(MemoryPressure, "Process is above the memory kill threshold. Trying to shrink down.");
    releaseMemory(Critical::Yes, Synchronous::Yes);

    size_t footprint = memoryFootprint();
    size_t footprintVideo = memoryFootprintVideo();
    RELEASE_LOG(MemoryPressure, "New memory footprint: %zu MB", footprint / MB);

    if ((footprint < killThreshold) && (footprintVideo < killThresholdVideo)) {
        RELEASE_LOG(MemoryPressure, "Shrank below memory kill threshold. Process gets to live.");
        setMemoryUsagePolicyBasedOnFootprints(footprint, footprintVideo);
        return;
    }

    if (footprint >= killThreshold)
        WTFLogAlways("Unable to shrink memory footprint of process (%zu MB) below the kill thresold (%zu MB). Killed\n", footprint / MB, killThreshold / MB);
    else
        WTFLogAlways("Unable to shrink video memory footprint of process (%zu MB) below the kill thresold (%zu MB). Killed\n", footprintVideo / MB, killThresholdVideo / MB);
    RELEASE_ASSERT(m_memoryKillCallback);
    m_memoryKillCallback();
}

void MemoryPressureHandler::setMemoryUsagePolicyBasedOnFootprints(size_t footprint, size_t footprintVideo)
{
    auto newPolicy = policyForFootprints(footprint, footprintVideo);
    if (newPolicy == m_memoryUsagePolicy)
        return;

    RELEASE_LOG(MemoryPressure, "Memory usage policy changed: %s -> %s, new thresholds: %zu MB, video %zu MB", toString(m_memoryUsagePolicy), toString(newPolicy), thresholdForPolicy(newPolicy, MemoryType::Normal) / MB, thresholdForPolicy(newPolicy, MemoryType::Video) / MB);
    m_memoryUsagePolicy = newPolicy;
    memoryPressureStatusChanged();
}

void MemoryPressureHandler::measurementTimerFired()
{
    size_t footprint = memoryFootprint();
    size_t footprintVideo = memoryFootprintVideo();

    static size_t footprintPeak = 0;
    if (footprintPeak < footprint)
        footprintPeak = footprint;
    RELEASE_LOG(MemoryPressure, "Current memory footprint: %zu MB, peak: %zu MB, video footprint: %zu MB", footprint / MB, footprintPeak / MB, footprintVideo / MB);
    auto killThreshold = thresholdForMemoryKill(MemoryType::Normal);
    auto killThresholdVideo = thresholdForMemoryKill(MemoryType::Video);
    if ((killThreshold && footprint >= *killThreshold) || (killThresholdVideo && footprintVideo >= *killThresholdVideo)) {
        shrinkOrDie(*killThreshold, *killThresholdVideo);
        return;
    }

    setMemoryUsagePolicyBasedOnFootprints(footprint, footprintVideo);

    switch (m_memoryUsagePolicy) {
    case MemoryUsagePolicy::Unrestricted:
        break;
    case MemoryUsagePolicy::Conservative:
        releaseMemory(Critical::No, Synchronous::No);
        break;
    case MemoryUsagePolicy::Strict:
        releaseMemory(Critical::Yes, Synchronous::No);
        break;
    }

    if (processState() == WebsamProcessState::Active && footprint > thresholdForMemoryKillOfInactiveProcess(m_pageCount))
        doesExceedInactiveLimitWhileActive();
    else
        doesNotExceedInactiveLimitWhileActive();
}

void MemoryPressureHandler::doesExceedInactiveLimitWhileActive()
{
    if (m_hasInvokedDidExceedInactiveLimitWhileActiveCallback)
        return;
    if (m_didExceedInactiveLimitWhileActiveCallback)
        m_didExceedInactiveLimitWhileActiveCallback();
    m_hasInvokedDidExceedInactiveLimitWhileActiveCallback = true;
}

void MemoryPressureHandler::doesNotExceedInactiveLimitWhileActive()
{
    m_hasInvokedDidExceedInactiveLimitWhileActiveCallback = false;
}

void MemoryPressureHandler::setProcessState(WebsamProcessState state)
{
    if (m_processState == state)
        return;
    m_processState = state;
}

void MemoryPressureHandler::beginSimulatedMemoryPressure()
{
    if (m_isSimulatingMemoryPressure)
        return;
    m_isSimulatingMemoryPressure = true;
    memoryPressureStatusChanged();
    respondToMemoryPressure(Critical::Yes, Synchronous::Yes);
}

void MemoryPressureHandler::endSimulatedMemoryPressure()
{
    if (!m_isSimulatingMemoryPressure)
        return;
    m_isSimulatingMemoryPressure = false;
    memoryPressureStatusChanged();
}

void MemoryPressureHandler::setConfiguration(Configuration&& configuration)
{
    m_configuration = WTFMove(configuration);
    if (s_envBaseThresholdVideo)
        m_configuration.baseThresholdVideo = s_envBaseThresholdVideo;
}

void MemoryPressureHandler::setConfiguration(const Configuration& configuration)
{
    m_configuration = configuration;
    if (s_envBaseThresholdVideo)
        m_configuration.baseThresholdVideo = s_envBaseThresholdVideo;
}

void MemoryPressureHandler::releaseMemory(Critical critical, Synchronous synchronous)
{
    if (!m_lowMemoryHandler)
        return;

    ReliefLogger log("Total");
    m_lowMemoryHandler(critical, synchronous);
    platformReleaseMemory(critical);
}

void MemoryPressureHandler::setMemoryPressureStatus(MemoryPressureStatus memoryPressureStatus)
{
    if (m_memoryPressureStatus == memoryPressureStatus)
        return;

    m_memoryPressureStatus = memoryPressureStatus;
    memoryPressureStatusChanged();
}

void MemoryPressureHandler::memoryPressureStatusChanged()
{
    if (m_memoryPressureStatusChangedCallback)
        m_memoryPressureStatusChangedCallback(m_memoryPressureStatus);
}

void MemoryPressureHandler::ReliefLogger::logMemoryUsageChange()
{
#if !RELEASE_LOG_DISABLED
#define MEMORYPRESSURE_LOG(...) RELEASE_LOG(MemoryPressure, __VA_ARGS__)
#else
#define MEMORYPRESSURE_LOG(...) WTFLogAlways(__VA_ARGS__)
#endif

    auto currentMemory = platformMemoryUsage();
    if (!currentMemory || !m_initialMemory) {
#if OS(LINUX)
        MEMORYPRESSURE_LOG("Memory pressure relief: pid = %d, %" PUBLIC_LOG_STRING ": (Unable to get dirty memory information for process)", getpid(), m_logString);
#else
        MEMORYPRESSURE_LOG("Memory pressure relief: %" PUBLIC_LOG_STRING ": (Unable to get dirty memory information for process)", m_logString);
#endif
        return;
    }

    long residentDiff = currentMemory->resident - m_initialMemory->resident;
    long physicalDiff = currentMemory->physical - m_initialMemory->physical;

#if !OS(LINUX)
    MEMORYPRESSURE_LOG("Memory pressure relief: %" PUBLIC_LOG_STRING ": res = %zu/%zu/%ld, res+swap = %zu/%zu/%ld",
#else
    MEMORYPRESSURE_LOG("Memory pressure relief: pid = %d, %" PUBLIC_LOG_STRING ": res = %zu/%zu/%ld, res+swap = %zu/%zu/%ld",
        getpid(),
#endif
        m_logString,
        m_initialMemory->resident, currentMemory->resident, residentDiff,
        m_initialMemory->physical, currentMemory->physical, physicalDiff);
}

#if !OS(WINDOWS)
void MemoryPressureHandler::platformInitialize() { }
#endif

#if PLATFORM(COCOA)
void MemoryPressureHandler::setDispatchQueue(OSObjectPtr<dispatch_queue_t>&& queue)
{
    RELEASE_ASSERT(!m_installed);
    m_dispatchQueue = WTFMove(queue);
}
#endif

MemoryPressureHandler::Configuration::Configuration()
    : baseThreshold(std::min(3 * GB, ramSize()))
    , baseThresholdVideo(1 * GB)
    , conservativeThresholdFraction(s_conservativeThresholdFraction)
    , strictThresholdFraction(s_strictThresholdFraction)
    , killThresholdFraction(s_killThresholdFraction)
    , pollInterval(s_pollInterval)
{
}

MemoryPressureHandler::Configuration::Configuration(size_t base, size_t baseVideo, double conservative, double strict, std::optional<double> kill, Seconds interval)
    : baseThreshold(base)
    , baseThresholdVideo(baseVideo)
    , conservativeThresholdFraction(conservative)
    , strictThresholdFraction(strict)
    , killThresholdFraction(kill)
    , pollInterval(interval)
{
}

} // namespace WebCore
