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
,sequencenumber(0)
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
    case TYPE_VIDEO_START:
        clientNum++;
        if(clientNum<=1){
            startRecord();
        }
        return 1;
    case TYPE_VIDEO_STOP:
        clientNum--;
        if(clientNum<=0){
            clientNum = 0;
            stopRecord();
        }
        return 1;
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
                        char spspps[sizeof(SPSPPS_DATA)];
                        memcpy(spspps,SPSPPS_DATA,sizeof(SPSPPS_DATA));
                        int32_t size = data->capacity()+16;
                        spspps[4] = (size & 0xFF000000) >> 24;
                        spspps[5] = (size & 0xFF0000) >> 16;
                        spspps[6] = (size & 0xFF00) >> 8;
                        spspps[7] =  size & 0xFF;
                        spspps[16] = (sequencenumber & 0xFF000000) >> 24;
                        spspps[17] = (sequencenumber & 0xFF0000) >> 16;
                        spspps[18] = (sequencenumber & 0xFF00) >> 8;
                        spspps[19] =  sequencenumber & 0xFF;                       
                        std::lock_guard<std::mutex> lock (mManager->mlock_up);
                        mManager->updataAsync(spspps, sizeof(spspps));
                        mManager->updataAsync((const char *)data->data(), data->capacity());
                    }
                    break;
                case kWhatVideoFrameData:
                    {
                        sequencenumber++;
                        sp<ABuffer> data;
                        CHECK(msg->findBuffer("data", &data));
                        int64_t ptsUsec;
                        CHECK(msg->findInt64("ptsUsec", &ptsUsec));
                        char vdata[sizeof(VIDEO_DATA)];
                        memcpy(vdata,VIDEO_DATA,sizeof(VIDEO_DATA));
                        int32_t size = data->capacity()+16;
                        vdata[4] = (size & 0xFF000000) >> 24;
                        vdata[5] = (size & 0xFF0000) >> 16;
                        vdata[6] = (size & 0xFF00) >> 8;
                        vdata[7] =  size & 0xFF;
                        vdata[16] = (sequencenumber & 0xFF000000) >> 24;
                        vdata[17] = (sequencenumber & 0xFF0000) >> 16;
                        vdata[18] = (sequencenumber & 0xFF00) >> 8;
                        vdata[19] =  sequencenumber & 0xFF; 
                        vdata[20] = (ptsUsec & 0xFF000000) >> 24;
                        vdata[21] = (ptsUsec & 0xFF0000) >> 16;
                        vdata[22] = (ptsUsec & 0xFF00) >> 8;
                        vdata[23] =  ptsUsec & 0xFF;
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
