//
// Created by FlyZebra on 2021/8/9 0009.
//

#ifndef ANDROID_INPUTSERVER_H
#define ANDROID_INPUTSERVER_H

#include "ServerManager.h"
#include "InputClient.h"

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
    void inputKey(int32_t fd, int16_t key);
    void inputTouch(int32_t fd, int16_t x, int16_t y, int16_t action);
    void runCommand(const char *kCommand, char* const argv[]);

private:
    ServerManager* mManager;
    volatile bool is_stop;
    
    std::thread *server_t;
    int32_t server_socket;

    std::mutex mlock_client;
    std::list<InputClient*> input_clients;
        
    std::thread *remove_t;
    std::mutex mlock_remove;
    std::vector<InputClient*> remove_clients;
    std::condition_variable mcond_remove;

    std::thread *handle_t;
    std::mutex mlock_event;
    std::vector<char> events;
    std::condition_variable mcond_event;
};

#endif //ANDROID_INPUTSERVER_H
