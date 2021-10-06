//
// Created by FlyZebra on 2021/7/30 0030.
//
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ABuffer.h>

#include "EncoderVideo.h"
#include "FlyLog.h"
#include "screenrecord.h"
#include "HandlerEvent.h"
#include "Command.h"

using namespace android;

EncoderVideo::EncoderVideo(ServerManager* manager)
:mManager(manager)
,clientNum(0)
{
    FLOGD("%s()", __func__);
    mManager->registerListener(this);
}

EncoderVideo::~EncoderVideo()
{
    mManager->unRegisterListener(this);
    FLOGD("%s()", __func__);
}

int32_t EncoderVideo::notify(const char* data, int32_t size)
{
    struct NotifyData* notifyData = (struct NotifyData*)data;
    switch (notifyData->type){
    case 0x0102:
        clientNum++;
        if(clientNum<=1){
            startRecord();
        }
        return -1;
    case 0x0202:
        clientNum--;
        if(clientNum<=0){
            stopRecord();
        }
        return -1;
    }
    return -1;
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
                        sp<ABuffer> data;
                        CHECK(msg->findBuffer("data", &data));
                        char spspps[sizeof(spsppsdata)];
                        memcpy(spspps,spsppsdata,sizeof(spsppsdata));
                        int32_t size = data->capacity()+12;
                        spspps[6] = (size & 0xFF000000) >> 24;
                        spspps[7] = (size & 0xFF0000) >> 16;
                        spspps[8] = (size & 0xFF00) >> 8;
                        spspps[9] =  size & 0xFF;
                        std::lock_guard<std::mutex> lock (mManager->mlock_up);
                        mManager->updataAsync(spspps, sizeof(spspps));
                        mManager->updataAsync((const char *)data->data(), data->capacity());
                    }
                    break;
                case kWhatVideoFrameData:
                    {
                        sp<ABuffer> data;
                        CHECK(msg->findBuffer("data", &data));
                        int64_t ptsUsec;
                        CHECK(msg->findInt64("ptsUsec", &ptsUsec));
                        char vdata[sizeof(videodata)];
                        memcpy(vdata,videodata,sizeof(videodata));
                        int32_t size = data->capacity()+12;
                        vdata[6] = (size & 0xFF000000) >> 24;
                        vdata[7] = (size & 0xFF0000) >> 16;
                        vdata[8] = (size & 0xFF00) >> 8;
                        vdata[9] =  size & 0xFF;
                        vdata[18] = (ptsUsec & 0xFF000000) >> 24;
                        vdata[19] = (ptsUsec & 0xFF0000) >> 16;
                        vdata[20] = (ptsUsec & 0xFF00) >> 8;
                        vdata[21] =  ptsUsec & 0xFF;
                        std::lock_guard<std::mutex> lock (mManager->mlock_up);
                        mManager->updataAsync(vdata, sizeof(vdata));
                        mManager->updataAsync((const char *)data->data(), data->capacity());
                    }
                    break;
            }
        }
   	        break;
   }
}

void EncoderVideo::loopStart()
{
    mNotify = new AMessage(kWhatMediaNotify, this);
}


void EncoderVideo::startRecord()
{
    stopRecord();
    if(isRunning) return;
    FLOGD("EncoderVideo::startRecord()");
    if(isRunning){
        FLOGE("EncoderVideo is running, exit!");
        return;
    }
    pthread_t run_tid;
    int ret = pthread_create(&run_tid, nullptr, _run_record, (void *) this);
    if (ret != 0) {
    	FLOGE("create socket thread error!");
    }
}

void *EncoderVideo::_run_record(void *argv)
{
    FLOGD("EncoderVideo::_run_record() start!");
    auto *p=(EncoderVideo *)argv;
    p->isRunning = true;
    screenrecord_start(p->mNotify);
    p->isRunning = false;
    FLOGD("EncoderVideo::_run_record() exit!");
    return 0;
}

void EncoderVideo::stopRecord()
{
    FLOGD("EncoderVideo::stopRecord()");
    screenrecord_stop();
    usleep(100000);
    while(isRunning){
        FLOGE("stopRecord -> wait run thread exit!");
        usleep(100000);
    }
}
