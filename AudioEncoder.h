//
// Created by FlyZebra on 2021/8/12 0012.
//

#ifndef ANDROID_AudioEncoder_H
#define ANDROID_AudioEncoder_H

#include <binder/IPCThreadState.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>
#include <ui/DisplayInfo.h>
#include <media/openmax/OMX_IVCommon.h>
#include <media/stagefright/foundation/AHandler.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/MediaCodec.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaMuxer.h>
#include <media/ICrypto.h>
#include <media/MediaCodecBuffer.h>

#include <media/IMediaPlayerService.h>
#include <media/MediaAnalyticsItem.h>
#include <media/stagefright/ACodec.h>
#include <media/stagefright/AudioSource.h>
#include <media/stagefright/AMRWriter.h>
#include <media/stagefright/AACWriter.h>
#include <media/stagefright/CameraSource.h>
#include <media/stagefright/CameraSourceTimeLapse.h>
#include <media/stagefright/MPEG2TSWriter.h>
#include <media/stagefright/MPEG4Writer.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/MediaCodecSource.h>
#include <media/stagefright/PersistentSurface.h>
#include <media/MediaProfiles.h>

extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include <system/audio.h>
#include "Config.h"

#define SERVER_IP                    "127.0.0.1"
#define SERVER_PORT                  "18183"
#define PROP_IP                      "persist.sys.audio.serverip"
#define PROP_PROT                    "persist.sys.audio.serverport"

namespace android {

class AudioEncoder : public AHandler {
public:
    AudioEncoder(sp<AMessage> notify);
    ~AudioEncoder();
    void start();
    void stop();
    void startRecord();
    void stopRecord();

protected:
    virtual void onMessageReceived(const sp<AMessage> &msg);

private:
    static void *_audio_socket(void *arg);
    static void *_audio_client_socket(void *arg);

    void handleRecvPCMData(const sp<AMessage> &msg);
    void handleClientExit(const sp<AMessage> &msg);

    void codecInit();
    void codecRelease();
    void ffmpegInit();
    void ffmpegRelease();

    struct client_conn {
        int32_t socket;
        int32_t status;
    };

    status_t err;

    sp<AMessage> mNotify;
    sp<ALooper> mLooper;
    sp<MediaCodec> mCodec;
    Vector<sp<MediaCodecBuffer>> outBuffers;
    Vector<sp<MediaCodecBuffer>> inBuffers;

    Mutex mLock;
    std::vector<int32_t> thread_sockets;
    std::vector<client_conn> conn_sockets;
    volatile bool is_stop;
    volatile bool is_running;
    volatile bool is_codec;
    pthread_t init_socket_tid;
    int32_t server_socket;

    std::map<int32_t, struct SwrContext*> swr_cxts;
    uint8_t *out_buf;

};

}; // namespace android

#endif //ANDROID_AudioEncoder_H
