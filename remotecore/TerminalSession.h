//
// Created by FlyZebra on 2021/9/30 0030.
//

#ifndef ANDROID_TERMINALSESSION_H
#define ANDROID_TERMINALSESSION_H

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
    void timerThread();
    void sendData(const char* data, int32_t size);
    void resetConnect();

private:
    ServerManager* mManager;
    int32_t mSocket;
    volatile bool is_stop;
    
    std::mutex mlock_conn;
    volatile bool is_connect;
    std::condition_variable mcond_conn;

    std::thread *send_t;
    std::mutex mlock_send;
    std::vector<char> sendBuf;
    std::condition_variable mcond_send;

    std::thread *recv_t;
    std::thread *hand_t;
    std::mutex mlock_recv;
    std::vector<char> recvBuf;
    std::condition_variable mcond_recv;
    
    std::thread *time_t;
};

#endif //ANDROID_TERMINALSESSION_H
