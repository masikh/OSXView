#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include "SystemMetrics.h"
#include "Display.h"

volatile sig_atomic_t running = 1;

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
    Display display(280, 200);
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
    
    while (running) {
        auto now = std::chrono::steady_clock::now();
        
        // Update metrics at the specified interval
        if (now - lastUpdate >= updateInterval) {
            metrics.update();
            lastUpdate = now;
            needsRender = true;
        }
        
        if (needsRender) {
            display.beginFrame();
            display.draw(metrics);
            display.endFrame();
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