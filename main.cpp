//
// Created by FlyZebra on 2021/9/30 0030.
//
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <binder/IPCThreadState.h>

#include "ServerManager.h"
#include "TerminalSession.h"
#include "RtspServer.h"
#include "InputServer.h"
#include "EncoderAudio.h"
#include "EncoderVideo.h"
#include "FlyLog.h"


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
    FLOGD("main client is start.\n");
    signal(SIGPIPE, SIG_IGN);
    isStop = false;
    status_t err = configureSignals();
    if (err != NO_ERROR) FLOGD("configureSignals failed!");

    androidSetThreadPriority(gettid(), -12);
    sp<ProcessState> proc(ProcessState::self());
    ProcessState::self()->startThreadPool();

    ServerManager* manager = new ServerManager();
    InputServer* input = new InputServer(manager);
    RtspServer* rtsp = new RtspServer(manager);
    TerminalSession* session = new TerminalSession(manager);
    sp<EncoderAudio> audio = new EncoderAudio(manager);
    sp<EncoderVideo> video = new EncoderVideo(manager);
    sp<android::ALooper> looper_video = new android::ALooper;
    looper_video->registerHandler(video);
    looper_video->start(false);
    video->loopStart();

    while(!isStop){
        usleep(1000000);
    }
    
    looper_video->unregisterHandler(video->id());
    looper_video->stop();

    delete input;
    delete rtsp;
    delete session;
    delete manager;
    
    FLOGD("main client is exit.\n");
    return 0;
}