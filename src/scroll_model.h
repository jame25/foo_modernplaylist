#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace modern_playlist {
// Device-pixel geometry. Inputs change the target; only tick advances display.
class scroll_model {
public:
    double target = 0, displayed = 0;
    double maximum = 0;
    void extent(double content, double viewport) {
        maximum = std::max(0.0, content - std::max(0.0, viewport));
        target = clamp(target); displayed = clamp(displayed);
        if (!maximum) cancel_inertia();
    }
    double clamp(double value) const { return std::clamp(value, 0.0, maximum); }
    void to(double value) { target = clamp(std::round(value)); }
    void by(double value) { to(target + value); }
    void reset(double value = 0) { cancel_inertia(); to(value); displayed = target; }
    bool moving() const { return target != displayed || multiplier_ >= 1; }
    void cancel_inertia() { multiplier_ = delta_ = inertia_ms_ = 0; }
    void interrupt(double row_height) {
        cancel_inertia();
        to(displayed + std::clamp(target - displayed, -row_height, row_height));
    }
    void fling(double gesture_ms) {
        cancel_inertia();
        if (std::abs(target - displayed) <= 30) return;
        delta_ = std::round((target - displayed) / 30);
        multiplier_ = std::clamp((1000 - gesture_ms) / 20, 1.0, 50.0);
    }
    bool tick(double elapsed_ms) {
        const double before = displayed;
        // Cap a suspended frame: never run an unbounded catch-up loop on resume.
        const double elapsed = std::clamp(elapsed_ms, 0.0, 100.0);
        if (multiplier_ >= 1) {
            inertia_ms_ += elapsed;
            while (inertia_ms_ >= 75 && multiplier_ >= 1) {
                inertia_ms_ -= 75;
                const double old = target;
                by(delta_ * multiplier_);
                --multiplier_; delta_ *= 0.9;
                if (old == target) cancel_inertia();
            }
        }
        target = clamp(target);
        // Same time constant as the outline's 2.5 lerp at 35 ms, at any cadence.
        displayed += (target - displayed) * (1 - std::pow(0.6, elapsed / 35));
        if (std::abs(target - displayed) < 1) displayed = target;
        return before != displayed;
    }
    void reveal(std::size_t row, double height, double viewport) {
        const double top = double(row) * height;
        if (top < target) to(top);
        else if (top + height > target + viewport) to(top + height - viewport);
    }
    std::pair<std::size_t, std::size_t> visible(std::size_t count, double height, double viewport) const {
        if (height <= 0 || viewport <= 0) return {0, 0};
        const auto first = std::min(count, static_cast<std::size_t>(std::floor(displayed / height)));
        const auto end = std::min(count, static_cast<std::size_t>(std::ceil((displayed + viewport) / height)));
        return {first, end};
    }
    int hit(double y, double height, double viewport, std::size_t count) const {
        if (y < 0 || y >= viewport || height <= 0) return -1;
        const auto row = static_cast<std::size_t>(std::floor((y + displayed) / height));
        return row < count ? static_cast<int>(row) : -1;
    }
private:
    double delta_ = 0, multiplier_ = 0, inertia_ms_ = 0;
};
}
