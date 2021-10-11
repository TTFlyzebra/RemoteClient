//
// Created by FlyZebra on 2021/7/30 0030.
//

#ifndef ANDROID_SCREENDISPLAY_H
#define ANDROID_SCREENDISPLAY_H

#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/AHandler.h>
#include "ServerManager.h"
#include <map>

namespace android {

class EncoderVideo : public AHandler, public INotify {
public:
    EncoderVideo(ServerManager* manager);
    ~EncoderVideo();
    void loopStart();
    void clientChecked();
    
public:
    virtual int32_t notify(const char* data, int32_t size);
    
protected:
    virtual void onMessageReceived(const sp<AMessage> &msg);

private:
    ServerManager* mManager;
    bool is_stop;

    int32_t sequencenumber;

    std::mutex mlock_work;
    std::map<int64_t, int64_t> mTerminals;
    std::condition_variable mcond_work;
    int64_t lastHeartBeat;

    std::thread *loop_t;
    std::thread *check_t;
};

}; // namespace android

#endif //ANDROID_SCREENDISPLAY_H
