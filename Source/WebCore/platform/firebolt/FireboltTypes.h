// Common value types shared by every Firebolt layer.
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace firebolt {

enum class FireboltErrorCode {
    None = 0,
    NotConnected,      // transport not connected when the request was issued / connection lost
    TransportError,    // transport->send() failed
    Timeout,           // FireboltOperation::Get() timed out (the request is cancelled)
    Cancelled,         // FireboltOperation::Cancel()
    ClientShutdown,    // FireboltClient destroyed while the request was pending
    RpcError,          // JSON-RPC "error" object returned by the gateway (see rpcCode)
    InvalidResponse,   // response could not be decoded into the requested model
    InvalidOperation   // default-constructed / moved-from operation or result
};

struct FireboltError {
    FireboltErrorCode code = FireboltErrorCode::None;
    int rpcCode = 0;          // valid when code == RpcError
    std::string message;
};

template<typename T>
class FireboltResult {
public:
    FireboltResult() { error_.code = FireboltErrorCode::InvalidOperation; }

    static FireboltResult Success(T value)
    {
        FireboltResult r;
        r.error_ = FireboltError {};
        r.value_ = std::move(value);
        return r;
    }
    static FireboltResult Failure(FireboltError error)
    {
        FireboltResult r;
        r.error_ = std::move(error);
        return r;
    }

    explicit operator bool() const { return value_.has_value(); }
    const T& Value() const { return *value_; }              // only valid when operator bool()
    const FireboltError& Error() const { return error_; }

private:
    std::optional<T> value_;
    FireboltError error_;
};

template<typename T> using OperationCallback = std::function<void(const FireboltResult<T>&)>;
template<typename T> using EventCallback = std::function<void(const T&)>;

// Hops a task onto the thread the application wants callbacks on.
// WebKit: [](auto task) { RunLoop::main().dispatch(WTFMove(task)); }
using Executor = std::function<void(std::function<void()>)>;

} // namespace firebolt
