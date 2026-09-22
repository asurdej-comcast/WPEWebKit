/*
 * Copyright (C) 2026 Comcast
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "FireboltClient.h"

#if ENABLE(FIREBOLT)

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "OperationState.h"

namespace firebolt {

struct FireboltClient::Impl : std::enable_shared_from_this<Impl> {
    struct Listener {
        uint64_t id;
        std::function<void(const std::string&)> fn;
    };

    class SubscriptionStateImpl final : public ISubscriptionState {
    public:
        SubscriptionStateImpl(std::weak_ptr<Impl> impl, std::string event, uint64_t id)
            : impl_(std::move(impl)), event_(std::move(event)), id_(id) { }

        bool IsActive() const override { return active_.load(); }
        bool Unsubscribe() override
        {
            if (!active_.exchange(false))
                return false;
            if (auto impl = impl_.lock())
                impl->RemoveListener(event_, id_);
            return true;
        }

    private:
        std::weak_ptr<Impl> impl_;
        std::string event_;
        uint64_t id_;
        std::atomic<bool> active_ { true };
    };

    // Borrowed. FireboltClient owns the shared_ptr, so the transport is always
    // released on the thread destroying the client - never on a transport callback
    // thread that merely held a temporary Impl reference (self-join hazard).
    IFireboltTransport* transport = nullptr;
    std::recursive_mutex transportMutex;   // guards `transport` for send/isConnected/Shutdown
    FireboltClientOptions options;

    std::mutex mutex;   // guards everything below; never held while calling out
    uint64_t nextRequestId = 1;
    uint64_t nextListenerId = 1;
    std::unordered_map<uint64_t, std::shared_ptr<OperationState>> pending;
    std::unordered_map<std::string, std::vector<Listener>> listeners;

    std::recursive_mutex interfaceMutex;
    std::unordered_map<const void*, std::shared_ptr<void>> interfaces;

    // ------------------------------------------------------------------
    void Init()
    {
        if (!transport)
            return;
        std::weak_ptr<Impl> weak = weak_from_this();
        transport->onMessageReceived([weak](const std::string& m) {
            if (auto self = weak.lock())
                self->OnMessage(m);
        });
        transport->onConnectionStatusChanged([weak](bool connected, const std::string& info) {
            if (auto self = weak.lock())
                self->OnConnection(connected, info);
        });
    }

    bool TransportConnected()
    {
        std::lock_guard<std::recursive_mutex> lock(transportMutex);
        return transport && transport->isConnected();
    }
    bool TransportSend(const std::string& message)
    {
        std::lock_guard<std::recursive_mutex> lock(transportMutex);
        return transport && transport->send(message);
    }

    void Shutdown()
    {
        {   // stop inbound traffic and sever access; waits out in-flight send()/isConnected()
            std::lock_guard<std::recursive_mutex> lock(transportMutex);
            if (transport) {
                transport->onMessageReceived(nullptr);
                transport->onConnectionStatusChanged(nullptr);
                transport = nullptr;
            }
        }
        FailAllPending(FireboltErrorCode::ClientShutdown, "client destroyed");
        std::lock_guard<std::mutex> lock(mutex);
        listeners.clear();
    }

    void Dispatch(std::function<void()> task)
    {
        if (options.callbackExecutor)
            options.callbackExecutor(std::move(task));
        else
            task();
    }

    static std::string BuildRequest(uint64_t id, const std::string& method, const std::string& params)
    {
        FireboltObject request, methodName, paramsObject;
        FireboltObject jsonrpc;
        jsonrpc.SetString("2.0");
        request.SetChild("jsonrpc", jsonrpc);
        FireboltObject idObject;
        idObject.SetNumber(static_cast<double>(id));
        request.SetChild("id", idObject);
        methodName.SetString(method);
        request.SetChild("method", methodName);
        if (params.empty() || !paramsObject.FromString(params))
            paramsObject = FireboltObject();
        request.SetChild("params", paramsObject);
        return request.ToString();
    }

    std::shared_ptr<IOperationState> SendRaw(const std::string& method, const std::string& params)
    {
        auto state = std::make_shared<OperationState>(options.callbackExecutor);
        if (!TransportConnected()) {
            state->Complete(OperationState::Failure(FireboltErrorCode::NotConnected, "transport not connected"));
            return state;
        }
        uint64_t id;
        {
            std::lock_guard<std::mutex> lock(mutex);
            id = nextRequestId++;
        }
        std::weak_ptr<Impl> weak = weak_from_this();
        state->SetCancelHook([weak, id]() {
            if (auto self = weak.lock()) {
                std::lock_guard<std::mutex> lock(self->mutex);
                self->pending.erase(id);
            }
        });
        {
            std::lock_guard<std::mutex> lock(mutex);
            pending.emplace(id, state);
        }
        if (!TransportSend(BuildRequest(id, method, params))) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                pending.erase(id);
            }
            state->Complete(OperationState::Failure(FireboltErrorCode::TransportError, "send failed"));
        }
        return state;
    }

    // Firebolt event registration: <event>({"listen": true|false}). Result is ignored.
    void SendListen(const std::string& event, bool listen)
    {
        SendRaw(event, listen ? "{\"listen\":true}" : "{\"listen\":false}");
    }

    void FailAllPending(FireboltErrorCode code, const std::string& message)
    {
        std::unordered_map<uint64_t, std::shared_ptr<OperationState>> drained;
        {
            std::lock_guard<std::mutex> lock(mutex);
            drained.swap(pending);
        }
        for (auto& entry : drained)
            entry.second->Complete(OperationState::Failure(code, message));
    }

    // ------------------------------------------------------------------
    void OnConnection(bool connected, const std::string& info)
    {
        if (connected) {
            std::vector<std::string> events;
            {
                std::lock_guard<std::mutex> lock(mutex);
                for (auto& entry : listeners)
                    events.push_back(entry.first);
            }
            for (auto& e : events)          // (re)establish gateway-side registrations
                SendListen(e, true);
        } else
            FailAllPending(FireboltErrorCode::NotConnected, info.empty() ? "connection lost" : info);

        if (options.onConnectionChanged) {
            auto cb = options.onConnectionChanged;
            Dispatch([cb, connected, info]() { cb(connected, info); });
        }
    }

    void OnMessage(const std::string& message)
    {
        FireboltObject j;
        if (!j.FromString(message) || j.GetKind() != FireboltObject::Kind::Object)
            return;                                         // malformed: drop
        FireboltObject id, method;
        bool hasId = j.GetChild("id", id);
        bool hasMethod = j.GetChild("method", method);
        double idNumber = 0;
        std::string methodName;

        if (hasId && !hasMethod && id.GetNumber(idNumber)) {
            HandleResponse(static_cast<uint64_t>(idNumber), j);
        } else if (hasMethod && !hasId && method.GetString(methodName)) {
            FireboltObject params;
            HandleNotification(methodName, j.GetChild("params", params) ? params.ToString() : "null");
        }
        // anything else (server->client requests) is not supported yet
    }

    void HandleResponse(uint64_t id, const FireboltObject& j)
    {
        std::shared_ptr<OperationState> state;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = pending.find(id);
            if (it == pending.end())
                return;                                     // cancelled / unknown
            state = std::move(it->second);
            pending.erase(it);
        }
        FireboltObject error;
        if (j.GetChild("error", error) && error.GetKind() == FireboltObject::Kind::Object) {
            FireboltObject code, msg;
            double codeNumber = 0;
            std::string message = "rpc error";
            if (error.GetChild("code", code))
                code.GetNumber(codeNumber);
            if (error.GetChild("message", msg))
                msg.GetString(message);
            state->Complete(OperationState::Failure(FireboltErrorCode::RpcError, message, static_cast<int>(codeNumber)));
            return;
        }
        FireboltObject result;
        if (!j.GetChild("result", result)) {
            state->Complete(OperationState::Failure(FireboltErrorCode::InvalidResponse, "no result"));
            return;
        }
        RawResult r;
        r.ok = true;
        r.payload = result.ToString();
        state->Complete(std::move(r));
    }

    void HandleNotification(const std::string& event, const std::string& payload)
    {
        std::vector<Listener> targets;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = listeners.find(event);
            if (it == listeners.end())
                return;
            targets = it->second;
        }
        for (auto& l : targets) {
            auto fn = l.fn;
            Dispatch([fn, payload]() { fn(payload); });
        }
    }

    // ------------------------------------------------------------------
    std::shared_ptr<ISubscriptionState> Subscribe(const std::string& event,
                                                  std::function<void(const std::string&)> fn);

    void RemoveListener(const std::string& event, uint64_t id)
    {
        bool last = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = listeners.find(event);
            if (it == listeners.end())
                return;
            auto& v = it->second;
            for (auto i = v.begin(); i != v.end(); ++i) {
                if (i->id == id) { v.erase(i); break; }
            }
            if (v.empty()) {
                listeners.erase(it);
                last = true;
            }
        }
        if (last && TransportConnected())
            SendListen(event, false);
    }
};

std::shared_ptr<ISubscriptionState> FireboltClient::Impl::Subscribe(const std::string& event,
    std::function<void(const std::string&)> fn)
{
    uint64_t id;
    bool first;
    {
        std::lock_guard<std::mutex> lock(mutex);
        id = nextListenerId++;
        auto& v = listeners[event];
        first = v.empty();
        v.push_back({ id, std::move(fn) });
    }
    // If not connected yet, OnConnection(true) registers it with the gateway.
    if (first && TransportConnected())
        SendListen(event, true);
    return std::make_shared<Impl::SubscriptionStateImpl>(weak_from_this(), event, id);
}

// ----------------------------------------------------------------------
FireboltClient::FireboltClient(std::shared_ptr<IFireboltTransport> transport, FireboltClientOptions options)
    : transport_(std::move(transport))
    , impl_(std::make_shared<Impl>())
{
    impl_->transport = transport_.get();
    impl_->options = std::move(options);
    impl_->Init();
}

FireboltClient::~FireboltClient() { impl_->Shutdown(); }

bool FireboltClient::Connect(const std::string& url, const std::map<std::string, std::string>& context)
{
    return transport_ && transport_->open(url, context);
}
void FireboltClient::Disconnect()
{
    if (transport_)
        transport_->disconnect();
}
bool FireboltClient::IsConnected() const { return impl_->TransportConnected(); }

std::shared_ptr<IOperationState> FireboltClient::SendRaw(const std::string& method, const std::string& paramsJson)
{
    return impl_->SendRaw(method, paramsJson);
}

std::shared_ptr<ISubscriptionState> FireboltClient::SubscribeRaw(const std::string& event,
    std::function<void(const std::string&)> listener)
{
    return impl_->Subscribe(event, std::move(listener));
}

void* FireboltClient::GetOrCreateInterface(const void* key, const std::function<std::shared_ptr<void>()>& factory)
{
    std::lock_guard<std::recursive_mutex> lock(impl_->interfaceMutex);
    auto it = impl_->interfaces.find(key);
    if (it == impl_->interfaces.end())
        it = impl_->interfaces.emplace(key, factory()).first;
    return it->second.get();
}

} // namespace firebolt

#endif // ENABLE(FIREBOLT)
