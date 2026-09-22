// Transport contract exactly as specified by "Firebolt CPP 2.0 Transport".
// The transport only moves opaque strings. It knows nothing about JSON-RPC
// correlation, models or typed callbacks - that is FireboltClient's job.
#pragma once
#include <functional>
#include <map>
#include <string>

namespace firebolt {

class IFireboltTransport {
public:
    virtual ~IFireboltTransport() = default;

    virtual bool open(const std::string& url,
                      const std::map<std::string, std::string>& context) = 0;

    virtual bool isConnected() = 0;

    virtual void onConnectionStatusChanged(
        std::function<void(bool connected, const std::string& errorInfo)> callback) = 0;

    virtual bool send(const std::string& message) = 0;

    virtual void onMessageReceived(
        std::function<void(const std::string& message)> callback) = 0;

    virtual void disconnect() = 0;
};

} // namespace firebolt
