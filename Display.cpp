#include "Display.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <unordered_map>

namespace {

struct CachedTextEntry {
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
};

std::unordered_map<std::string, CachedTextEntry> gTextCache;
int gActiveFontSize = 0;

void clearTextCache() {
    for (auto& entry : gTextCache) {
        if (entry.second.texture) {
            SDL_DestroyTexture(entry.second.texture);
        }
    }
    gTextCache.clear();
}

void setActiveFontSize(int fontSize) {
    if (fontSize != gActiveFontSize) {
        clearTextCache();
        gActiveFontSize = fontSize;
    }
}

std::string makeCacheKey(const std::string& text, const SDL_Color& color) {
    std::ostringstream oss;
    oss << gActiveFontSize << '|'
        << static_cast<int>(color.r) << ','
        << static_cast<int>(color.g) << ','
        << static_cast<int>(color.b) << ','
        << static_cast<int>(color.a) << '|'
        << text;
    return oss.str();
}

CachedTextEntry* ensureCachedText(SDL_Renderer* renderer,
                                  TTF_Font* font,
                                  const std::string& text,
                                  const SDL_Color& color) {
    if (!renderer || !font) {
        return nullptr;
    }
    
    const std::string key = makeCacheKey(text, color);
    auto it = gTextCache.find(key);
    if (it == gTextCache.end()) {
        SDL_Surface* surface = TTF_RenderText_Solid(font, text.c_str(), color);
        if (!surface) {
            return nullptr;
        }
        
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (!texture) {
            SDL_FreeSurface(surface);
            return nullptr;
        }
        
        CachedTextEntry entry;
        entry.texture = texture;
        entry.width = surface->w;
        entry.height = surface->h;
        SDL_FreeSurface(surface);
        
        it = gTextCache.emplace(key, entry).first;
    }
    
    return &it->second;
}

} // namespace

Display::Display(int width, int height) 
    : window_(nullptr), renderer_(nullptr), font_(nullptr), width_(width), height_(height),
      backgroundColor_{64, 64, 94, 255},  // RGB(64, 64, 64)
      textColor_{255, 255, 255, 255},
      valueColor_{89, 135, 96, 255},
      labelColor_{203, 203, 69, 255},
      borderColor_{255, 255, 0, 255},  // Bright yellow borders
      cpuUserColor_{255, 0, 0, 255},    // Red for user
      cpuSystemColor_{255, 165, 0, 255}, // Orange for system
      cpuIdleColor_{0, 0, 0, 255},      // Black for idle
      memUsedColor_{74, 137, 92, 255},    // Custom green for used
      memBufferColor_{255, 165, 0, 255}, // Orange for buffers
      memSlabColor_{0, 100, 255, 255},   // Dark blue for slab
      memFreeColor_{255, 0, 0, 255},     // Red for free
      diskReadColor_{159, 215, 244, 255}, // White for read label
      diskWriteColor_{127, 112, 247, 255},    // Red for write
      diskIdleColor_{0, 0, 0, 255},       // Black for idle
      netInColor_{159, 215, 244, 255},        // Red for in
      netOutColor_{127, 112, 247, 255},   // White for out label
      netIdleColor_{0, 0, 0, 255},        // Black for idle
      irqColor_{255, 0, 0, 255},          // Red for IRQs
      irqIdleColor_{0, 0, 0, 255} {       // Black for idle
    updateLayout();
}

Display::~Display() {
    cleanup();
}

bool Display::initialize() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return false;
    }
    
    if (TTF_Init() < 0) {
        SDL_Quit();
        return false;
    }
    
    window_ = SDL_CreateWindow("OSXView", SDL_WINDOWPOS_UNDEFINED, 
                              SDL_WINDOWPOS_UNDEFINED, width_, height_, 
                              SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (!window_) {
        TTF_Quit();
        SDL_Quit();
        return false;
    }
    
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) {
        SDL_DestroyWindow(window_);
        TTF_Quit();
        SDL_Quit();
        return false;
    }
    
    // Get actual window size (might differ from requested due to DPI)
    SDL_GetWindowSize(window_, &width_, &height_);
    
    // For high DPI displays, get the drawable size
    int drawableWidth, drawableHeight;
    SDL_GL_GetDrawableSize(window_, &drawableWidth, &drawableHeight);
    
    // If drawable size is different, update our dimensions
    if (drawableWidth != width_ || drawableHeight != height_) {
        width_ = drawableWidth;
        height_ = drawableHeight;
    }
    
    // Update layout with actual window dimensions
    handleResize(width_, height_);
    // updateLayout();
    
    // Load macOS system font - try common monospace fonts
    const char* fontPaths[] = {
        "/System/Library/Fonts/Monaco.ttc",
        "/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/Courier New.ttf",
        "/Library/Fonts/Courier New.ttf",
        nullptr
    };
    
    // Calculate initial font size
    int initialFontSize = std::max(19, height_ / 20);
    
    for (int i = 0; fontPaths[i]; i++) {
        font_ = TTF_OpenFont(fontPaths[i], initialFontSize);
        if (font_) {
            break;
        }
    }
    
    if (!font_) {
        // Fallback to default-font
        font_ = TTF_OpenFont("/System/Library/Fonts/Helvetica.ttc", initialFontSize);
    }
    
    setActiveFontSize(initialFontSize);
    
    return true;
}

void Display::cleanup() {
    clearTextCache();
    clearDynamicTextCache();
    
    if (font_) {
        TTF_CloseFont(font_);
        font_ = nullptr;
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    TTF_Quit();
    SDL_Quit();
}

void Display::beginFrame() {
    SDL_SetRenderDrawColor(renderer_, backgroundColor_.r, backgroundColor_.g, 
                          backgroundColor_.b, backgroundColor_.a);
    SDL_RenderClear(renderer_);
}

void Display::endFrame() {
    SDL_RenderPresent(renderer_);
}

void Display::handleResize(int /* newWidth */, int /* newHeight */) {
    // Don't use the event size - get the actual drawable size
    // This ensures we use the correct size on high DPI displays
    int drawableWidth, drawableHeight;
    SDL_GL_GetDrawableSize(window_, &drawableWidth, &drawableHeight);
    
    width_ = drawableWidth;
    height_ = drawableHeight;
    updateLayout();
}

void Display::updateLayout() {
    // SDL_GetWindowSize(window_, &width_, &height_);
    // Debug output
    printf("Window size: %dx%d\n", width_, height_);
    
    // Exact calculations - no magic numbers
    // Divide window height evenly for 5 meters with spacing
    meterHeight_ = (height_ - (NUM_METERS + 1) * METER_SPACING) / NUM_METERS;
    meterYStart_ = METER_SPACING;  // Add extra spacing at top
    
    // Calculate exact positions
    labelWidth_ = width_ / 8 + METER_SPACING;      // 12.5% of window width
    valueWidth_ = width_ / 8;      // 12.5% of window width
    meterX_ = labelWidth_ + valueWidth_ + METER_SPACING;
    meterWidth_ = width_ - labelWidth_ - 24;  // Use remaining space for meters
    legendX_ = meterX_ + meterWidth_ + METER_SPACING;
    
    printf("Meter width: %d (from x=%d to x=%d)\n", meterWidth_, meterX_, meterX_ + meterWidth_);
    
    // Font size proportional to window
    int fontSize = std::max(19, height_ / 20);
    if (font_) {
        if (TTF_SetFontSize(font_, fontSize) == 0) {
            setActiveFontSize(fontSize);
            clearDynamicTextCache();
        }
    }
    charWidth_ = fontSize * 0.6;   // Approximate character width
    charHeight_ = fontSize;
    
    printf("Font size: %d (height=%d)\n", fontSize, height_);
    
    // Ensure minimum sizes
    charWidth_ = std::max(8, charWidth_);
    charHeight_ = std::max(10, charHeight_);
    meterHeight_ = std::max(20, meterHeight_);
}

void Display::draw(const SystemMetrics& metrics) {
    // Draw each meter with calculated Y position
    int y = meterYStart_;
    
    drawCPUMeter(metrics.getCPUMetrics(), y);
    y += meterHeight_ + METER_SPACING;
    
    drawMemoryMeter(metrics.getMemoryMetrics(), y);
    y += meterHeight_ + METER_SPACING;
    
    drawDiskMeter(metrics.getDiskMetrics(), y);
    y += meterHeight_ + METER_SPACING;
    
    drawNetworkMeter(metrics.getNetworkMetrics(), y);
}

void Display::drawCPUMeter(const std::vector<CPUMetrics>& metrics, int y) {
    // Draw label and value at calculated positions
    drawText(4, y + meterHeight_/2 - charHeight_/2, "CPU", labelColor_);
    
    double user = 0, system = 0, idle = 100;
    if (!metrics.empty()) {
        user = metrics[0].user;
        system = metrics[0].system;
        idle = metrics[0].idle;
    }
    
    // drawRightAlignedText(labelWidth_ + valueWidth_, y + meterHeight_/2 - charHeight_/2, formatValue(user + system, "%"), valueColor_);
    drawRightAlignedDynamicText("cpu_total",
                                labelWidth_,
                                y + meterHeight_/2 - charHeight_/2,
                                formatValue(user + system, "%"),
                                valueColor_);

    // Draw legend above the bar
    std::vector<std::string> labels = {"USR", "SYS", "IDLE"};
    std::vector<SDL_Color> colors = {cpuUserColor_, cpuSystemColor_, cpuIdleColor_};
    drawLegend(labelWidth_ + 16, y - charHeight_ - 5, labels, colors);
    
    // Draw horizontal meter
    std::vector<double> values = {user, system, idle};
    std::vector<SDL_Color> meterColors = {cpuUserColor_, cpuSystemColor_, cpuIdleColor_};
    drawHorizontalMeter(labelWidth_ + 16, y, meterWidth_, meterHeight_, values, meterColors);
}

void Display::drawMemoryMeter(const MemoryMetrics& metrics, int y) {
    // Draw label and value
    drawText(4, y + meterHeight_/2 - charHeight_/2, "MEM", labelColor_);
    
    double usedGB = metrics.used / (1024.0 * 1024.0 * 1024.0);
    drawRightAlignedDynamicText("mem_used",
                                labelWidth_,
                                y + meterHeight_/2 - charHeight_/2,
                                formatValue(usedGB, "G"),
                                valueColor_);
    
    // Draw legend above the bar
    std::vector<std::string> labels = {"USED", "BUFF", "SLAB", "FREE"};
    std::vector<SDL_Color> colors = {valueColor_, {173, 216, 230, 255}, {0, 0, 139, 255}, memFreeColor_};
    drawLegend(labelWidth_ + 16, y - charHeight_ - 5, labels, colors);
    
    // Calculate memory components
    double used = (double)metrics.used / metrics.total * 100.0;
    double buffer = 2.0; // Simulated buffer
    double slab = (double)metrics.inactive / metrics.total * 100.0;
    double free = (double)metrics.free / metrics.total * 100.0;
    
    // Draw horizontal meter
    std::vector<double> values = {used, buffer, slab, free};
    std::vector<SDL_Color> meterColors = {memUsedColor_, memBufferColor_, memSlabColor_, memFreeColor_};
    drawHorizontalMeter(labelWidth_ + 16, y, meterWidth_, meterHeight_, values, meterColors);
}

void Display::drawDiskMeter(const DiskMetrics& metrics, int y) {
    // Draw label and value
    drawText(4, y + meterHeight_/2 - charHeight_/2, "DISK", labelColor_);
    
    std::string valStr = formatBytes(metrics.readBytes + metrics.writeBytes) + "/s";
    drawRightAlignedDynamicText("disk_total",
                                labelWidth_,
                                y + meterHeight_/2 - charHeight_/2,
                                valStr,
                                valueColor_);
    
    // Draw legend above the bar
    std::vector<std::string> labels = {"READ", "WRITE", "IDLE"};
    std::vector<SDL_Color> colors = {textColor_, diskWriteColor_, cpuIdleColor_};
    drawLegend(labelWidth_ + 16, y - charHeight_ - 5, labels, colors);
    
    // Calculate disk usage using a logarithmic scale to avoid instant saturation
    double maxBytes = 500.0 * 1024.0 * 1024.0; // 500MB/s as ~100%
    auto logPercent = [maxBytes](double value) -> double {
        if (value <= 0.0) {
            return 0.0;
        }
        double denom = std::log10(1.0 + maxBytes);
        if (denom <= 0.0) {
            return 0.0;
        }
        double pct = std::log10(1.0 + value) / denom * 100.0;
        return std::min(100.0, pct);
    };
    
    double totalBytes = metrics.readBytes + metrics.writeBytes;
    double totalPct = logPercent(totalBytes);
    double readRatio = totalBytes > 0 ? static_cast<double>(metrics.readBytes) / totalBytes : 0.0;
    double writeRatio = totalBytes > 0 ? static_cast<double>(metrics.writeBytes) / totalBytes : 0.0;
    double read = totalPct * readRatio;
    double write = totalPct * writeRatio;
    double idle = std::max(0.0, 100.0 - std::min(100.0, read + write));
    
    // Draw horizontal meter
    std::vector<double> values = {read, write, idle};
    std::vector<SDL_Color> meterColors = {diskReadColor_, diskWriteColor_, diskIdleColor_};
    drawHorizontalMeter(labelWidth_ + 16, y, meterWidth_, meterHeight_, values, meterColors);
}

void Display::drawNetworkMeter(const NetworkMetrics& metrics, int y) {
    // Draw label and value
    drawText(4, y + meterHeight_/2 - charHeight_/2, "NET", labelColor_);
    
    std::string valStr = formatBytes(metrics.bytesIn + metrics.bytesOut);
    drawRightAlignedDynamicText("net_total",
                                labelWidth_,
                                y + meterHeight_/2 - charHeight_/2,
                                valStr,
                                valueColor_);
    
    // Draw legend above the bar
    std::vector<std::string> labels = {"IN", "OUT", "IDLE"};
    std::vector<SDL_Color> colors = {netInColor_, netOutColor_, cpuIdleColor_};
    drawLegend(labelWidth_ + 16, y - charHeight_ - 5, labels, colors);
    
    // Calculate network usage using a logarithmic scale similar to disk
    double maxBytes = 2.0 * 1024.0 * 1024.0 * 1024.0; // 2GB/s ~= 100%
    auto logPercent = [maxBytes](double value) -> double {
        if (value <= 0.0) {
            return 0.0;
        }
        double denom = std::log10(1.0 + maxBytes);
        if (denom <= 0.0) {
            return 0.0;
        }
        double pct = std::log10(1.0 + value) / denom * 100.0;
        return std::min(100.0, pct);
    };
    
    double totalBytes = metrics.bytesIn + metrics.bytesOut;
    double totalPct = logPercent(totalBytes);
    double inRatio = totalBytes > 0 ? static_cast<double>(metrics.bytesIn) / totalBytes : 0.0;
    double outRatio = totalBytes > 0 ? static_cast<double>(metrics.bytesOut) / totalBytes : 0.0;
    double in = totalPct * inRatio;
    double out = totalPct * outRatio;
    double idle = std::max(0.0, 100.0 - std::min(100.0, in + out));
    
    // Draw horizontal meter
    std::vector<double> values = {in, out, idle};
    std::vector<SDL_Color> meterColors = {netInColor_, netOutColor_, netIdleColor_};
    drawHorizontalMeter(labelWidth_ + 16, y, meterWidth_, meterHeight_, values, meterColors);
}

void Display::drawIRQMeter(int irqCount, int y) {
    // Draw label and value
    drawText(4, y + meterHeight_/2 - charHeight_/2, "IRQS", labelColor_);
    drawRightAlignedDynamicText("irq_count",
                                labelWidth_,
                                y + meterHeight_/2 - charHeight_/2,
                                std::to_string(irqCount),
                                valueColor_);
    
    // Draw legend above the bar
    std::vector<std::string> labels = {"IRQs per sec", "IDLE"};
    std::vector<SDL_Color> colors = {irqColor_, cpuIdleColor_};
    drawLegend(labelWidth_ + 16, y - charHeight_ - 5, labels, colors);
    
    // Calculate IRQ usage as percentage (max 1000 IRQs/sec as 100%)
    double irqUsage = std::min(100.0, (double)irqCount / 10.0);
    double idle = std::max(0.0, 100.0 - irqUsage);
    
    // Draw horizontal meter
    std::vector<double> values = {irqUsage, idle};
    std::vector<SDL_Color> meterColors = {irqColor_, irqIdleColor_};
    drawHorizontalMeter(labelWidth_ + 16, y, meterWidth_, meterHeight_, values, meterColors);
}

void Display::drawHorizontalMeter(int x, int y, int width, int height,
                                const std::vector<double>& values,
                                const std::vector<SDL_Color>& colors) {
    // Draw border
    drawMeterBorder(x, y, width, height);
    
    // Draw meter segments
    int currentX = x + 2;
    double total = 0;
    for (double v : values) total += v;
    
    for (size_t i = 0; i < values.size(); i++) {
        int segmentWidth = total > 0 ? (int)(values[i] / 100.0 * (width - 4)) : 0;
        
        if (segmentWidth > 0) {
            SDL_Rect segment{currentX, y + 2, segmentWidth, height - 4};
            SDL_SetRenderDrawColor(renderer_, colors[i].r, colors[i].g, 
                                 colors[i].b, colors[i].a);
            SDL_RenderFillRect(renderer_, &segment);
        }
        
        currentX += segmentWidth;
    }
}

void Display::drawLegend(int x, int y, const std::vector<std::string>& labels, 
                        const std::vector<SDL_Color>& colors) {
    int currentX = x;
    for (size_t i = 0; i < labels.size(); i++) {
        drawText(currentX, y, labels[i], colors[i]);
        
        // Get actual text width for proper spacing
        if (font_) {
            int textWidth;
            TTF_SizeText(font_, labels[i].c_str(), &textWidth, nullptr);
            currentX += textWidth + charWidth_ * 2;
        } else {
            currentX += (int)labels[i].length() * charWidth_ + charWidth_ * 2;
        }
    }
}

Display::DynamicTextEntry* Display::prepareDynamicText(const std::string& key,
                                                       const std::string& text,
                                                       const SDL_Color& color) {
    if (!renderer_ || !font_) {
        return nullptr;
    }
    
    auto& entry = dynamicTextCache_[key];
    bool needsUpdate = (entry.texture == nullptr) ||
                       (entry.lastText != text) ||
                       (entry.color.r != color.r ||
                        entry.color.g != color.g ||
                        entry.color.b != color.b ||
                        entry.color.a != color.a);
    
    if (needsUpdate) {
        if (entry.texture) {
            SDL_DestroyTexture(entry.texture);
            entry.texture = nullptr;
        }
        
        SDL_Surface* surface = TTF_RenderText_Solid(font_, text.c_str(), color);
        if (!surface) {
            return nullptr;
        }
        
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
        if (!texture) {
            SDL_FreeSurface(surface);
            return nullptr;
        }
        
        entry.texture = texture;
        entry.width = surface->w;
        entry.height = surface->h;
        entry.lastText = text;
        entry.color = color;
        SDL_FreeSurface(surface);
    }
    
    return &entry;
}

void Display::drawDynamicText(const std::string& key, int x, int y,
                              const std::string& text, const SDL_Color& color) {
    DynamicTextEntry* entry = prepareDynamicText(key, text, color);
    if (!entry || !entry->texture) {
        return;
    }
    
    SDL_Rect dstRect{x, y, entry->width, entry->height};
    SDL_RenderCopy(renderer_, entry->texture, nullptr, &dstRect);
}

void Display::drawRightAlignedDynamicText(const std::string& key, int x, int y,
                                          const std::string& text, const SDL_Color& color) {
    DynamicTextEntry* entry = prepareDynamicText(key, text, color);
    if (!entry || !entry->texture) {
        return;
    }
    
    SDL_Rect dstRect{x - entry->width, y, entry->width, entry->height};
    SDL_RenderCopy(renderer_, entry->texture, nullptr, &dstRect);
}

void Display::clearDynamicTextCache() {
    for (auto& kv : dynamicTextCache_) {
        if (kv.second.texture) {
            SDL_DestroyTexture(kv.second.texture);
        }
    }
    dynamicTextCache_.clear();
}

void Display::drawText(int x, int y, const std::string& text, const SDL_Color& color) {
    if (!renderer_ || !font_) return;
    
    CachedTextEntry* cached = ensureCachedText(renderer_, font_, text, color);
    if (!cached) {
        return;
    }
    
    SDL_Rect dstRect{x, y, cached->width, cached->height};
    SDL_RenderCopy(renderer_, cached->texture, nullptr, &dstRect);
}

void Display::drawRightAlignedText(int x, int y, const std::string& text, const SDL_Color& color) {
    if (!renderer_ || !font_) return;
    
    CachedTextEntry* cached = ensureCachedText(renderer_, font_, text, color);
    if (!cached) {
        return;
    }
    
    SDL_Rect dstRect{x - cached->width, y, cached->width, cached->height};
    SDL_RenderCopy(renderer_, cached->texture, nullptr, &dstRect);
}

void Display::drawMeterBorder(int x, int y, int width, int height) {
    SDL_SetRenderDrawColor(renderer_, borderColor_.r, borderColor_.g, 
                         borderColor_.b, borderColor_.a);
    
    // Draw rectangle border
    SDL_Rect rect{x, y, width, height};
    SDL_RenderDrawRect(renderer_, &rect);
    
    // Make it brighter/thicker by drawing again
    SDL_RenderDrawRect(renderer_, &rect);
}

std::string Display::formatBytes(uint64_t bytes) const {
    const char* units[] = {"", "K", "M", "G", "T"};
    double value = bytes;
    int unit = 0;
    
    while (value >= 1024 && unit < 4) {
        value /= 1024;
        unit++;
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0) << value << units[unit];
    return oss.str();
}

std::string Display::formatValue(double value, const std::string& unit) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0) << value << unit;
    return oss.str();
}
