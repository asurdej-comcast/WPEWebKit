#pragma once
#include <chrono>
#include <memory>
#include <utility>

#include "FireboltRuntime.h"
#include "FireboltTypes.h"

namespace firebolt {

namespace detail {
template<typename T>
FireboltResult<T> Decode(const RawResult& raw)
{
    if (!raw.ok)
        return FireboltResult<T>::Failure(raw.error);
    T model;
    if (!model.FromString(raw.payload)) {
        FireboltError e;
        e.code = FireboltErrorCode::InvalidResponse;
        e.message = "response does not match the expected model";
        return FireboltResult<T>::Failure(std::move(e));
    }
    return FireboltResult<T>::Success(std::move(model));
}
} // namespace detail

// Typed facade over a runtime-owned IOperationState. Completes exactly once.
template<typename T>
class FireboltOperation {
public:
    FireboltOperation() = default;
    explicit FireboltOperation(std::shared_ptr<IOperationState> state) : state_(std::move(state)) { }

    bool IsValid() const { return static_cast<bool>(state_); }
    bool IsCompleted() const { return state_ && state_->IsCompleted(); }
    bool Cancel() { return state_ && state_->Cancel(); }

    // One continuation per operation. If already completed it is dispatched
    // immediately (through the client's callback executor).
    void Then(OperationCallback<T> callback)
    {
        if (!state_ || !callback)
            return;
        state_->Then([callback = std::move(callback)](const RawResult& raw) {
            callback(detail::Decode<T>(raw));
        });
    }

    // Blocking; never call on a thread that must stay responsive (WebKit main thread).
    FireboltResult<T> Get(std::chrono::milliseconds timeout)
    {
        if (!state_)
            return FireboltResult<T>();
        return detail::Decode<T>(state_->Wait(timeout));
    }

private:
    std::shared_ptr<IOperationState> state_;
};

} // namespace firebolt
