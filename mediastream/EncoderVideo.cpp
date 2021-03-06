//
// Created by FlyZebra on 2021/7/30 0030.
//
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ABuffer.h>

#include "EncoderVideo.h"
#include "screenrecord/screenrecord.h"
#include "HandlerEvent.h"
#include "Command.h"
#include "Global.h"
#include "FlyLog.h"

using namespace android;

EncoderVideo::EncoderVideo(ServerManager* manager)
:mManager(manager)
,is_stop(false)
,is_record(false)
,is_re_record(false)
,sequencenumber(0)
{
    FLOGD("%s()", __func__);
    mManager->registerListener(this);
    loop_t = new std::thread(&EncoderVideo::loopStart, this);
    check_t = new std::thread(&EncoderVideo::clientChecked, this);
}

EncoderVideo::~EncoderVideo()
{
    mManager->unRegisterListener(this);
    is_stop = true;
    {
        std::lock_guard<std::mutex> lock (mlock_work);
        mcond_work.notify_all();
    }
    loop_t->join();
    check_t->join();
    delete loop_t;
    delete check_t;
    FLOGD("%s()", __func__);
}

int32_t EncoderVideo::notify(const char* data, int32_t size)
{
    struct NotifyData* notifyData = (struct NotifyData*)data;
    switch (notifyData->type){
    case TYPE_VIDEO_START:
        is_re_record = true;
        {
            lastHeartBeat = systemTime(CLOCK_MONOTONIC);
            int64_t uid;
            memcpy(&uid, data+16, 8);
            std::lock_guard<std::mutex> lock (mlock_work);
            std::map<int64_t, int64_t>::iterator it = mUsers.find(uid);
            if(it != mUsers.end()){
                it->second = lastHeartBeat;
            }else{
                mUsers.emplace(uid,lastHeartBeat);
                mcond_work.notify_one();
            }
            FLOGD("EncoderVideo recv start mUsers.size=%zu", mUsers.size());
        }
        return 0;
    case TYPE_HEARTBEAT_VIDEO:
        {
            lastHeartBeat = systemTime(CLOCK_MONOTONIC);
            int64_t uid;
            memcpy(&uid, data+16, 8);
            std::lock_guard<std::mutex> lock (mlock_work);
            std::map<int64_t, int64_t>::iterator it = mUsers.find(uid);
            if(it != mUsers.end()){
                it->second = lastHeartBeat;
            }else{
                mUsers.emplace(uid,lastHeartBeat);
                mcond_work.notify_one();
            }
        }
        return 0;
    case TYPE_VIDEO_STOP:
        {
            int64_t uid;
            memcpy(&uid, data+16, 8);
            std::lock_guard<std::mutex> lock (mlock_work);
            mUsers.erase(uid);
            FLOGD("EncoderVideo recv stop mUsers.size=%zu", mUsers.size());
        }
        return 0;
    }
    return 0;
}

void EncoderVideo::onMessageReceived(const sp<AMessage> &msg)
{
    switch (msg->what()) {
   	    case kWhatMediaNotify:
        {
            int32_t type;
            CHECK(msg->findInt32("type", &type));
            switch (type) {
                case kWhatSPSPPSData:
                {
                    sequencenumber++;
                    sp<ABuffer> data;
                    CHECK(msg->findBuffer("data", &data));
                    char spspps[sizeof(SPSPPS_DATA) + data->capacity()];
                    memcpy(spspps,SPSPPS_DATA,sizeof(SPSPPS_DATA));
                    int32_t size = data->capacity()+16;
                    spspps[4] = (size & 0xFF000000) >> 24;
                    spspps[5] = (size & 0xFF0000) >> 16;
                    spspps[6] = (size & 0xFF00) >> 8;
                    spspps[7] =  size & 0xFF;
                    memcpy(spspps+8,mTerminal.tid,8);
                    spspps[16] = (sequencenumber & 0xFF000000) >> 24;
                    spspps[17] = (sequencenumber & 0xFF0000) >> 16;
                    spspps[18] = (sequencenumber & 0xFF00) >> 8;
                    spspps[19] =  sequencenumber & 0xFF;
                    memcpy(spspps + sizeof(SPSPPS_DATA),data->data(),data->capacity());
                    mManager->updataAsync(spspps, sizeof(spspps));
                }
                    break;
                case kWhatVideoFrameData:
                {
                    sequencenumber++;
                    sp<ABuffer> data;
                    CHECK(msg->findBuffer("data", &data));
                    int64_t ptsUsec;
                    CHECK(msg->findInt64("ptsUsec", &ptsUsec));
                    char vdata[sizeof(VIDEO_DATA) + data->capacity()];
                    memcpy(vdata,VIDEO_DATA,sizeof(VIDEO_DATA));
                    int32_t size = data->capacity()+16;
                    vdata[4] = (size & 0xFF000000) >> 24;
                    vdata[5] = (size & 0xFF0000) >> 16;
                    vdata[6] = (size & 0xFF00) >> 8;
                    vdata[7] =  size & 0xFF;
                    memcpy(vdata+8,mTerminal.tid,8);
                    vdata[16] = (sequencenumber & 0xFF000000) >> 24;
                    vdata[17] = (sequencenumber & 0xFF0000) >> 16;
                    vdata[18] = (sequencenumber & 0xFF00) >> 8;
                    vdata[19] =  sequencenumber & 0xFF;
                    vdata[20] = (ptsUsec & 0xFF000000) >> 24;
                    vdata[21] = (ptsUsec & 0xFF0000) >> 16;
                    vdata[22] = (ptsUsec & 0xFF00) >> 8;
                    vdata[23] =  ptsUsec & 0xFF;
                    memcpy(vdata+sizeof(VIDEO_DATA), data->data(), data->capacity());
                    mManager->updataAsync(vdata, sizeof(vdata));
                }
                    break;
            }
        }
   	        break;
   }
}

void EncoderVideo::loopStart()
{
    sp<AMessage> msg = new AMessage(kWhatMediaNotify, this);
    sp<ALooper> looper = new android::ALooper;
    looper->registerHandler(this);
    looper->start(false);
    while(!is_stop){
        {
            std::unique_lock<std::mutex> lock (mlock_work);
            while(!is_stop && mUsers.empty()) {
                mcond_work.wait(lock);
            }
        }
        if(is_stop) return;
        FLOGD("EncoderVideo screenrecord start");
        is_re_record = false;
        is_record = true;
        screenrecord_start(msg);
        is_record = false;
        FLOGD("EncoderVideo screenrecord end");
    }
    looper->unregisterHandler(id());
    looper->stop();
}

void EncoderVideo::clientChecked()
{
    while(!is_stop){
        for(int i=0;i<50;i++){
            usleep(100000);
            if(is_stop) return;
        }
        {
            std::lock_guard<std::mutex> lock (mlock_work);
            std::map<int64_t, int64_t>::iterator it = mUsers.begin();
            while(it != mUsers.end()){
                int32_t time = ((systemTime(SYSTEM_TIME_MONOTONIC) - it->second)/1000000)&0xFFFFFFFF;
                if(time > 61000){
                    it = mUsers.erase(it);
                    FLOGD("EncoderVideo timeout to remove client! size=%zu", mUsers.size());
                }else{
                    it++;
                }
            }
        }
        if(mUsers.empty() || is_re_record){
            screenrecord_stop();
            for(int i=0;i<30;i++){
                if(is_stop) break;
                usleep(100000);
            }
            if(is_re_record) {
                FLOGE("EncoderVideo can't stop screen record, will exit");
                exit(1);
            }
        }
    }
    return;
}
