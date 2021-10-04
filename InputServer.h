//
// Created by FlyZebra on 2021/8/9 0009.
//

#ifndef ANDROID_INPUTSERVER_H
#define ANDROID_INPUTSERVER_H

#include <stdint.h>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "ServerManager.h"
#include "InputClient.h"

namespace android {

class InputServer : public INotify {
public:
    InputServer(ServerManager* manager);
    ~InputServer();
    void disconnectClient(InputClient* client);
    
public:
    virtual int32_t notify(const char* data, int32_t size);
    
private:
    void serverSocket();
    void removeClient();
    void handleInputEvent();
    void inputKey(int32_t fd, int32_t key);
    void inputTouch(int32_t fd, int32_t x, int32_t y, int32_t action);
    
private:
    ServerManager* mManager;
    volatile bool is_stop;
    
    std::thread *server_t;
    int32_t server_socket;

    std::list<InputClient*> input_clients;
    std::mutex mlock_server;
        
    std::thread *remove_t;
    std::vector<InputClient*> remove_clients;
    std::mutex mlock_remove;
    std::condition_variable mcond_remove;

    std::thread *handle_t;
};

}; // namespace android

#endif //ANDROID_INPUTSERVER_H