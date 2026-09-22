// "Generated" header for the Firebolt Metrics module, playback-state methods only.
// Header-only; depends only on public framework types.
//
// Wire format (Firebolt Core SDK, Metrics module): every method takes an object with
// "entityId" plus method specific members, and returns a boolean/null result that
// callers ignore. NOTE: method names/params must be verified against
// https://rdkcentral.github.io/firebolt/apis/next/core/Metrics/ and the target gateway.
#pragma once
#include <string>
#include <utility>

#include "FireboltClient.h"
#include "FireboltModel.h"
#include "FireboltObject.h"
#include "FireboltOperation.h"
#include "FireboltTypes.h"

namespace firebolt {
namespace metrics {

class IMetrics {
public:
    virtual ~IMetrics() = default;

    virtual FireboltOperation<FireboltVoid> MediaLoadStart(const std::string& entityId) = 0;
    virtual FireboltOperation<FireboltVoid> MediaPlay(const std::string& entityId) = 0;
    virtual FireboltOperation<FireboltVoid> MediaPlaying(const std::string& entityId) = 0;
    virtual FireboltOperation<FireboltVoid> MediaPause(const std::string& entityId) = 0;
    virtual FireboltOperation<FireboltVoid> MediaWaiting(const std::string& entityId) = 0;
    virtual FireboltOperation<FireboltVoid> MediaProgress(const std::string& entityId, double progress) = 0;
    virtual FireboltOperation<FireboltVoid> MediaSeeking(const std::string& entityId, double target) = 0;
    virtual FireboltOperation<FireboltVoid> MediaSeeked(const std::string& entityId, double position) = 0;
    virtual FireboltOperation<FireboltVoid> MediaRateChange(const std::string& entityId, double rate) = 0;
    virtual FireboltOperation<FireboltVoid> MediaRenditionChange(const std::string& entityId, int bitrate, int width, int height, const std::string& profile) = 0;
    virtual FireboltOperation<FireboltVoid> MediaEnded(const std::string& entityId) = 0;
};

class Metrics final : public IMetrics {
public:
    explicit Metrics(FireboltClient& client) : m_client(client) { }

    FireboltOperation<FireboltVoid> MediaLoadStart(const std::string& entityId) override { return Call("metrics.mediaLoadStart", Params(entityId)); }
    FireboltOperation<FireboltVoid> MediaPlay(const std::string& entityId) override { return Call("metrics.mediaPlay", Params(entityId)); }
    FireboltOperation<FireboltVoid> MediaPlaying(const std::string& entityId) override { return Call("metrics.mediaPlaying", Params(entityId)); }
    FireboltOperation<FireboltVoid> MediaPause(const std::string& entityId) override { return Call("metrics.mediaPause", Params(entityId)); }
    FireboltOperation<FireboltVoid> MediaWaiting(const std::string& entityId) override { return Call("metrics.mediaWaiting", Params(entityId)); }
    FireboltOperation<FireboltVoid> MediaEnded(const std::string& entityId) override { return Call("metrics.mediaEnded", Params(entityId)); }

    FireboltOperation<FireboltVoid> MediaProgress(const std::string& entityId, double progress) override
    {
        auto params = Params(entityId);
        params.Set("progress", FireboltFloat(progress));
        return Call("metrics.mediaProgress", params);
    }
    FireboltOperation<FireboltVoid> MediaSeeking(const std::string& entityId, double target) override
    {
        auto params = Params(entityId);
        params.Set("target", FireboltFloat(target));
        return Call("metrics.mediaSeeking", params);
    }
    FireboltOperation<FireboltVoid> MediaSeeked(const std::string& entityId, double position) override
    {
        auto params = Params(entityId);
        params.Set("position", FireboltFloat(position));
        return Call("metrics.mediaSeeked", params);
    }
    FireboltOperation<FireboltVoid> MediaRateChange(const std::string& entityId, double rate) override
    {
        auto params = Params(entityId);
        params.Set("rate", FireboltFloat(rate));
        return Call("metrics.mediaRateChange", params);
    }
    FireboltOperation<FireboltVoid> MediaRenditionChange(const std::string& entityId, int bitrate, int width, int height, const std::string& profile) override
    {
        auto params = Params(entityId);
        params.Set("bitrate", FireboltInteger(bitrate));
        params.Set("width", FireboltInteger(width));
        params.Set("height", FireboltInteger(height));
        if (!profile.empty())
            params.Set("profile", FireboltString(profile));
        return Call("metrics.mediaRenditionChange", params);
    }

private:
    static FireboltObject Params(const std::string& entityId)
    {
        FireboltObject params;
        params.Set("entityId", FireboltString(entityId));
        return params;
    }
    FireboltOperation<FireboltVoid> Call(const char* method, const FireboltObject& params)
    {
        return m_client.Send<FireboltVoid>(method, params);
    }

    FireboltClient& m_client;
};

inline IMetrics& MetricsInterface(FireboltClient& client) { return client.GetInterface<Metrics>(); }

} // namespace metrics
} // namespace firebolt
