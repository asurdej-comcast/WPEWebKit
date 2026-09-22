#pragma once
#include <memory>
#include <utility>

#include "FireboltRuntime.h"

namespace firebolt {

// Move-only lifetime handle for an event registration. Destroying it unsubscribes.
class FireboltSubscription {
public:
    FireboltSubscription() = default;
    explicit FireboltSubscription(std::shared_ptr<ISubscriptionState> state) : state_(std::move(state)) { }

    FireboltSubscription(FireboltSubscription&&) noexcept = default;
    FireboltSubscription& operator=(FireboltSubscription&& other) noexcept
    {
        if (this != &other) {
            Unsubscribe();
            state_ = std::move(other.state_);
        }
        return *this;
    }
    FireboltSubscription(const FireboltSubscription&) = delete;
    FireboltSubscription& operator=(const FireboltSubscription&) = delete;
    ~FireboltSubscription() { Unsubscribe(); }

    bool IsValid() const { return static_cast<bool>(state_); }
    bool IsActive() const { return state_ && state_->IsActive(); }
    bool Unsubscribe() { return state_ && state_->Unsubscribe(); }

private:
    std::shared_ptr<ISubscriptionState> state_;
};

} // namespace firebolt
