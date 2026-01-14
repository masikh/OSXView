#ifndef XOSVIEW_DISPLAY_H
#define XOSVIEW_DISPLAY_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <string>
#include "SystemMetrics.h"

class Display {
public:
    Display(int width = 580, int height = 388);
    ~Display();
    
    bool initialize();
    void cleanup();
    
    void beginFrame();
    void endFrame();
    
    void draw(const SystemMetrics& metrics);
    void handleResize(int newWidth, int newHeight);
    
private:
    SDL_Window* window_;
    SDL_Renderer* renderer_;
    TTF_Font* font_;
    int width_;
    int height_;
    
    // Colors
    SDL_Color backgroundColor_;
    SDL_Color textColor_;
    SDL_Color valueColor_;
    SDL_Color labelColor_;
    SDL_Color borderColor_;
    
    // CPU colors
    SDL_Color cpuUserColor_;
    SDL_Color cpuSystemColor_;
    SDL_Color cpuIdleColor_;
    
    // Memory colors
    SDL_Color memUsedColor_;
    SDL_Color memBufferColor_;
    SDL_Color memSlabColor_;
    SDL_Color memFreeColor_;
    
    // Disk colors
    SDL_Color diskReadColor_;
    SDL_Color diskWriteColor_;
    SDL_Color diskIdleColor_;
    
    // Network colors
    SDL_Color netInColor_;
    SDL_Color netOutColor_;
    SDL_Color netIdleColor_;
    
    // IRQ colors
    SDL_Color irqColor_;
    SDL_Color irqIdleColor_;
    
    // Dynamic layout constants
    static const int NUM_METERS = 5;
    static const int METER_SPACING = 40;
    static const int LABEL_X = 10;
    static const int VALUE_X = 100;
    static const int LEGEND_Y_OFFSET = -15;
    
    // Calculated layout
    int meterHeight_;
    int meterYStart_;
    int meterX_;
    int meterWidth_;
    int legendX_;
    int charWidth_;
    int charHeight_;
    int labelWidth_;
    int valueWidth_;
    
    void updateLayout();
    
    void drawCPUMeter(const std::vector<CPUMetrics>& metrics, int y);
    void drawMemoryMeter(const MemoryMetrics& metrics, int y);
    void drawDiskMeter(const DiskMetrics& metrics, int y);
    void drawNetworkMeter(const NetworkMetrics& metrics, int y);
    void drawIRQMeter(int irqCount, int y);
    
    void drawHorizontalMeter(int x, int y, int width, int height,
                           const std::vector<double>& values,
                           const std::vector<SDL_Color>& colors);
    
    void drawText(int x, int y, const std::string& text, const SDL_Color& color);
    void drawRightAlignedText(int x, int y, const std::string& text, const SDL_Color& color);
    void drawMeterBorder(int x, int y, int width, int height);
    void drawLegend(int x, int y, const std::vector<std::string>& labels, 
                   const std::vector<SDL_Color>& colors);
    
    std::string formatBytes(uint64_t bytes) const;
    std::string formatValue(double value, const std::string& unit) const;
};

#endif //XOSVIEW_DISPLAY_H
