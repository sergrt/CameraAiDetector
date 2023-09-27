#pragma once

#include <string>

class ErrorState final {
public:
    void Activate(std::string deactivate_message) {
        is_active_ = true;
        deactivate_message_ = std::move(deactivate_message);
    }

    std::string Reset() {
        is_active_ = false;
        return deactivate_message_;
    }

    bool IsActive() const {
        return is_active_;
    }

private:
    bool is_active_ = false;
    std::string deactivate_message_;
};
