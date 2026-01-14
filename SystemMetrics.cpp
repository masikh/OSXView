#include "SystemMetrics.h"
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#include <net/if.h>
#include <net/route.h>
#include <net/if_types.h>
#include <sys/socket.h>
#include <sys/types.h>

SystemMetrics::SystemMetrics() 
    : machPort_(0), prevCpuLoad_(nullptr), numCpus_(0),
      prevNetworkIn_(0), prevNetworkOut_(0), prevPacketsIn_(0), prevPacketsOut_(0),
      prevDiskRead_(0), prevDiskWrite_(0),
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

void SystemMetrics::updateNetwork() {
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
    // Get disk I/O statistics using IOKit
    io_iterator_t iter;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault,
                                                    IOServiceMatching("IOBlockStorageDriver"),
                                                    &iter);
    if (kr != KERN_SUCCESS) {
        return;
    }
    
    uint64_t totalRead = 0, totalWrite = 0;
    uint64_t readOps = 0, writeOps = 0;
    
    io_object_t obj;
    while ((obj = IOIteratorNext(iter)) != 0) {
        CFDictionaryRef stats = (CFDictionaryRef)IORegistryEntryCreateCFProperty(
            obj, CFSTR("IOBlockStorageDriverStatistics"),
            kCFAllocatorDefault, 0);
        
        if (stats) {
            CFNumberRef num;
            
            num = (CFNumberRef)CFDictionaryGetValue(stats, CFSTR("BytesRead"));
            if (num) {
                uint64_t value;
                CFNumberGetValue(num, kCFNumberSInt64Type, &value);
                totalRead += value;
            }
            
            num = (CFNumberRef)CFDictionaryGetValue(stats, CFSTR("BytesWritten"));
            if (num) {
                uint64_t value;
                CFNumberGetValue(num, kCFNumberSInt64Type, &value);
                totalWrite += value;
            }
            
            num = (CFNumberRef)CFDictionaryGetValue(stats, CFSTR("Reads"));
            if (num) {
                uint32_t value;
                CFNumberGetValue(num, kCFNumberSInt32Type, &value);
                readOps += value;
            }
            
            num = (CFNumberRef)CFDictionaryGetValue(stats, CFSTR("Writes"));
            if (num) {
                uint32_t value;
                CFNumberGetValue(num, kCFNumberSInt32Type, &value);
                writeOps += value;
            }
            
            CFRelease(stats);
        }
        
        IOObjectRelease(obj);
    }
    
    IOObjectRelease(iter);
    
    diskMetrics_.readBytes = totalRead - prevDiskRead_;
    diskMetrics_.writeBytes = totalWrite - prevDiskWrite_;
    diskMetrics_.readOps = readOps;
    diskMetrics_.writeOps = writeOps;
    
    prevDiskRead_ = totalRead;
    prevDiskWrite_ = totalWrite;
}

void SystemMetrics::updateSystemInfo() {
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
