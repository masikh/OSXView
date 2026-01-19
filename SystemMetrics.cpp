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
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace {

const auto kNetworkUpdateInterval = std::chrono::milliseconds(333);
const auto kDiskUpdateInterval = std::chrono::milliseconds(1500);
const auto kSystemInfoUpdateInterval = std::chrono::milliseconds(333);
const auto kGPUUpdateInterval = std::chrono::milliseconds(500);

mach_port_t getIOKitMasterPort() {
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 120000
    if (__builtin_available(macOS 12.0, *)) {
        return kIOMainPortDefault;
    }
#endif
    static mach_port_t masterPort = MACH_PORT_NULL;
    if (masterPort == MACH_PORT_NULL) {
        if (IOMasterPort(MACH_PORT_NULL, &masterPort) != KERN_SUCCESS) {
            return MACH_PORT_NULL;
        }
    }
    return masterPort;
}

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

bool cfStringEquals(CFTypeRef value, CFStringRef expected) {
    if (!value || !expected || CFGetTypeID(value) != CFStringGetTypeID()) {
        return false;
    }
    return CFStringCompare((CFStringRef)value, expected, 0) == kCFCompareEqualTo;
}

bool cfNumberToInt(CFTypeRef value, int& outValue) {
    if (!value || CFGetTypeID(value) != CFNumberGetTypeID()) {
        return false;
    }
    return CFNumberGetValue((CFNumberRef)value, kCFNumberIntType, &outValue);
}

struct SMCKeyData_vers_t {
    char major;
    char minor;
    char build;
    char reserved;
    uint16_t release;
};

struct SMCKeyData_pLimitData_t {
    uint16_t version;
    uint16_t length;
    uint32_t cpuPLimit;
    uint32_t gpuPLimit;
    uint32_t memPLimit;
};

struct SMCKeyData_keyInfo_t {
    uint32_t dataSize;
    uint32_t dataType;
    uint8_t dataAttributes;
};

struct SMCKeyData_t {
    uint32_t key;
    SMCKeyData_vers_t vers;
    SMCKeyData_pLimitData_t pLimitData;
    SMCKeyData_keyInfo_t keyInfo;
    uint8_t result;
    uint8_t status;
    uint8_t data8;
    uint32_t data32;
    uint8_t bytes[32];
};

static_assert(sizeof(SMCKeyData_t) == 80, "SMCKeyData_t size mismatch");

struct SMCReadResult {
    uint32_t dataSize = 0;
    uint32_t dataType = 0;
    uint8_t bytes[32]{};
};

constexpr uint32_t kSMCUserClientMethod = 2;
constexpr uint8_t kSMCCmdReadKey = 5;
constexpr uint8_t kSMCCmdReadKeyInfo = 9;

uint32_t smcKeyFromString(const char* key) {
    if (!key) {
        return 0;
    }
    return (static_cast<uint32_t>(key[0]) << 24)
         | (static_cast<uint32_t>(key[1]) << 16)
         | (static_cast<uint32_t>(key[2]) << 8)
         | static_cast<uint32_t>(key[3]);
}

bool smcCall(io_connect_t connection, SMCKeyData_t* input, SMCKeyData_t* output) {
    if (!input || !output || connection == IO_OBJECT_NULL) {
        return false;
    }
    size_t outputSize = sizeof(SMCKeyData_t);
    kern_return_t kr = IOConnectCallStructMethod(connection,
                                                 kSMCUserClientMethod,
                                                 input,
                                                 sizeof(SMCKeyData_t),
                                                 output,
                                                 &outputSize);
    return kr == KERN_SUCCESS;
}

bool smcReadKey(io_connect_t connection, uint32_t key, SMCReadResult& outResult) {
    SMCKeyData_t input{};
    SMCKeyData_t output{};

    input.key = key;
    input.data8 = kSMCCmdReadKeyInfo;

    if (!smcCall(connection, &input, &output)) {
        return false;
    }

    outResult.dataSize = output.keyInfo.dataSize;
    outResult.dataType = output.keyInfo.dataType;

    input.keyInfo.dataSize = outResult.dataSize;
    input.data8 = kSMCCmdReadKey;

    if (!smcCall(connection, &input, &output)) {
        return false;
    }

    std::memcpy(outResult.bytes, output.bytes, sizeof(outResult.bytes));
    return true;
}

bool smcReadUInt(io_connect_t connection, const char* keyString, uint32_t& outValue) {
    if (!keyString) {
        return false;
    }

    SMCReadResult result;
    if (!smcReadKey(connection, smcKeyFromString(keyString), result)) {
        return false;
    }

    if (result.dataSize == 1) {
        outValue = result.bytes[0];
        return true;
    }

    if (result.dataSize == 2) {
        outValue = (static_cast<uint32_t>(result.bytes[0]) << 8)
                 | static_cast<uint32_t>(result.bytes[1]);
        return true;
    }

    if (result.dataSize == 4) {
        outValue = (static_cast<uint32_t>(result.bytes[0]) << 24)
                 | (static_cast<uint32_t>(result.bytes[1]) << 16)
                 | (static_cast<uint32_t>(result.bytes[2]) << 8)
                 | static_cast<uint32_t>(result.bytes[3]);
        return true;
    }

    return false;
}

bool smcReadFloat(io_connect_t connection, const char* keyString, double& outValue) {
    if (!keyString) {
        return false;
    }

    SMCReadResult result;
    if (!smcReadKey(connection, smcKeyFromString(keyString), result)) {
        return false;
    }

    if (result.dataSize != 4) {
        return false;
    }

    // Convert 4-byte IEEE 754 float to double
    // Use little-endian byte order (bytes[3] is most significant)
    uint32_t raw = (static_cast<uint32_t>(result.bytes[3]) << 24)
                 | (static_cast<uint32_t>(result.bytes[2]) << 16)
                 | (static_cast<uint32_t>(result.bytes[1]) << 8)
                 | static_cast<uint32_t>(result.bytes[0]);
    
    // Use union to reinterpret bits as float
    union {
        uint32_t i;
        float f;
    } u;
    u.i = raw;
    
    outValue = static_cast<double>(u.f);
    
    return true;
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
      networkIter_(0), diskIter_(0), smcConnection_(IO_OBJECT_NULL) {
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

    if (smcConnection_ != IO_OBJECT_NULL) {
        IOServiceClose(smcConnection_);
        smcConnection_ = IO_OBJECT_NULL;
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

    io_service_t smcService = IOServiceGetMatchingService(getIOKitMasterPort(), IOServiceMatching("AppleSMC"));
    if (smcService == IO_OBJECT_NULL) {
        smcService = IOServiceGetMatchingService(getIOKitMasterPort(), IOServiceMatching("AppleSMCKeysEndpoint"));
    }
    if (smcService != IO_OBJECT_NULL) {
        kern_return_t openResult = IOServiceOpen(smcService, mach_task_self(), 0, &smcConnection_);
        IOObjectRelease(smcService);
        if (openResult != KERN_SUCCESS) {
            smcConnection_ = IO_OBJECT_NULL;
        }
    }

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
    updateBattery();
    updateFans();
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
        kern_return_t kr = IOServiceGetMatchingServices(getIOKitMasterPort(), matching, &iterator);
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
    kern_return_t kr = IOServiceGetMatchingServices(getIOKitMasterPort(), matching, &iterator);
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

void SystemMetrics::updateBattery() {
    BatteryMetrics metrics{};

    CFTypeRef powerInfo = IOPSCopyPowerSourcesInfo();
    if (!powerInfo) {
        batteryMetrics_ = metrics;
        return;
    }

    CFArrayRef sources = IOPSCopyPowerSourcesList(powerInfo);
    if (!sources) {
        CFRelease(powerInfo);
        batteryMetrics_ = metrics;
        return;
    }

    CFIndex count = CFArrayGetCount(sources);
    for (CFIndex i = 0; i < count; ++i) {
        CFTypeRef source = CFArrayGetValueAtIndex(sources, i);
        CFDictionaryRef description = IOPSGetPowerSourceDescription(powerInfo, source);
        if (!description || CFGetTypeID(description) != CFDictionaryGetTypeID()) {
            continue;
        }

        CFTypeRef typeValue = CFDictionaryGetValue(description, CFSTR("Type"));
        if (!typeValue || !cfStringEquals(typeValue, CFSTR("InternalBattery"))) {
            continue;
        }

        metrics.isPresent = true;

        CFBooleanRef chargingRef = (CFBooleanRef)CFDictionaryGetValue(description, CFSTR("Is Charging"));
        metrics.isCharging = chargingRef ? CFBooleanGetValue(chargingRef) : false;

        CFTypeRef powerStateValue = CFDictionaryGetValue(description, CFSTR("Power Source State"));
        if (powerStateValue && cfStringEquals(powerStateValue, CFSTR("AC Power"))) {
            metrics.onACPower = true;
        } else if (powerStateValue && cfStringEquals(powerStateValue, CFSTR("Battery Power"))) {
            metrics.onACPower = false;
        }

        CFTypeRef currentCapacityValue = CFDictionaryGetValue(description, CFSTR("Current Capacity"));
        CFTypeRef maxCapacityValue = CFDictionaryGetValue(description, CFSTR("Max Capacity"));
        int cur = 0;
        int max = 0;
        if (currentCapacityValue && maxCapacityValue &&
            cfNumberToInt(currentCapacityValue, cur) &&
            cfNumberToInt(maxCapacityValue, max) &&
            max > 0) {
            metrics.chargePercent = std::clamp(static_cast<double>(cur) / static_cast<double>(max) * 100.0, 0.0, 100.0);
        }

        CFTypeRef timeRemainingValue = CFDictionaryGetValue(
            description,
            metrics.isCharging ? CFSTR("Time to Full Charge") : CFSTR("Time to Empty"));
        int minutes = 0;
        if (timeRemainingValue && cfNumberToInt(timeRemainingValue, minutes)) {
            if (minutes == kIOPSTimeRemainingUnknown || minutes < 0) {
                metrics.timeRemainingMinutes = -1;
            } else {
                metrics.timeRemainingMinutes = minutes;
            }
        }

        break;
    }

    CFRelease(sources);
    CFRelease(powerInfo);

    batteryMetrics_ = metrics;
}

void SystemMetrics::updateFans() {
    if (smcConnection_ == IO_OBJECT_NULL) {
        fanMetrics_.clear();
        return;
    }

    uint32_t fanCount = 0;
    if (!smcReadUInt(smcConnection_, "FNum", fanCount) || fanCount == 0) {
        fanMetrics_.clear();
        return;
    }

    fanCount = std::min<uint32_t>(fanCount, 16);
    fanMetrics_.assign(fanCount, FanMetrics{});

    auto hexDigit = [](uint32_t value) -> char {
        if (value < 10) {
            return static_cast<char>('0' + value);
        }
        return static_cast<char>('A' + (value - 10));
    };

    for (uint32_t i = 0; i < fanCount; ++i) {
        char actualKey[5] = {'F', hexDigit(i), 'A', 'c', '\0'};
        char minKey[5] = {'F', hexDigit(i), 'M', 'n', '\0'};
        char maxKey[5] = {'F', hexDigit(i), 'M', 'x', '\0'};

        double rpm = 0.0;
        bool rpmOk = smcReadFloat(smcConnection_, actualKey, rpm);
        if (rpmOk) {
            fanMetrics_[i].rpm = rpm;
            fanMetrics_[i].valid = true;
        }

        double minRpm = 0.0;
        if (smcReadFloat(smcConnection_, minKey, minRpm)) {
            fanMetrics_[i].minRpm = minRpm;
        }

        double maxRpm = 0.0;
        if (smcReadFloat(smcConnection_, maxKey, maxRpm)) {
            fanMetrics_[i].maxRpm = maxRpm;
        }

        if (!rpmOk) {
            fanMetrics_[i].rpm = 0.0;
            fanMetrics_[i].valid = false;
        }
    }
}
