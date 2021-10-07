//
// Created by FlyZebra on 2021/8/12 0012.
//

#ifndef ANDROID_EncoderAudio_H
#define ANDROID_EncoderAudio_H

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
#include "ServerManager.h"

#define SERVER_IP                    "127.0.0.1"
#define SERVER_PORT                  "18183"
#define PROP_IP                      "persist.sys.audio.serverip"
#define PROP_PROT                    "persist.sys.audio.serverport"

namespace android {

class EncoderAudio : public AHandler, public INotify {
public:
    EncoderAudio(ServerManager* manager);
    ~EncoderAudio();
    
public:
    virtual int32_t notify(const char* data, int32_t size);

protected:
    virtual void onMessageReceived(const sp<AMessage> &msg);

private:
    void serverSocket();
    void clientSocket();
    void serverClose();
    
    void ffmpegInit();
    void ffmpegRelease();

    void codecInit();
    void codecRelease();

    void encoderPCMData(sp<ABuffer> pcmdata,      int32_t sample_fmt, int32_t sample_rate, int64_t ch_layout);
    void clientExit(int32_t socket_fd);

private:
    struct client_conn {
        int32_t socket;
        int32_t status;
    };

    status_t err;

    ServerManager* mManager;
    
    sp<ALooper> mLooper;
    sp<MediaCodec> mCodec;
    Vector<sp<MediaCodecBuffer>> outBuffers;
    Vector<sp<MediaCodecBuffer>> inBuffers;

    volatile bool is_stop;
    volatile bool is_codec;

    std::mutex mlock_temp;
    std::vector<int32_t> temp_clients;

    std::mutex mlock_client;
    std::list<int32_t> audio_clients;
    
    int32_t server_socket;

    std::map<int32_t, struct SwrContext*> swr_cxts;
    uint8_t *out_buf;
    
    std::thread *server_t;
    std::thread *client_t;
    std::thread *close_t;
    
    std::mutex mlock_work;
    volatile int32_t clientNum;
    std::condition_variable mcond_work;
    
    int32_t sequencenumber;

};

}; // namespace android

#endif //ANDROID_EncoderAudio_H
