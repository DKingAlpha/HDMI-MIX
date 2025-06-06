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
