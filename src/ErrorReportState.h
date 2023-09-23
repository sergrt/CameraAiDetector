#pragma once

class ErrorState final {
public:
    void Activate() {
        is_active_ = true;
    }

    void Reset() {
        is_active_ = false;
    }

    bool IsActive() const {
        return is_active_;
    }

private:
    bool is_active_ = false;
};
