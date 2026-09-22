// Non-template runtime state interfaces ("IOperationState"/"ISubscriptionState"
// in the API Headers page). They are implemented ONLY inside libfirebolt-client
// and are what lets generated headers stay header-only: the library never needs
// to know any generated model type; payloads cross the boundary as JSON text.
#pragma once
#include <chrono>
#include <functional>
#include <string>

#include "FireboltTypes.h"

namespace firebolt {

struct RawResult {
    bool ok = false;
    std::string payload;     // JSON text of the JSON-RPC "result" when ok
    FireboltError error;     // valid when !ok
};

class IOperationState {
public:
    virtual ~IOperationState() = default;
    virtual bool IsCompleted() const = 0;
    virtual bool Cancel() = 0;                                       // true if this call cancelled it
    virtual void Then(std::function<void(const RawResult&)> continuation) = 0;
    virtual RawResult Wait(std::chrono::milliseconds timeout) = 0;   // cancels on timeout
};

class ISubscriptionState {
public:
    virtual ~ISubscriptionState() = default;
    virtual bool IsActive() const = 0;
    virtual bool Unsubscribe() = 0;                                  // idempotent
};

} // namespace firebolt
