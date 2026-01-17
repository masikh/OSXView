#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include <limits>
#include <cstdint>
#include "SystemMetrics.h"
#include "Display.h"

volatile sig_atomic_t running = 1;

#ifdef OSXVIEW_PROFILE
struct PhaseStats {
    double totalMs = 0.0;
    double minMs = std::numeric_limits<double>::max();
    double maxMs = 0.0;
    std::uint64_t samples = 0;
};
#endif

void signalHandler(int /* signal */) {
    running = 0;
}

int main() {
    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Initialize system metrics collector
    SystemMetrics metrics;
    if (!metrics.initialize()) {
        std::cerr << "Failed to initialize system metrics" << std::endl;
        return 1;
    }
    
    // Initialize display 580 388 -> 280 120
    Display display(400, 200);
    if (!display.initialize()) {
        std::cerr << "Failed to initialize display" << std::endl;
        return 1;
    }
    
    std::cout << "OSXview started - Press Ctrl+C to exit" << std::endl;
    
    // Main loop
    const std::chrono::milliseconds updateInterval(333); // Update every 1/3 second
    auto lastUpdate = std::chrono::steady_clock::now() - updateInterval;
    bool needsRender = true;
    
    auto handleEvent = [&](const SDL_Event& event) {
        if (event.type == SDL_QUIT) {
            running = 0;
            return;
        }
        
        if (event.type == SDL_WINDOWEVENT) {
            switch (event.window.event) {
                case SDL_WINDOWEVENT_RESIZED:
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    display.handleResize(event.window.data1, event.window.data2);
                    needsRender = true;
                    break;
                case SDL_WINDOWEVENT_EXPOSED:
                    needsRender = true;
                    break;
                default:
                    break;
            }
        }
    };
    
    #ifdef OSXVIEW_PROFILE
    constexpr std::uint64_t kProfileReportEvery = 120;
    PhaseStats updateStats;
    PhaseStats renderStats;
    auto resetStats = [](PhaseStats& stats) {
        stats.totalMs = 0.0;
        stats.minMs = std::numeric_limits<double>::max();
        stats.maxMs = 0.0;
        stats.samples = 0;
    };
    resetStats(updateStats);
    resetStats(renderStats);
    auto recordSample = [&](PhaseStats& stats, double elapsedMs, const char* label) {
        stats.totalMs += elapsedMs;
        if (elapsedMs < stats.minMs) {
            stats.minMs = elapsedMs;
        }
        if (elapsedMs > stats.maxMs) {
            stats.maxMs = elapsedMs;
        }
        stats.samples++;
        if (stats.samples >= kProfileReportEvery) {
            double avg = stats.totalMs / static_cast<double>(stats.samples);
            std::cout << "[profile] " << label << ": avg " << avg
                      << " ms (min " << stats.minMs
                      << ", max " << stats.maxMs
                      << ") over " << stats.samples << " samples" << std::endl;
            resetStats(stats);
        }
    };
    #endif

    while (running) {
        auto now = std::chrono::steady_clock::now();
        
        // Update metrics at the specified interval
        if (now - lastUpdate >= updateInterval) {
            #ifdef OSXVIEW_PROFILE
            auto updateStart = std::chrono::steady_clock::now();
            #endif
            metrics.update();
            #ifdef OSXVIEW_PROFILE
            auto updateEnd = std::chrono::steady_clock::now();
            double updateMs = std::chrono::duration<double, std::milli>(updateEnd - updateStart).count();
            recordSample(updateStats, updateMs, "metrics.update()");
            #endif
            lastUpdate = now;
            needsRender = true;
        }
        
        if (needsRender) {
            #ifdef OSXVIEW_PROFILE
            auto renderStart = std::chrono::steady_clock::now();
            #endif
            display.beginFrame();
            display.draw(metrics);
            display.endFrame();
            #ifdef OSXVIEW_PROFILE
            auto renderEnd = std::chrono::steady_clock::now();
            double renderMs = std::chrono::duration<double, std::milli>(renderEnd - renderStart).count();
            recordSample(renderStats, renderMs, "display frame");
            #endif
            needsRender = false;
        }
        
        auto nextUpdateTime = lastUpdate + updateInterval;
        auto timeToNextUpdate = nextUpdateTime - std::chrono::steady_clock::now();
        int waitMs = 0;
        if (timeToNextUpdate > std::chrono::milliseconds::zero()) {
            waitMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timeToNextUpdate).count());
        }
        
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, waitMs)) {
            handleEvent(event);
            
            // Flush any additional queued events without spinning
            while (SDL_PollEvent(&event)) {
                handleEvent(event);
            }
        }
    }
    
    std::cout << "\nShutting down OSXview..." << std::endl;
    
    return 0;
}