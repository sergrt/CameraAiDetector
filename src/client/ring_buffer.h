#pragma once

#include <algorithm>
#include <vector>

template <typename T>
class RingBuffer {
public:
    RingBuffer(size_t size) {
        data_.resize(size);
    }

    void push(const T& value) {
        data_[end_] = value;
        ++end_;
        if (end_ >= data_.size()) {
            end_ = 0;
            ring_filled_ = true;
        }
    }

    std::vector<T> dump() {
        size_t begin = ring_filled_ ? end_ : 0;  // end_ record still intact
        std::vector<T> res;
        if (!ring_filled_) {
            if (end_ - begin == 0)
                return res;
            res.assign(data_.begin() + begin, data_.begin() + end_);
        } else {
            res.reserve(data_.size());
            res.assign(data_.begin() + begin, data_.end());
            std::copy(data_.begin(), data_.begin() + end_, std::back_inserter(res));
        }
        return res;
    }

private:
    std::vector<T> data_;
    bool ring_filled_{false};
    size_t end_{0};
};
