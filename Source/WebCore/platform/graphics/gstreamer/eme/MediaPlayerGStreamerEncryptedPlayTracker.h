#pragma once

#if ENABLE(ENCRYPTED_MEDIA) && USE(GSTREAMER)

#include "GStreamerCommon.h"
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/text/WTFString.h>
#include <wtf/Seconds.h>
#include <wtf/MonotonicTime.h>
#include <gst/gst.h>

namespace WebCore {

class MediaPlayerGStreamerEncryptedPlayTracker {
    WTF_MAKE_FAST_ALLOCATED;
public:
    MediaPlayerGStreamerEncryptedPlayTracker() = default;
    ~MediaPlayerGStreamerEncryptedPlayTracker();

    void setURL(String url);
    void notifyStateChange(GstState current, GstState pending);
    void notifyDecryptionStarted(const gchar* keySystem);

private:
    enum PlayState {
        PLAYBACK_STARTED,
        DECRYPTION_STARTED
    };
    void logPlayStart(PlayState state);
    void logPlayEnd();

    String m_url;
    String m_keySystem;
    WTF::MonotonicTime m_playStart { MonotonicTime::fromRawSeconds(-1) };
    WTF::MonotonicTime m_decryptionStart { MonotonicTime::fromRawSeconds(-1) };
    WTF::Seconds m_playTime { -1 };
};

} //namespace WebCore

#endif // ENABLE(ENCRYPTED_MEDIA) && USE(GSTREAMER)
