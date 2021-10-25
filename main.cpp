//
// Created by FlyZebra on 2021/9/30 0030.
//
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <binder/IPCThreadState.h>
#include <cutils/properties.h>
#include "ServerManager.h"
#include "Global.h"
#include "FlyLog.h"
#include "mediastream/EncoderAudio.h"
#include "mediastream/EncoderVideo.h"
#include "remotecore/TerminalSession.h"
#include "rtspserver/RtspServer.h"
#include "inputserver/InputServer.h"
#include "zebraservice/ZebraService.h"

using namespace android;

static volatile bool isStop = false;

static struct sigaction gOrigSigactionINT;
static struct sigaction gOrigSigactionHUP;

static void signalCatcher(int signum)
{
    isStop = true;
    switch (signum) {
        case SIGINT:
        case SIGHUP:
            sigaction(SIGINT, &gOrigSigactionINT, NULL);
            sigaction(SIGHUP, &gOrigSigactionHUP, NULL);
            break;
        default:
            abort();
            break;
    }
}

static status_t configureSignals() 
{
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = signalCatcher;
    if (sigaction(SIGINT, &act, &gOrigSigactionINT) != 0) {
        status_t err = -errno;
        FLOGD("Unable to configure SIGINT handler: %s", strerror(errno));
        return err;
    }
    if (sigaction(SIGHUP, &act, &gOrigSigactionHUP) != 0) {
        status_t err = -errno;
        FLOGD("Unable to configure SIGHUP handler: %s", strerror(errno));
        return err;
    }
    return NO_ERROR;
}

int32_t main(int32_t  argc,  char*  argv[])
{
    FLOGD("###mctl  Ver 2.0 Date 2021025###");
    FLOGD("main client is start.\n");
    signal(SIGPIPE, SIG_IGN);
    isStop = false;
    status_t err = configureSignals();
    if (err != NO_ERROR) FLOGD("configureSignals failed!");

    androidSetThreadPriority(gettid(), -10);
    sp<ProcessState> proc(ProcessState::self());
    char temp[PROPERTY_VALUE_MAX] = {0};
    property_get("persist.sys.nv.sn", temp, "0");
    int64_t sn = strtoul(temp, NULL, 10);
    memcpy(&mTerminal, &sn, 8);

    ServerManager* manager = new ServerManager();
    InputServer* input = new InputServer(manager);
    RtspServer* rtsp = new RtspServer(manager);
    TerminalSession* session = new TerminalSession(manager);

    sp<EncoderAudio> audio = new EncoderAudio(manager);
    sp<EncoderVideo> video = new EncoderVideo(manager);

    int32_t ret = ZebraService::init(manager);
    ProcessState::self()->startThreadPool();
    IPCThreadState::self()->joinThreadPool();

    while(!isStop){
        usleep(1000000);
    }

    delete input;
    delete rtsp;
    delete session;
    delete manager;
    
    FLOGD("main client is exit.\n");
    return 0;
}
