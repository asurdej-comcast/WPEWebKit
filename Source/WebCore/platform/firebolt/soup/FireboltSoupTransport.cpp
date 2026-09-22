#include "config.h"
#include "FireboltSoupTransport.h"

#if ENABLE(FIREBOLT)

#include <libsoup/soup.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

namespace firebolt {

namespace {
using Task = std::function<void()>;

gboolean RunTask(gpointer data)
{
    (*static_cast<Task*>(data))();
    return G_SOURCE_REMOVE;
}
void DestroyTask(gpointer data) { delete static_cast<Task*>(data); }
} // namespace

struct FireboltSoupTransport::Impl {
    explicit Impl(const FireboltSoupTransportOptions& o) : options(o) { }

    struct ConnectCtx {
        Impl* impl;
        uint64_t generation;
    };

    enum class Emitted { Unknown, Up, Down };

    const FireboltSoupTransportOptions options;

    // ---- shared (guarded by `mutex`) ----
    std::mutex mutex;
    std::function<void(bool, const std::string&)> statusCb;
    std::function<void(const std::string&)> messageCb;
    std::deque<std::string> outbound;
    bool drainScheduled = false;
    bool shuttingDown = false;
    std::string url;
    std::map<std::string, std::string> context;

    std::atomic<bool> connected { false };   // true only while the websocket is OPEN
    std::atomic<bool> wanted { false };      // between open() and disconnect()

    // ---- IO-thread only ----
    GMainContext* gctx = nullptr;
    GMainLoop* loop = nullptr;
    std::thread thread;
    SoupSession* session = nullptr;
    SoupWebsocketConnection* conn = nullptr;
    GCancellable* cancellable = nullptr;
    GSource* connectTimeout = nullptr;
    GSource* reconnectTimer = nullptr;
    uint64_t generation = 0;
    bool connecting = false;
    unsigned backoffMs = 0;
    Emitted emitted = Emitted::Unknown;
    std::string lastError;
    std::mt19937 rng { std::random_device {}() };

    // ------------------------------------------------------------------
    void Start(std::shared_ptr<Impl> self)
    {
        gctx = g_main_context_new();
        loop = g_main_loop_new(gctx, FALSE);
        thread = std::thread([self]() { self->Run(); });   // thread co-owns Impl
    }

    void Run()
    {
        g_main_context_push_thread_default(gctx);
        session = soup_session_new();          // bound to this thread's context
        g_main_loop_run(loop);
        // loop quit: everything was torn down by Teardown() already
        g_clear_object(&session);
        g_main_context_pop_thread_default(gctx);
        g_main_loop_unref(loop);
        g_main_context_unref(gctx);
    }

    void RunInIo(Task task)
    {
        g_main_context_invoke_full(gctx, G_PRIORITY_DEFAULT, RunTask, new Task(std::move(task)), DestroyTask);
    }

    // ---- callbacks to the client (always on the IO thread, never under a lock) ----
    void EmitStatus(bool up, const std::string& info)
    {
        Emitted next = up ? Emitted::Up : Emitted::Down;
        if (emitted == next)
            return;                            // only report transitions
        emitted = next;
        std::function<void(bool, const std::string&)> cb;
        {
            std::lock_guard<std::mutex> lock(mutex);
            cb = statusCb;
        }
        if (cb)
            cb(up, info);
    }

    void EmitMessage(const std::string& m)
    {
        std::function<void(const std::string&)> cb;
        {
            std::lock_guard<std::mutex> lock(mutex);
            cb = messageCb;
        }
        if (cb)
            cb(m);
    }

    // ---- IO-thread operations ------------------------------------------
    static void ClearSource(GSource*& s)
    {
        if (s) {
            g_source_destroy(s);
            g_source_unref(s);
            s = nullptr;
        }
    }

    GSource* AddTimeout(unsigned ms, GSourceFunc fn)
    {
        GSource* s = g_timeout_source_new(ms);
        g_source_set_callback(s, fn, this, nullptr);
        g_source_attach(s, gctx);
        return s;
    }

    void StartConnect()
    {
        if (!wanted || connecting || conn)
            return;
        std::string u;
        std::map<std::string, std::string> ctx;
        {
            std::lock_guard<std::mutex> lock(mutex);
            u = url;
            ctx = context;
        }
        SoupMessage* msg = soup_message_new("GET", u.c_str());
        if (!msg) {
            lastError = "invalid url";
            EmitStatus(false, lastError);
            wanted = false;
            return;
        }
        SoupMessageHeaders* headers = soup_message_get_request_headers(msg);
        std::string origin;
        std::vector<std::string> protocolStore;
        for (auto& kv : ctx) {
            if (kv.first == "origin")
                origin = kv.second;
            else if (kv.first == "subprotocols") {
                std::stringstream ss(kv.second);
                std::string item;
                while (std::getline(ss, item, ','))
                    if (!item.empty())
                        protocolStore.push_back(item);
            } else
                soup_message_headers_replace(headers, kv.first.c_str(), kv.second.c_str());
        }
        std::vector<char*> protocols;
        for (auto& p : protocolStore)
            protocols.push_back(const_cast<char*>(p.c_str()));
        protocols.push_back(nullptr);

        connecting = true;
        ++generation;
        cancellable = g_cancellable_new();
        connectTimeout = AddTimeout(options.connectTimeoutMs, [](gpointer d) -> gboolean {
            auto* self = static_cast<Impl*>(d);
            if (self->cancellable)
                g_cancellable_cancel(self->cancellable);   // completion callback reports the failure
            if (self->connectTimeout) {
                g_source_unref(self->connectTimeout);
                self->connectTimeout = nullptr;
            }
            self->lastError = "connect timeout";
            return G_SOURCE_REMOVE;
        });

        soup_session_websocket_connect_async(session, msg,
            origin.empty() ? nullptr : origin.c_str(),
            protocolStore.empty() ? nullptr : protocols.data(),
            G_PRIORITY_DEFAULT, cancellable, &Impl::OnConnected, new ConnectCtx { this, generation });
        g_object_unref(msg);
    }

    static void OnConnected(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<ConnectCtx> ctx(static_cast<ConnectCtx*>(data));
        Impl* self = ctx->impl;
        GError* error = nullptr;
        SoupWebsocketConnection* c = soup_session_websocket_connect_finish(SOUP_SESSION(source), result, &error);

        if (ctx->generation != self->generation) {      // stale attempt (disconnect/teardown raced)
            if (c) { soup_websocket_connection_close(c, SOUP_WEBSOCKET_CLOSE_NORMAL, nullptr); g_object_unref(c); }
            g_clear_error(&error);
            return;
        }
        self->connecting = false;
        ClearSource(self->connectTimeout);
        g_clear_object(&self->cancellable);

        if (!c) {
            std::string why = self->lastError == "connect timeout" ? self->lastError
                : (error ? error->message : "connect failed");
            g_clear_error(&error);
            self->lastError.clear();
            self->OnLost(why);
            return;
        }
        if (!self->wanted) {                           // disconnect() arrived during the handshake
            soup_websocket_connection_close(c, SOUP_WEBSOCKET_CLOSE_NORMAL, nullptr);
            g_object_unref(c);
            return;
        }

        self->conn = c;
        soup_websocket_connection_set_max_incoming_payload_size(c, self->options.maxIncomingMessageBytes);
        soup_websocket_connection_set_keepalive_interval(c, self->options.keepaliveIntervalSeconds);
        g_signal_connect(c, "message", G_CALLBACK(&Impl::OnMessage), self);
        g_signal_connect(c, "closed", G_CALLBACK(&Impl::OnClosed), self);
        g_signal_connect(c, "error", G_CALLBACK(&Impl::OnError), self);

        self->backoffMs = 0;
        self->connected = true;                        // before the callback: client sends from it
        self->EmitStatus(true, "");
    }

    static void OnMessage(SoupWebsocketConnection*, gint type, GBytes* bytes, gpointer data)
    {
        if (type != SOUP_WEBSOCKET_DATA_TEXT)
            return;                                    // Firebolt JSON-RPC is text only
        gsize size = 0;
        const char* p = static_cast<const char*>(g_bytes_get_data(bytes, &size));
        static_cast<Impl*>(data)->EmitMessage(std::string(p ? p : "", size));
    }

    static void OnError(SoupWebsocketConnection*, GError* error, gpointer data)
    {
        static_cast<Impl*>(data)->lastError = error ? error->message : "websocket error";
    }

    static void OnClosed(SoupWebsocketConnection* c, gpointer data)
    {
        auto* self = static_cast<Impl*>(data);
        std::string why = self->lastError;
        if (why.empty()) {
            const char* reason = soup_websocket_connection_get_close_data(c);
            why = "closed (" + std::to_string(soup_websocket_connection_get_close_code(c)) + ")"
                + (reason && *reason ? std::string(": ") + reason : "");
        }
        self->lastError.clear();
        self->DropConnection();
        self->OnLost(why);
    }

    void DropConnection()
    {
        connected = false;
        if (conn) {
            g_signal_handlers_disconnect_by_data(conn, this);
            g_object_unref(conn);
            conn = nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex);
        outbound.clear();                              // unsent data is meaningless after a drop
    }

    // Connection attempt failed or an established connection ended.
    void OnLost(const std::string& why)
    {
        EmitStatus(false, why);
        if (!wanted || !options.autoReconnect || ShuttingDown())
            return;
        backoffMs = backoffMs == 0 ? options.reconnectMinDelayMs
                                   : std::min(backoffMs * 2, options.reconnectMaxDelayMs);
        unsigned jitter = std::uniform_int_distribution<unsigned>(0, backoffMs / 4)(rng);
        ClearSource(reconnectTimer);
        reconnectTimer = AddTimeout(backoffMs + jitter, [](gpointer d) -> gboolean {
            auto* self = static_cast<Impl*>(d);
            g_source_unref(self->reconnectTimer);
            self->reconnectTimer = nullptr;
            self->StartConnect();
            return G_SOURCE_REMOVE;
        });
    }

    bool ShuttingDown()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return shuttingDown;
    }

    void DoDisconnect()
    {
        wanted = false;
        ++generation;                                   // invalidates an in-flight attempt
        ClearSource(reconnectTimer);
        ClearSource(connectTimeout);
        if (cancellable) {
            g_cancellable_cancel(cancellable);
            g_clear_object(&cancellable);
        }
        connecting = false;
        if (conn) {
            SoupWebsocketConnection* c = conn;
            g_signal_handlers_disconnect_by_data(c, this);
            conn = nullptr;
            connected = false;
            soup_websocket_connection_close(c, SOUP_WEBSOCKET_CLOSE_NORMAL, nullptr);
            g_object_unref(c);
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            outbound.clear();
        }
        if (emitted == Emitted::Up)
            EmitStatus(false, "disconnected");
    }

    void Drain()
    {
        for (;;) {
            std::string m;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (outbound.empty()) {
                    drainScheduled = false;
                    return;
                }
                m = std::move(outbound.front());
                outbound.pop_front();
            }
            if (conn && soup_websocket_connection_get_state(conn) == SOUP_WEBSOCKET_STATE_OPEN)
                soup_websocket_connection_send_text(conn, m.c_str());
        }
    }

    void Teardown()
    {
        DoDisconnect();
        g_main_loop_quit(loop);
    }

    // ---- public-API implementations (any thread) --------------------------
    bool Open(const std::string& u, const std::map<std::string, std::string>& ctx)
    {
        GError* error = nullptr;
        GUri* parsed = g_uri_parse(u.c_str(), G_URI_FLAGS_NONE, &error);
        if (!parsed) {
            g_clear_error(&error);
            return false;
        }
        const char* scheme = g_uri_get_scheme(parsed);
        bool ok = scheme && (!g_ascii_strcasecmp(scheme, "ws") || !g_ascii_strcasecmp(scheme, "wss")
                          || !g_ascii_strcasecmp(scheme, "http") || !g_ascii_strcasecmp(scheme, "https"));
        g_uri_unref(parsed);
        if (!ok)
            return false;

        bool expected = false;
        if (!wanted.compare_exchange_strong(expected, true))
            return false;                               // already connecting/connected
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown) {
                wanted = false;
                return false;
            }
            url = u;
            context = ctx;
        }
        RunInIo([this]() { StartConnect(); });
        return true;
    }

    bool Send(const std::string& message)
    {
        if (!connected)
            return false;
        bool schedule = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown || outbound.size() >= options.maxQueuedMessages)
                return false;
            outbound.push_back(message);
            schedule = !drainScheduled;
            drainScheduled = true;
        }
        if (schedule)
            RunInIo([this]() { Drain(); });
        return true;
    }

    void Shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            shuttingDown = true;
            statusCb = nullptr;                         // no callbacks after destruction begins
            messageCb = nullptr;
        }
        RunInIo([this]() { Teardown(); });
    }
};

// ---------------------------------------------------------------------------
FireboltSoupTransport::FireboltSoupTransport(FireboltSoupTransportOptions options)
    : impl_(std::make_shared<Impl>(options))
{
    impl_->Start(impl_);
}

FireboltSoupTransport::~FireboltSoupTransport()
{
    impl_->Shutdown();
    if (impl_->thread.get_id() == std::this_thread::get_id())
        impl_->thread.detach();     // destroyed from a callback on the IO thread: cannot self-join;
                                    // the thread keeps Impl alive until its loop exits
    else
        impl_->thread.join();
}

bool FireboltSoupTransport::open(const std::string& url, const std::map<std::string, std::string>& context)
{
    return impl_->Open(url, context);
}
bool FireboltSoupTransport::isConnected() { return impl_->connected.load(); }
bool FireboltSoupTransport::send(const std::string& message) { return impl_->Send(message); }
void FireboltSoupTransport::disconnect() { impl_->RunInIo([impl = impl_.get()]() { impl->DoDisconnect(); }); }

void FireboltSoupTransport::onConnectionStatusChanged(std::function<void(bool, const std::string&)> callback)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->statusCb = std::move(callback);
}
void FireboltSoupTransport::onMessageReceived(std::function<void(const std::string&)> callback)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->messageCb = std::move(callback);
}

} // namespace firebolt

#endif // ENABLE(FIREBOLT)
