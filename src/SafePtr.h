#pragma once

#include <memory>
#include <mutex>

// Wrapper to make any object thread-safe
template <typename T, typename M = std::recursive_mutex>
class SafePtr {
private:
    class LockedPtr {
    public:
        LockedPtr(M& mutex, T* const data) : lock_(mutex), data_(data) {}
        LockedPtr(LockedPtr&& other) noexcept : lock_(std::move(other.lock_)), data_(other.data_) {}
        LockedPtr& operator=(LockedPtr&& other) noexcept {
            // Currently never used, but added to satisfy static analyzer
            lock_ = std::move(other.lock_);
            data_ = std::move(other.data_);
            return *this;
        }
        T* operator->() {
            return data_;
        }
        const T* operator->() const {
            return data_;
        }
    private:
        std::unique_lock<M> lock_;
        T* const data_;
    };

public:
    template <typename... Args>
    SafePtr(Args... args) : mutex_(std::make_shared<M>()), data_(std::make_shared<T>(args...)) {}

    LockedPtr operator->() {
        return LockedPtr(*mutex_, data_.get());
    }
    const LockedPtr operator->() const {
        return LockedPtr(*mutex_, data_.get());
    }

private:
    std::shared_ptr<M> mutex_;
    std::shared_ptr<T> data_;
};
