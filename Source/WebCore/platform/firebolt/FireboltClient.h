// FireboltClient: explicit runtime context binding generated APIs/models to a
// transport. No singleton; any number of clients may coexist.
// Builds with -fno-exceptions -fno-rtti (WebKit build flags).
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "FireboltObject.h"
#include "FireboltOperation.h"
#include "FireboltRuntime.h"
#include "FireboltSubscription.h"
#include "FireboltTypes.h"
#include "IFireboltTransport.h"

namespace firebolt {

struct FireboltClientOptions {
    // Where Then()/event/connection callbacks run. Empty = on the transport thread.
    Executor callbackExecutor;
    // Optional: connection state changes (delivered through callbackExecutor).
    std::function<void(bool connected, const std::string& errorInfo)> onConnectionChanged;
};

class FireboltClient {
public:
    explicit FireboltClient(std::shared_ptr<IFireboltTransport> transport,
                            FireboltClientOptions options = FireboltClientOptions());
    ~FireboltClient();

    FireboltClient(const FireboltClient&) = delete;
    FireboltClient& operator=(const FireboltClient&) = delete;

    // Convenience pass-throughs; the application may also drive the transport itself.
    bool Connect(const std::string& url, const std::map<std::string, std::string>& context = {});
    void Disconnect();
    bool IsConnected() const;

    // ---- API used by generated interface headers ----------------------
    template<typename T>
    FireboltOperation<T> Send(const std::string& method, const FireboltObject& params)
    {
        return FireboltOperation<T>(SendRaw(method, params.ToString()));
    }

    template<typename T>
    FireboltSubscription Subscribe(const std::string& event, EventCallback<T> listener)
    {
        if (!listener)
            return FireboltSubscription();
        return FireboltSubscription(SubscribeRaw(event,
            [listener = std::move(listener)](const std::string& payload) {
                T model;
                if (model.FromString(payload))
                    listener(model);
            }));
    }

    // Cached per-client interface instance (RTTI-free key).
    template<typename Interface>
    Interface& GetInterface()
    {
        static const char key = 0;
        return *static_cast<Interface*>(GetOrCreateInterface(&key,
            [this]() { return std::shared_ptr<void>(std::make_shared<Interface>(*this)); }));
    }

    // ---- non-template runtime (implemented in libfirebolt-client) -------
    std::shared_ptr<IOperationState> SendRaw(const std::string& method, const std::string& paramsJson);
    std::shared_ptr<ISubscriptionState> SubscribeRaw(const std::string& event,
                                                     std::function<void(const std::string& paramsJson)> listener);

private:
    void* GetOrCreateInterface(const void* key, const std::function<std::shared_ptr<void>()>& factory);

    struct Impl;
    std::shared_ptr<IFireboltTransport> transport_;   // declared before impl_: released after it
    std::shared_ptr<Impl> impl_;
};

} // namespace firebolt
