#pragma once

#include <stdint.h>
#include <string>
#include <stdio.h>
#include <time.h>

class TwoDimensionalBuffer {
public:
    TwoDimensionalBuffer(unsigned char* data, size_t size, int width, int height) 
        : data(data), size(size), width(width), height(height) {
    }
    
    unsigned char* get(int x, int y) {
        if (x < 0 || x >= width || y < 0 || y >= height) {
            return nullptr; // Out of bounds
        }
        return data + (y * width + x) * 4; // Assuming 4 bytes per pixel (ARGB)
    }
private:
    int width;
    int height;
    unsigned char* data;
    size_t size;
};

class FreqMonitor {
    std::string name;
public:
    int interval_ms;
    uint64_t count = 0;
    uint64_t last_print_time = 0;
    FreqMonitor(std::string name, int interval_ms = 5000): name(name), interval_ms(interval_ms) {
        last_print_time = get_current_time();
    }
    void increment() {
        count++;
        uint64_t current_time = get_current_time();
        if (current_time - last_print_time >= interval_ms) {
            float freq = count * 1000.0 / (current_time - last_print_time);
            printf("Frequency: %s %.2fHz\n", name.c_str(), freq);
            count = 0;
            last_print_time = current_time;
        }
    }
    uint64_t get_current_time() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1000 + ts.tv_nsec / 1000000; // milliseconds
    }
};

#include <chrono>
#include <vector>
#include <numeric>
#include <cmath>
#include <algorithm>

class FrameJitterMeasurer {
public:
    FrameJitterMeasurer(double targetFps = 60.0, size_t maxStoredFrames = 300) 
        : targetFrameTime(1000.0 / targetFps), 
          lastFrameTime(std::chrono::high_resolution_clock::now()),
          maxStoredFrames(maxStoredFrames),
          frameTimes(maxStoredFrames, 1000.0 / targetFps) {}

    // Call this at the beginning or end of each frame
    void markFrame() {
        auto now = std::chrono::high_resolution_clock::now();
        double frameDuration = std::chrono::duration<double, std::milli>(now - lastFrameTime).count();
        
        frameTimes.push_back(frameDuration);
        
        // Keep only the last N frames to limit memory usage
        if (frameTimes.size() > maxStoredFrames) {
            frameTimes.erase(frameTimes.begin());
        }
        
        lastFrameTime = now;
    }

    // Get current jitter metrics
    struct JitterMetrics {
        double averageFps;         // Current average FPS
        double jitterRate;        // Percentage of frames that deviate from target
        double maxDeviation;      // Maximum deviation from target frame time (ms)
        double stdDev;            // Standard deviation of frame times (ms)
        size_t totalFrames;       // Total frames measured
    };

    JitterMetrics getMetrics() const {
        JitterMetrics metrics;
        if (frameTimes.empty()) {
            return metrics;
        }

        // Calculate average frame time
        double sum = std::accumulate(frameTimes.begin(), frameTimes.end(), 0.0);
        double averageFrameTime = sum / frameTimes.size();
        metrics.averageFps = 1000.0 / averageFrameTime;

        // Calculate jitter (frames that deviate more than 1% from target)
        size_t jitterCount = std::count_if(frameTimes.begin(), frameTimes.end(), 
            [this](double t) { 
                return std::abs(t - targetFrameTime) > (targetFrameTime * 0.01); 
            });
        metrics.jitterRate = (static_cast<double>(jitterCount) / frameTimes.size()) * 100.0;

        // Calculate max deviation
        auto minmax = std::minmax_element(frameTimes.begin(), frameTimes.end());
        metrics.maxDeviation = std::max(
            std::abs(*minmax.first - targetFrameTime),
            std::abs(*minmax.second - targetFrameTime)
        );

        // Calculate standard deviation
        double sq_sum = std::inner_product(frameTimes.begin(), frameTimes.end(), 
                                         frameTimes.begin(), 0.0);
        metrics.stdDev = std::sqrt(sq_sum / frameTimes.size() - 
                                  averageFrameTime * averageFrameTime);

        metrics.totalFrames = frameTimes.size();

        return metrics;
    }

    // Reset all measurements
    void reset() {
        frameTimes.clear();
        lastFrameTime = std::chrono::high_resolution_clock::now();
    }

private:
    const double targetFrameTime;  // Target frame time in milliseconds (16.666...ms for 60FPS)
    std::chrono::high_resolution_clock::time_point lastFrameTime;
    std::vector<double> frameTimes; // Stores frame times in milliseconds
    
    const size_t maxStoredFrames; // Store up to 10 seconds at 60FPS
};