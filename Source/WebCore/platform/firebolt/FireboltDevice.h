// "Generated" header for the Firebolt Device module (hand-written here as the
// reference for what the code generator must emit). Header-only; depends only
// on public framework types.
//
// Wire format (Firebolt Core SDK, Device module):
//   device.hdr               -> {"hdr10":bool,"hdr10Plus":bool,"dolbyVision":bool,"hlg":bool}
//   device.onHdrChanged      -> same payload as an event
//   device.screenResolution  -> [width, height]
//   device.videoResolution   -> [width, height]
#pragma once
#include <string>
#include <utility>

#include "FireboltClient.h"
#include "FireboltModel.h"
#include "FireboltObject.h"
#include "FireboltOperation.h"
#include "FireboltSubscription.h"
#include "FireboltTypes.h"

namespace firebolt {
namespace device {

// ---- models --------------------------------------------------------------

class HDRFormatMap final : public FireboltModel<HDRFormatMap> {
public:
    FireboltBoolean hdr10;
    FireboltBoolean hdr10Plus;
    FireboltBoolean dolbyVision;
    FireboltBoolean hlg;

    void Write(FireboltObject& object) const
    {
        object.Set("hdr10", hdr10);
        object.Set("hdr10Plus", hdr10Plus);
        object.Set("dolbyVision", dolbyVision);
        object.Set("hlg", hlg);
    }

    bool Read(const FireboltObject& object)
    {
        if (object.GetKind() != FireboltObject::Kind::Object)
            return false;
        // Absent formats stay false: gateways may omit unsupported entries.
        object.Get("hdr10", hdr10);
        object.Get("hdr10Plus", hdr10Plus);
        object.Get("dolbyVision", dolbyVision);
        object.Get("hlg", hlg);
        return true;
    }
};

// OpenRPC tuple [width, height].
class Resolution final : public FireboltModel<Resolution> {
public:
    FireboltInteger width;
    FireboltInteger height;

    void Write(FireboltObject& object) const
    {
        object.AppendChild(width.ToObject());
        object.AppendChild(height.ToObject());
    }

    bool Read(const FireboltObject& object)
    {
        FireboltObject w, h;
        return object.GetKind() == FireboltObject::Kind::Array && object.Size() == 2
            && object.GetAt(0, w) && object.GetAt(1, h)
            && width.FromObject(w) && height.FromObject(h);
    }
};

// ---- interface -----------------------------------------------------------

class IDevice {
public:
    virtual ~IDevice() = default;

    virtual FireboltOperation<HDRFormatMap> Hdr() = 0;
    virtual FireboltSubscription OnHdrChanged(EventCallback<HDRFormatMap> listener) = 0;

    virtual FireboltOperation<Resolution> ScreenResolution() = 0;
    virtual FireboltSubscription OnScreenResolutionChanged(EventCallback<Resolution> listener) = 0;

    virtual FireboltOperation<Resolution> VideoResolution() = 0;
};

class Device final : public IDevice {
public:
    explicit Device(FireboltClient& client) : m_client(client) { }

    FireboltOperation<HDRFormatMap> Hdr() override
    {
        return m_client.Send<HDRFormatMap>("device.hdr", FireboltObject {});
    }
    FireboltSubscription OnHdrChanged(EventCallback<HDRFormatMap> listener) override
    {
        return m_client.Subscribe<HDRFormatMap>("device.onHdrChanged", std::move(listener));
    }

    FireboltOperation<Resolution> ScreenResolution() override
    {
        return m_client.Send<Resolution>("device.screenResolution", FireboltObject {});
    }
    FireboltSubscription OnScreenResolutionChanged(EventCallback<Resolution> listener) override
    {
        return m_client.Subscribe<Resolution>("device.onScreenResolutionChanged", std::move(listener));
    }

    FireboltOperation<Resolution> VideoResolution() override
    {
        return m_client.Send<Resolution>("device.videoResolution", FireboltObject {});
    }

private:
    FireboltClient& m_client;
};

inline IDevice& DeviceInterface(FireboltClient& client) { return client.GetInterface<Device>(); }

} // namespace device
} // namespace firebolt
