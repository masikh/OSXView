#ifndef OSXVIEW_SYSTEMMETRICS_H
#define OSXVIEW_SYSTEMMETRICS_H

#include <vector>
#include <cstdint>
#include <unistd.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/vm_statistics.h>
#include <chrono>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

struct CPUMetrics {
    double user;
    double system;
    double idle;
    double total;
};

struct MemoryMetrics {
    uint64_t total;
    uint64_t used;
    uint64_t free;
    uint64_t active;
    uint64_t inactive;
    uint64_t wired;
};

struct NetworkMetrics {
    uint64_t bytesIn;
    uint64_t bytesOut;
    uint64_t packetsIn;
    uint64_t packetsOut;
};

struct DiskMetrics {
    uint64_t readBytes;
    uint64_t writeBytes;
    uint64_t readOps;
    uint64_t writeOps;
};

struct GPUMetrics {
    double deviceUtilization = 0.0;
    double rendererUtilization = 0.0;
    double tilerUtilization = 0.0;
    bool valid = false;
};

struct SystemInfo {
    double loadAverage[3];
    int processCount;
    int cpuCount;
    int irqCount;
};

class SystemMetrics {
public:
    SystemMetrics();
    ~SystemMetrics();
    
    bool initialize();
    void update();
    
    std::vector<CPUMetrics> getCPUMetrics() const { return cpuMetrics_; }
    MemoryMetrics getMemoryMetrics() const { return memoryMetrics_; }
    MemoryMetrics getSwapMetrics() const { return swapMetrics_; }
    GPUMetrics getGPUMetrics() const { return gpuMetrics_; }
    NetworkMetrics getNetworkMetrics() const { return networkMetrics_; }
    DiskMetrics getDiskMetrics() const { return diskMetrics_; }
    SystemInfo getSystemInfo() const { return systemInfo_; }
    int getIRQCount() const { return systemInfo_.irqCount; }
    
private:
    void updateCPU();
    void updateMemory();
    void updateSwap();
    void updateGPU();
    void updateNetwork();
    void updateDisk();
    void updateSystemInfo();
    
    std::vector<CPUMetrics> cpuMetrics_;
    MemoryMetrics memoryMetrics_;
    MemoryMetrics swapMetrics_;
    GPUMetrics gpuMetrics_;
    NetworkMetrics networkMetrics_;
    DiskMetrics diskMetrics_;
    SystemInfo systemInfo_;
    
    mach_port_t machPort_;
    processor_cpu_load_info_t prevCpuLoad_;
    unsigned int numCpus_;
    uint64_t prevNetworkIn_;
    uint64_t prevNetworkOut_;
    uint64_t prevPacketsIn_;
    uint64_t prevPacketsOut_;
    uint64_t prevDiskRead_;
    uint64_t prevDiskWrite_;
    uint64_t prevDiskReadOps_;
    uint64_t prevDiskWriteOps_;
    bool diskStatsInitialized_;
    std::chrono::steady_clock::time_point lastDiskSample_;
    std::chrono::steady_clock::time_point lastNetworkSample_;
    std::chrono::steady_clock::time_point lastSystemInfoSample_;
    std::chrono::steady_clock::time_point lastGpuSample_;
    
    io_iterator_t networkIter_;
    io_iterator_t diskIter_;
};

#endif //OSXVIEW_SYSTEMMETRICS_H
