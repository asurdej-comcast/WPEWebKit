// Process-wide access point to Firebolt. Owns the libsoup transport and the
// FireboltClient, and exposes the generated Device and Metrics interfaces.
//
// If FIREBOLT_ENDPOINT is unset/empty the client is not connected; the
// interfaces remain valid but their operations cannot reach a gateway.
// Intentionally never destroyed (avoids joining the IO thread during exit).
#pragma once

#include "config.h"

#if ENABLE(FIREBOLT)

#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <wtf/RunLoop.h>

#include "FireboltClient.h"
#include "FireboltDevice.h"
#include "FireboltMetrics.h"
#include "FireboltSoupTransport.h"

namespace firebolt {
namespace webkit {

class FireboltManager {
public:
    static FireboltManager& singleton()
    {
        static FireboltManager* instance = new FireboltManager();
        return *instance;
    }

    FireboltManager(const FireboltManager&) = delete;
    FireboltManager& operator=(const FireboltManager&) = delete;

    device::IDevice& device() { return m_device; }
    metrics::IMetrics& metrics() { return m_metrics; }

    FireboltClient& client() { return *m_client; }
    bool isConnected() const { return m_client->IsConnected(); }

private:
    static FireboltClientOptions clientOptions()
    {
        FireboltClientOptions options;
        options.callbackExecutor = [](std::function<void()> task) {
            RunLoop::main().dispatch([task = WTFMove(task)] { task(); });
        };
        return options;
    }

    FireboltManager()
        : m_transport(std::make_shared<FireboltSoupTransport>())
        , m_client(std::make_unique<FireboltClient>(m_transport, clientOptions()))
        , m_device(*m_client)
        , m_metrics(*m_client)
    {
        const char* endpoint = getenv("FIREBOLT_ENDPOINT");
        if (endpoint && *endpoint)
            m_client->Connect(endpoint);
    }

    std::shared_ptr<IFireboltTransport> m_transport;
    std::unique_ptr<FireboltClient> m_client;
    device::Device m_device;
    metrics::Metrics m_metrics;
};

} // namespace webkit
} // namespace firebolt

#endif // ENABLE(FIREBOLT)
