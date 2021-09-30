//
// Created by FlyZebra on 2021/9/30 0030.
//
#include <sys/types.h>
#include <sys/stat.h>
#include <binder/IPCThreadState.h>

#include "ServerManager.h"
#include "TerminalSession.h"
#include "FlyLog.h"

using namespace android;

static volatile bool is_stop = false;

static struct sigaction gOrigSigactionINT;
static struct sigaction gOrigSigactionHUP;

static void signalCatcher(int signum)
{
    FLOGE("signalCatcher %d", signum);
    is_stop = true;
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

static status_t configureSignals() {
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = signalCatcher;
    if (sigaction(SIGINT, &act, &gOrigSigactionINT) != 0) {
        status_t err = -errno;
        FLOGE("Unable to configure SIGINT handler: %s", strerror(errno));
        return err;
    }
    if (sigaction(SIGHUP, &act, &gOrigSigactionHUP) != 0) {
        status_t err = -errno;
        FLOGE("Unable to configure SIGHUP handler: %s", strerror(errno));
        return err;
    }
    return NO_ERROR;
}

int32_t main(int32_t  argc,  char*  argv[])
{
    FLOGD("main client is start.");

    is_stop = false;
    status_t err = configureSignals();
    if (err != NO_ERROR) FLOGE("configureSignals failed!");

    androidSetThreadPriority(gettid(), -10);
    sp<ProcessState> proc(ProcessState::self());
    ProcessState::self()->startThreadPool();

    ServerManager *manager = new ServerManager();
    TerminalSession *session = new TerminalSession(manager);

    while(!is_stop){
        usleep(1000000);
    }

    delete session;
    delete manager;
    FLOGD("main client is exit.");
}
