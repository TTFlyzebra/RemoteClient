//
// Created by FlyZebra on 2021/10/02 0016.
//

#ifndef ANDROID_INPUTCLIENT_H
#define ANDROID_INPUTCLIENT_H

#include <arpa/inet.h>
#include "ServerManager.h"
#include "FlyLog.h"

class InputServer;

class InputClient : public INotify {
public:
    InputClient(InputServer* server, ServerManager* manager, int32_t socket);
    ~InputClient();

public:
    virtual int32_t notify(const char* data, int32_t size);

private:
    void disConnect();
    void sendThread();
    void recvThread();
    void handleData();
    void sendData(const char* data, int32_t size);

private:
    InputServer* mServer;
    ServerManager* mManager;     
    int32_t mSocket;
    volatile bool is_stop;
    volatile bool is_disconnect;
    
    std::thread *send_t;
    std::mutex mlock_send;
    std::vector<char> sendBuf;
    std::condition_variable mcond_send;

    std::thread *recv_t;
    std::thread *hand_t;
    std::mutex mlock_recv;
    std::vector<char> recvBuf;
    std::condition_variable mcond_recv;

};

#endif //ANDROID_INPUTCLIENT_H

