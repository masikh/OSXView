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
    
    // Initialize display 580 388 -> 280 194
    Display display(280, 194);
    if (!display.initialize()) {
        std::cerr << "Failed to initialize display" << std::endl;
        return 1;
    }
    
    std::cout << "OSXview started - Press Ctrl+C to exit" << std::endl;
    
    // Main loop
    auto lastUpdate = std::chrono::steady_clock::now();
    const std::chrono::milliseconds updateInterval(333); // Update every 1/3 second
    
    while (running) {
        auto now = std::chrono::steady_clock::now();
        
        // Update metrics at the specified interval
        if (now - lastUpdate >= updateInterval) {
            metrics.update();
            lastUpdate = now;
        }
        
        // Handle SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
                break;
            } else if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    display.handleResize(event.window.data1, event.window.data2);
                }
            }
        }
        
        // Begin frame
        display.beginFrame();
        
        // Draw all components
        display.draw(metrics);
        
        // End frame
        display.endFrame();
        
        // Small delay to prevent CPU spinning
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "\nShutting down Xosview..." << std::endl;
    
    return 0;
}