// Runtime-side implementation of IOperationState (private to libfirebolt-client).
#pragma once
#include <condition_variable>
#include <mutex>

#include "FireboltRuntime.h"

namespace firebolt {

class OperationState final : public IOperationState {
public:
    explicit OperationState(Executor executor) : executor_(std::move(executor)) { }

    // Called by FireboltClient to clean its correlation map on cancel/timeout.
    void SetCancelHook(std::function<void()> hook)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelHook_ = std::move(hook);
    }

    // First completion wins; returns false if already completed.
    bool Complete(RawResult result)
    {
        std::function<void(const RawResult&)> continuation;
        RawResult copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (completed_)
                return false;
            completed_ = true;
            result_ = std::move(result);
            continuation = std::move(continuation_);
            continuation_ = nullptr;
            cancelHook_ = nullptr;
            if (continuation)
                copy = result_;
        }
        cv_.notify_all();
        if (continuation)
            Dispatch(std::move(continuation), std::move(copy));
        return true;
    }

    static RawResult Failure(FireboltErrorCode code, std::string message, int rpcCode = 0)
    {
        RawResult r;
        r.ok = false;
        r.error.code = code;
        r.error.rpcCode = rpcCode;
        r.error.message = std::move(message);
        return r;
    }

    bool IsCompleted() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return completed_;
    }

    bool Cancel() override { return Abort(FireboltErrorCode::Cancelled, "cancelled"); }

    void Then(std::function<void(const RawResult&)> continuation) override
    {
        RawResult copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!completed_) {
                continuation_ = std::move(continuation);
                return;
            }
            copy = result_;
        }
        Dispatch(std::move(continuation), std::move(copy));
    }

    RawResult Wait(std::chrono::milliseconds timeout) override
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (cv_.wait_for(lock, timeout, [this] { return completed_; }))
                return result_;
        }
        Abort(FireboltErrorCode::Timeout, "timed out");   // may lose a race to a real result: fine
        std::lock_guard<std::mutex> lock(mutex_);
        return result_;
    }

private:
    bool Abort(FireboltErrorCode code, const char* message)
    {
        std::function<void()> hook;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (completed_)
                return false;
            hook = cancelHook_;
        }
        if (!Complete(Failure(code, message)))
            return false;
        if (hook)
            hook();     // never invoked under our lock
        return true;
    }

    void Dispatch(std::function<void(const RawResult&)> continuation, RawResult result)
    {
        if (executor_)
            executor_([continuation = std::move(continuation), result = std::move(result)]() { continuation(result); });
        else
            continuation(result);
    }

    Executor executor_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool completed_ = false;
    RawResult result_;
    std::function<void(const RawResult&)> continuation_;
    std::function<void()> cancelHook_;
};

} // namespace firebolt
