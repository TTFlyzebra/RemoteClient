//
// Created by FlyZebra on 2021/7/30 0030.
//

#ifndef ANDROID_SCREENDISPLAY_H
#define ANDROID_SCREENDISPLAY_H

#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/AHandler.h>
#include "ServerManager.h"

namespace android {

class EncoderVideo : public AHandler, public INotify {
public:
    EncoderVideo(ServerManager* manager);
    ~EncoderVideo();
    void startRecord();
    void stopRecord();
    void loopStart();
    
public:
    virtual int32_t notify(const char* data, int32_t size);
    
protected:
    virtual void onMessageReceived(const sp<AMessage> &msg);

private:
	static void *_run_record(void *arg);
    bool isRunning = false;
    ServerManager* mManager;
    sp<AMessage> mNotify;
    volatile int32_t clientNum;
};

}; // namespace android

#endif //ANDROID_SCREENDISPLAY_H
