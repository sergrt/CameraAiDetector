#pragma once

#include <cmath>

struct StreamProperties {
    double fps{0.0};
    int height{0};
    int width{0};
    bool operator==(const StreamProperties& other) const {
        return height == other.height && width == other.width && fabs(fps - other.fps) < 0.1;
    }
};
