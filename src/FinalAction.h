#pragma once

// This was taken from GSL Guidelines
template <class F>
class FinalAction {
public:
    explicit FinalAction(F f) noexcept : f_(std::move(f)), invoke_(true) {}

    FinalAction(FinalAction&& other) noexcept : f_(std::move(other.f_)), invoke_(other.invoke_) {
        other.invoke_ = false;
    }

    FinalAction(const FinalAction&) = delete;
    FinalAction& operator=(const FinalAction&) = delete;
    FinalAction& operator=(FinalAction&&) = delete;

    ~FinalAction() noexcept {
        if (invoke_)
            f_();
    }

private:
    F f_;
    bool invoke_;
};