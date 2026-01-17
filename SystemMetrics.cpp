#include "SystemMetrics.h"
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#include <net/if.h>
#include <net/route.h>
#include <cstdlib>
#include <net/if_types.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace {

const auto kNetworkUpdateInterval = std::chrono::milliseconds(333);
const auto kDiskUpdateInterval = std::chrono::milliseconds(1500);
const auto kSystemInfoUpdateInterval = std::chrono::milliseconds(333);
const auto kGPUUpdateInterval = std::chrono::milliseconds(500);

template <typename T, size_t N>
constexpr size_t arraySize(const T (&)[N]) {
    return N;
}

bool tryGetDictionaryValue(CFDictionaryRef dict, CFStringRef key, uint64_t& outValue) {
    if (!dict || !key) {
        return false;
    }

    CFNumberRef number = (CFNumberRef)CFDictionaryGetValue(dict, key);
    if (!number) {
        return false;
    }

    int64_t value = 0;
    if (!CFNumberGetValue(number, kCFNumberSInt64Type, &value)) {
        return false;
    }

    if (value < 0) {
        return false;
    }

    outValue = static_cast<uint64_t>(value);
    return true;
}

bool tryGetDictionaryValue(CFDictionaryRef dict, const CFStringRef* keys, size_t keyCount, uint64_t& outValue) {
    for (size_t i = 0; i < keyCount; ++i) {
        if (tryGetDictionaryValue(dict, keys[i], outValue)) {
            return true;
        }
    }
    return false;
}

bool tryGetDictionaryDouble(CFDictionaryRef dict, CFStringRef key, double& outValue) {
    if (!dict || !key) {
        return false;
    }

    CFNumberRef number = (CFNumberRef)CFDictionaryGetValue(dict, key);
    if (!number) {
        return false;
    }

    double value = 0.0;
    if (!CFNumberGetValue(number, kCFNumberDoubleType, &value)) {
        int64_t intValue = 0;
        if (!CFNumberGetValue(number, kCFNumberSInt64Type, &intValue)) {
            return false;
        }
        value = static_cast<double>(intValue);
    }

    outValue = value;
    return true;
}

bool tryGetDictionaryDouble(CFDictionaryRef dict, const CFStringRef* keys, size_t keyCount, double& outValue) {
    for (size_t i = 0; i < keyCount; ++i) {
        if (tryGetDictionaryDouble(dict, keys[i], outValue)) {
            return true;
        }
    }
    return false;
}

} // namespace

SystemMetrics::SystemMetrics() 
    : machPort_(0), prevCpuLoad_(nullptr), numCpus_(0),
      prevNetworkIn_(0), prevNetworkOut_(0), prevPacketsIn_(0), prevPacketsOut_(0),
      prevDiskRead_(0), prevDiskWrite_(0),
      prevDiskReadOps_(0), prevDiskWriteOps_(0),
      diskStatsInitialized_(false),
      lastDiskSample_(),
      lastNetworkSample_(),
      lastSystemInfoSample_(),
      lastGpuSample_(),
      networkIter_(0), diskIter_(0) {
}

SystemMetrics::~SystemMetrics() {
    if (prevCpuLoad_) {
        vm_deallocate(mach_host_self(), (vm_address_t)prevCpuLoad_, 
                      numCpus_ * sizeof(processor_cpu_load_info_data_t));
    }
    if (networkIter_) {
        IOObjectRelease(networkIter_);
    }
    if (diskIter_) {
        IOObjectRelease(diskIter_);
    }
}

bool SystemMetrics::initialize() {
    machPort_ = mach_host_self();
    
    // Get CPU count
    kern_return_t kr = host_processor_info(machPort_, PROCESSOR_CPU_LOAD_INFO,
                                         &numCpus_, (processor_info_array_t*)&prevCpuLoad_,
                                         &numCpus_);
    if (kr != KERN_SUCCESS) {
        return false;
    }
    
    // Initialize system info
    updateSystemInfo();
    
    return true;
}

void SystemMetrics::update() {
    updateCPU();
    updateMemory();
    updateSwap();
    updateGPU();
    updateNetwork();
    updateDisk();
    updateSystemInfo();
}

void SystemMetrics::updateCPU() {
    processor_cpu_load_info_t cpuLoad;
    unsigned int numCpus;
    kern_return_t kr = host_processor_info(machPort_, PROCESSOR_CPU_LOAD_INFO,
                                         &numCpus, (processor_info_array_t*)&cpuLoad,
                                         &numCpus);
    
    if (kr != KERN_SUCCESS) {
        return;
    }
    
    cpuMetrics_.resize(numCpus);
    
    for (unsigned int i = 0; i < numCpus; i++) {
        uint32_t totalTicks = 0;
        uint32_t usedTicks = 0;
        
        for (int j = 0; j < CPU_STATE_MAX; j++) {
            totalTicks += cpuLoad[i].cpu_ticks[j];
        }
        
        usedTicks = cpuLoad[i].cpu_ticks[CPU_STATE_USER] + 
                   cpuLoad[i].cpu_ticks[CPU_STATE_SYSTEM];
        
        uint32_t prevTotal = 0;
        uint32_t prevUsed = 0;
        
        for (int j = 0; j < CPU_STATE_MAX; j++) {
            prevTotal += prevCpuLoad_[i].cpu_ticks[j];
        }
        
        prevUsed = prevCpuLoad_[i].cpu_ticks[CPU_STATE_USER] + 
                  prevCpuLoad_[i].cpu_ticks[CPU_STATE_SYSTEM];
        
        uint32_t diffTotal = totalTicks - prevTotal;
        uint32_t diffUsed = usedTicks - prevUsed;
        
        if (diffTotal > 0) {
            cpuMetrics_[i].user = (double)(cpuLoad[i].cpu_ticks[CPU_STATE_USER] - 
                                          prevCpuLoad_[i].cpu_ticks[CPU_STATE_USER]) / diffTotal * 100.0;
            cpuMetrics_[i].system = (double)(cpuLoad[i].cpu_ticks[CPU_STATE_SYSTEM] - 
                                            prevCpuLoad_[i].cpu_ticks[CPU_STATE_SYSTEM]) / diffTotal * 100.0;
            cpuMetrics_[i].idle = (double)(cpuLoad[i].cpu_ticks[CPU_STATE_IDLE] - 
                                          prevCpuLoad_[i].cpu_ticks[CPU_STATE_IDLE]) / diffTotal * 100.0;
            cpuMetrics_[i].total = (double)diffUsed / diffTotal * 100.0;
        }
    }
    
    // Store current values for next update
    if (prevCpuLoad_) {
        vm_deallocate(mach_host_self(), (vm_address_t)prevCpuLoad_, 
                      numCpus_ * sizeof(processor_cpu_load_info_data_t));
    }
    prevCpuLoad_ = cpuLoad;
    numCpus_ = numCpus;
}

void SystemMetrics::updateMemory() {
    vm_size_t pageSize;
    host_page_size(machPort_, &pageSize);
    
    vm_statistics64_data_t vmStats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    kern_return_t kr = host_statistics64(machPort_, HOST_VM_INFO64,
                                        (host_info64_t)&vmStats, &count);
    
    if (kr != KERN_SUCCESS) {
        return;
    }
    
    uint64_t totalMemory = 0;
    size_t size = sizeof(totalMemory);
    sysctlbyname("hw.memsize", &totalMemory, &size, nullptr, 0);
    
    memoryMetrics_.total = totalMemory;
    memoryMetrics_.free = vmStats.free_count * pageSize;
    memoryMetrics_.active = vmStats.active_count * pageSize;
    memoryMetrics_.inactive = vmStats.inactive_count * pageSize;
    memoryMetrics_.wired = vmStats.wire_count * pageSize;
    memoryMetrics_.used = memoryMetrics_.active + memoryMetrics_.wired + 
                         (vmStats.compressor_page_count * pageSize);
}

void SystemMetrics::updateSwap() {
    xsw_usage swapUsage;
    size_t size = sizeof(swapUsage);
    sysctlbyname("vm.swapusage", &swapUsage, &size, nullptr, 0);
    
    swapMetrics_.total = swapUsage.xsu_total;
    swapMetrics_.used = swapUsage.xsu_used;
    swapMetrics_.free = swapUsage.xsu_avail;
    swapMetrics_.active = swapUsage.xsu_used;
    swapMetrics_.inactive = 0;
    swapMetrics_.wired = 0;
}

void SystemMetrics::updateGPU() {
    auto now = std::chrono::steady_clock::now();
    if (lastGpuSample_.time_since_epoch().count() != 0 &&
        now - lastGpuSample_ < kGPUUpdateInterval) {
        return;
    }
    lastGpuSample_ = now;

    gpuMetrics_ = GPUMetrics{};

    auto fetchStatsForClass = [&](const char* className) -> bool {
        if (!className) {
            return false;
        }
        CFMutableDictionaryRef matching = IOServiceMatching(className);
        if (!matching) {
            return false;
        }

        io_iterator_t iterator = IO_OBJECT_NULL;
        kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator);
        if (kr != KERN_SUCCESS) {
            return false;
        }

        io_object_t object = IO_OBJECT_NULL;
        bool found = false;
        while ((object = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
            CFDictionaryRef perfStats = (CFDictionaryRef)IORegistryEntryCreateCFProperty(
                object, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0);
            if (perfStats) {
                const CFStringRef deviceKeys[] = {
                    CFSTR("Device Utilization %"),
                    CFSTR("device_utilization"),
                    CFSTR("Device Utilization")
                };
                const CFStringRef rendererKeys[] = {
                    CFSTR("Renderer Utilization %"),
                    CFSTR("renderer_utilization"),
                    CFSTR("Renderer Utilization")
                };
                const CFStringRef tilerKeys[] = {
                    CFSTR("Tiler Utilization %"),
                    CFSTR("tiler_utilization"),
                    CFSTR("Tiler Utilization")
                };

                double value = 0.0;
                bool anyValue = false;
                if (tryGetDictionaryDouble(perfStats, deviceKeys, arraySize(deviceKeys), value)) {
                    gpuMetrics_.deviceUtilization = value;
                    anyValue = true;
                }
                if (tryGetDictionaryDouble(perfStats, rendererKeys, arraySize(rendererKeys), value)) {
                    gpuMetrics_.rendererUtilization = value;
                    anyValue = true;
                }
                if (tryGetDictionaryDouble(perfStats, tilerKeys, arraySize(tilerKeys), value)) {
                    gpuMetrics_.tilerUtilization = value;
                    anyValue = true;
                }

                if (anyValue) {
                    gpuMetrics_.valid = true;
                    found = true;
                }
                CFRelease(perfStats);
            }

            IOObjectRelease(object);

            if (found) {
                break;
            }
        }

        IOObjectRelease(iterator);
        return found;
    };

    if (!fetchStatsForClass("IOAccelerator") &&
        !fetchStatsForClass("AGXAccelerator")) {
        gpuMetrics_.valid = false;
    }
}

void SystemMetrics::updateNetwork() {
    auto now = std::chrono::steady_clock::now();
    if (lastNetworkSample_.time_since_epoch().count() != 0 &&
        now - lastNetworkSample_ < kNetworkUpdateInterval) {
        return;
    }
    lastNetworkSample_ = now;

    // Get network interface statistics
    int mib[] = {CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST2, 0};
    size_t len;
    
    if (sysctl(mib, 6, nullptr, &len, nullptr, 0) < 0) {
        return;
    }
    
    std::vector<char> buf(len);
    if (sysctl(mib, 6, buf.data(), &len, nullptr, 0) < 0) {
        return;
    }
    
    uint64_t totalIn = 0, totalOut = 0;
    uint64_t packetsIn = 0, packetsOut = 0;
    
    char *lim = buf.data() + len;
    char *next = buf.data();
    
    while (next < lim) {
        struct if_msghdr *ifm = (struct if_msghdr *)next;
        next += ifm->ifm_msglen;
        
        if (ifm->ifm_type == RTM_IFINFO2) {
            struct if_msghdr2 *ifm2 = (struct if_msghdr2 *)ifm;
            
            // Skip loopback interface
            if (ifm2->ifm_index == 1) continue;
            
            totalIn += ifm2->ifm_data.ifi_ibytes;
            totalOut += ifm2->ifm_data.ifi_obytes;
            packetsIn += ifm2->ifm_data.ifi_ipackets;
            packetsOut += ifm2->ifm_data.ifi_opackets;
        }
    }
    
    networkMetrics_.bytesIn = totalIn - prevNetworkIn_;
    networkMetrics_.bytesOut = totalOut - prevNetworkOut_;
    networkMetrics_.packetsIn = packetsIn - prevPacketsIn_;
    networkMetrics_.packetsOut = packetsOut - prevPacketsOut_;
    
    prevNetworkIn_ = totalIn;
    prevNetworkOut_ = totalOut;
    prevPacketsIn_ = packetsIn;
    prevPacketsOut_ = packetsOut;
}

void SystemMetrics::updateDisk() {
    auto now = std::chrono::steady_clock::now();
    if (diskStatsInitialized_ && now - lastDiskSample_ < kDiskUpdateInterval) {
        return;
    }
    double intervalSeconds = diskStatsInitialized_
        ? std::chrono::duration<double>(now - lastDiskSample_).count()
        : 1.0;
    if (intervalSeconds <= 0.0) {
        intervalSeconds = 1.0;
    }

    lastDiskSample_ = now;

    CFMutableDictionaryRef matching = IOServiceMatching("IOBlockStorageDriver");
    if (!matching) {
        return;
    }

    io_iterator_t iterator = IO_OBJECT_NULL;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator);
    if (kr != KERN_SUCCESS) {
        return;
    }

    uint64_t totalRead = 0;
    uint64_t totalWrite = 0;
    uint64_t totalReadOps = 0;
    uint64_t totalWriteOps = 0;

    io_object_t object = IO_OBJECT_NULL;
    while ((object = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        CFDictionaryRef stats = (CFDictionaryRef)IORegistryEntryCreateCFProperty(
            object, CFSTR(kIOBlockStorageDriverStatisticsKey), kCFAllocatorDefault, 0);
        if (!stats) {
            stats = (CFDictionaryRef)IORegistryEntryCreateCFProperty(
                object, CFSTR("IOBlockStorageDriverStatistics"), kCFAllocatorDefault, 0);
        }
        if (!stats) {
            stats = (CFDictionaryRef)IORegistryEntryCreateCFProperty(
                object, CFSTR("Statistics"), kCFAllocatorDefault, 0);
        }

        if (stats) {
            const CFStringRef readByteKeys[] = {
                CFSTR("Bytes (Read)"),
                CFSTR("Bytes Read"),
                CFSTR("BytesRead")
            };
            const CFStringRef writeByteKeys[] = {
                CFSTR("Bytes (Write)"),
                CFSTR("Bytes Written"),
                CFSTR("BytesWritten")
            };
            const CFStringRef readOpKeys[] = {
                CFSTR("Operations (Read)"),
                CFSTR("Read Operations"),
                CFSTR("Reads")
            };
            const CFStringRef writeOpKeys[] = {
                CFSTR("Operations (Write)"),
                CFSTR("Write Operations"),
                CFSTR("Writes")
            };

            uint64_t value = 0;

            if (tryGetDictionaryValue(stats, readByteKeys, arraySize(readByteKeys), value)) {
                totalRead += value;
            }
            if (tryGetDictionaryValue(stats, writeByteKeys, arraySize(writeByteKeys), value)) {
                totalWrite += value;
            }
            if (tryGetDictionaryValue(stats, readOpKeys, arraySize(readOpKeys), value)) {
                totalReadOps += value;
            }
            if (tryGetDictionaryValue(stats, writeOpKeys, arraySize(writeOpKeys), value)) {
                totalWriteOps += value;
            }

            CFRelease(stats);
        }

        IOObjectRelease(object);
    }

    IOObjectRelease(iterator);

    if (!diskStatsInitialized_) {
        prevDiskRead_ = totalRead;
        prevDiskWrite_ = totalWrite;
        prevDiskReadOps_ = totalReadOps;
        prevDiskWriteOps_ = totalWriteOps;
        diskStatsInitialized_ = true;
        diskMetrics_.readBytes = 0;
        diskMetrics_.writeBytes = 0;
        diskMetrics_.readOps = 0;
        diskMetrics_.writeOps = 0;
        return;
    }

    auto rateFromDelta = [intervalSeconds](uint64_t current, uint64_t previous) -> uint64_t {
        if (current <= previous) {
            return 0;
        }
        double delta = static_cast<double>(current - previous);
        return static_cast<uint64_t>(delta / intervalSeconds);
    };

    diskMetrics_.readBytes = rateFromDelta(totalRead, prevDiskRead_);
    diskMetrics_.writeBytes = rateFromDelta(totalWrite, prevDiskWrite_);
    diskMetrics_.readOps = rateFromDelta(totalReadOps, prevDiskReadOps_);
    diskMetrics_.writeOps = rateFromDelta(totalWriteOps, prevDiskWriteOps_);

    prevDiskRead_ = totalRead;
    prevDiskWrite_ = totalWrite;
    prevDiskReadOps_ = totalReadOps;
    prevDiskWriteOps_ = totalWriteOps;
}

void SystemMetrics::updateSystemInfo() {
    auto now = std::chrono::steady_clock::now();
    if (lastSystemInfoSample_.time_since_epoch().count() != 0 &&
        now - lastSystemInfoSample_ < kSystemInfoUpdateInterval) {
        return;
    }
    lastSystemInfoSample_ = now;

    // Get load average
    getloadavg(systemInfo_.loadAverage, 3);
    
    // Get process count
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
    size_t size = 0;
    sysctl(mib, 4, nullptr, &size, nullptr, 0);
    systemInfo_.processCount = size / sizeof(struct kinfo_proc);
    
    // Get CPU count
    size = sizeof(systemInfo_.cpuCount);
    sysctlbyname("hw.ncpu", &systemInfo_.cpuCount, &size, nullptr, 0);
    
    // Initialize IRQ count (macOS doesn't expose IRQ count like Linux)
    // For now, we'll simulate it based on system activity
    systemInfo_.irqCount = 0;
}
