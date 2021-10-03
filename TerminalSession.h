//
// Created by FlyZebra on 2021/9/30 0030.
//

#ifndef ANDROID_TERMINALSESSION_H
#define ANDROID_TERMINALSESSION_H

#include <stdint.h>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unistd.h>
#include "ServerManager.h"

class TerminalSession : public INotify{
public:
    TerminalSession(ServerManager* manager);
    ~TerminalSession();

public:
    virtual int32_t notify(const char* data, int32_t size);

private:
    void connThread();
    void sendThread();
    void handThread();

private:
    ServerManager* mManager;
    int32_t mSocket;

    volatile bool is_stop;
    volatile bool is_connect;
    std::mutex mlock_conn;
    std::condition_variable mcond_conn;

    std::thread *send_t;
    std::vector<char> sendBuf;
    std::mutex mlock_send;
    std::condition_variable mcond_send;

    std::thread *recv_t;
    std::thread *hand_t;
};

#endif //ANDROID_TERMINALSESSION_H
