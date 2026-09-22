// libsoup-3 based IFireboltTransport (SoupWebsocketConnection).
//
// Threading model
//   * The transport owns ONE dedicated IO thread running a private GMainContext/GMainLoop.
//     All libsoup objects live on that thread and are touched only from it.
//   * send()/open()/disconnect()/isConnected() are thread-safe and never block on the network.
//   * Messages and connection-status callbacks are delivered on the IO thread, never from
//     inside send(). This is what lets other threads block in FireboltOperation::Get().
//   * Callbacks must not block waiting on a response from this same transport.
//
// The public header does not include glib/libsoup (pimpl).
#pragma once
#include <memory>

#include "IFireboltTransport.h"

namespace firebolt {

struct FireboltSoupTransportOptions {
    unsigned connectTimeoutMs = 10000;       // per connection attempt
    bool autoReconnect = true;               // reconnect after unexpected loss / failed attempt
    unsigned reconnectMinDelayMs = 500;      // exponential backoff: min .. max, with jitter
    unsigned reconnectMaxDelayMs = 10000;
    unsigned keepaliveIntervalSeconds = 30;  // WebSocket ping interval (0 = off)
    unsigned maxIncomingMessageBytes = 1u << 20;
    unsigned maxQueuedMessages = 256;        // send() returns false beyond this
};

// open() context keys with special meaning (everything else becomes an HTTP request header):
//   "origin"        -> Origin header used for the handshake
//   "subprotocols"  -> comma separated Sec-WebSocket-Protocol list
class FireboltSoupTransport final : public IFireboltTransport {
public:
    explicit FireboltSoupTransport(FireboltSoupTransportOptions options = FireboltSoupTransportOptions());
    ~FireboltSoupTransport() override;

    FireboltSoupTransport(const FireboltSoupTransport&) = delete;
    FireboltSoupTransport& operator=(const FireboltSoupTransport&) = delete;

    // Asynchronous: returns true if the attempt was started (false: bad URL / already open).
    // Success or failure is reported through onConnectionStatusChanged.
    bool open(const std::string& url, const std::map<std::string, std::string>& context) override;
    bool isConnected() override;
    void onConnectionStatusChanged(
        std::function<void(bool connected, const std::string& errorInfo)> callback) override;
    bool send(const std::string& message) override;
    void onMessageReceived(std::function<void(const std::string& message)> callback) override;
    void disconnect() override;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;   // also owned by the IO thread, so teardown is safe from any thread
};

} // namespace firebolt
