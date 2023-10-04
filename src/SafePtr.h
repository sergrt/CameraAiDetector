#pragma once

#include <memory>
#include <mutex>

// Wrapper to make any object thread-safe
template <typename T, typename M = std::recursive_mutex>
class SafePtr {
private:
    class LockedPtr {
    public:
        LockedPtr(T* const data, M& mutex) : lock_(mutex), data_(data) {}
        LockedPtr(LockedPtr&& other) noexcept : lock_(std::move(other.lock_)), data_(other.data_) {}
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
        return LockedPtr(data_.get(), *mutex_);
    }
    const LockedPtr operator->() const {
        return LockedPtr(data_.get(), *mutex_);
    }

private:
    std::shared_ptr<M> mutex_;
    std::shared_ptr<T> data_;
};
