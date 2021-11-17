//
// Created by FlyZebra on 2021/8/9 0009.
//

#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include "InputServer.h"
#include "HandlerEvent.h"
#include "input.h"
#include "Config.h"
#include "Global.h"
#include "Command.h"
#include "FlyLog.h"

InputServer::InputServer(ServerManager* manager)
:mManager(manager)
,is_stop(false)
{
    FLOGD("%s()", __func__);
    mManager->registerListener(this);
    server_t = new std::thread(&InputServer::serverSocket, this);
    remove_t = new std::thread(&InputServer::removeClient, this);
    handle_t = new std::thread(&InputServer::handleInputEvent, this);
}

InputServer::~InputServer()
{
    mManager->unRegisterListener(this);
    is_stop = true;
    {
        std::lock_guard<std::mutex> lock (mlock_remove);
        mcond_remove.notify_one();
    }
    {
        std::lock_guard<std::mutex> lock (mlock_event);
        mcond_event.notify_one();
    }
    
    shutdown(server_socket, SHUT_RDWR);
    close(server_socket);
    {
        std::lock_guard<std::mutex> lock (mlock_client);
        for (std::list<InputClient*>::iterator it = input_clients.begin(); it != input_clients.end(); ++it) {
            delete ((InputClient*)*it);
        }
        input_clients.clear();
    }
    
    server_t->join();
    remove_t->join();
    handle_t->join();
    delete server_t;
    delete remove_t;
    delete handle_t;
    FLOGD("%s()", __func__);    
}

int32_t InputServer::notify(const char* data, int32_t size)
{
    struct NotifyData* notifyData = (struct NotifyData*)data;
    switch (notifyData->type) {
    case TYPE_INPUT_TOUCH:
    case TYPE_INPUT_KEY:
    case TYPE_INPUT_TEXT:
        {
            std::lock_guard<std::mutex> lock (mlock_event);
            events.insert(events.end(), data, data+size);
            mcond_event.notify_one();
        }
        return 0;
    }
    return 0;
}

void InputServer::serverSocket()
{
	FLOGD("InputServer socketServer start!");
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        FLOGE("InputServer socket error %s errno: %d", strerror(errno), errno);
        return;
    }

    struct sockaddr_in t_sockaddr;
    memset(&t_sockaddr, 0, sizeof(t_sockaddr));
    t_sockaddr.sin_family = AF_INET;
    t_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    t_sockaddr.sin_port = htons(INPUT_SERVER_TCP_PORT);
    int32_t ret = bind(server_socket,(struct sockaddr *) &t_sockaddr,sizeof(t_sockaddr));
    if (ret < 0) {
        FLOGE( "InputServer bind %d socket error %s errno: %d",INPUT_SERVER_TCP_PORT, strerror(errno), errno);
        return;
    }
    ret = listen(server_socket, 5);
    if (ret < 0) {
        FLOGE("InputServer listen error %s errno: %d", strerror(errno), errno);
        return;
    }
    while(!is_stop) {
        int32_t client_socket = accept(server_socket, (struct sockaddr*)NULL, NULL);
        if(client_socket < 0) {
            FLOGE("InputServer accpet socket error: %s errno :%d", strerror(errno), errno);
            continue;
        }
        if(is_stop) break;
		InputClient *client = new InputClient(this, mManager, client_socket);
        std::lock_guard<std::mutex> lock (mlock_client);
        input_clients.push_back(client);
    }
    shutdown(server_socket, SHUT_RDWR);
    close(server_socket);
    FLOGD("InputServer socketServer exit!");
	return;
}

void InputServer::removeClient()
{
    while(!is_stop){
        std::unique_lock<std::mutex> lock (mlock_remove);
        while (!is_stop && remove_clients.empty()) {
            mcond_remove.wait(lock);
        }
        if(is_stop) break;
        for (std::vector<InputClient*>::iterator it = remove_clients.begin(); it != remove_clients.end(); ++it) {
            {
                std::lock_guard<std::mutex> lock (mlock_client);
                input_clients.remove(((InputClient*)*it));
            }
            delete ((InputClient*)*it);
        }
        remove_clients.clear();
        FLOGD("InputServer::removeClient input_clients.size=%zu", input_clients.size());
    }
}


void InputServer::handleInputEvent()
{
    int32_t key_fd = open("/dev/input/event0",O_RDWR);
    //int32_t ts_fd = open("/dev/input/event2",O_RDWR);
    int32_t ts_fd = key_fd;
    while(!is_stop){
        {
            std::unique_lock<std::mutex> lock (mlock_event);
            while (!is_stop && events.size() < 8) {
                mcond_event.wait(lock);
            }
            if(is_stop) break;
        }
        char* data = (char*)&events[0];
        struct NotifyData* notifyData = (struct NotifyData*)&events[0];
        switch (notifyData->type) {
        case TYPE_INPUT_TOUCH:
            {
                int16_t x = (data[18]&0xFF)<<8|(data[19]&0xFF);
                int16_t y = (data[20]&0xFF)<<8|(data[21]&0xFF);;
                int16_t w = (data[22]&0xFF)<<8|(data[23]&0xFF);;
                int16_t h = (data[24]&0xFF)<<8|(data[25]&0xFF);;
                x = (int16_t)x * 1080 / w;
                y = (int16_t)y * 1920 / h;
                inputTouch(ts_fd, x, y, (data[17]&0xFF)==0x02 ? 0 : 1);
            }
            break;
        case TYPE_INPUT_KEY:
            {
                if(0x03 == (data[17]&0xFF)){
                    //system("/system/bin/input keyevent 3");
                }else{
                    inputKey(key_fd, (int16_t)(data[17]&0xFF));
                }
            }
            break;
        case TYPE_INPUT_TEXT:
            {
                std::lock_guard<std::mutex> lock (mlock_event);
                int32_t dLen = (data[4]&0xFF)<<24|(data[5]&0xFF)<<16|(data[6]&0xFF)<<8|(data[7]&0xFF);
                char text[4096] = {0};
                int32_t maxSize = (dLen-8)>4095?4095:(dLen-8);
                memcpy(text,data+16,maxSize);
                const char* kCommand = "/system/bin/input";
                const char* const argv[] = {"text",text};
                execv(kCommand, (char* const*)(argv));
            }
            break;
        }
        {
            std::lock_guard<std::mutex> lock (mlock_event);
            int32_t dLen = (data[4]&0xFF)<<24|(data[5]&0xFF)<<16|(data[6]&0xFF)<<8|(data[7]&0xFF);
            events.erase(events.begin(),events.begin()+(dLen + 8));
        }

    }
    close(key_fd);
    //close(ts_fd);
}

void InputServer::disconnectClient(InputClient* client)
{
    std::lock_guard<std::mutex> lock (mlock_remove);
    remove_clients.push_back(client);
    mcond_remove.notify_one();
}

void InputServer::inputKey(int32_t fd, int16_t key)
{
    input_event _event1;
    memset(&_event1,0,sizeof(_event1));
    _event1.type = EV_KEY;
    _event1.code = key;
    _event1.value = 1;
    int32_t ret = write(fd,&_event1,sizeof(_event1));
    input_event _event2;
    memset(&_event2,0,sizeof(_event2));
    _event2.type = EV_SYN;
    _event2.code = SYN_REPORT;
    _event2.value = 0;
    ret = write(fd,&_event2,sizeof(_event2));
    input_event _event3;
    memset(&_event3,0,sizeof(_event3));
    _event3.type = EV_KEY;
    _event3.code = key;
    _event3.value = 0;
    ret = write(fd,&_event3,sizeof(_event3));
    input_event _event4;
    memset(&_event4,0,sizeof(_event4));
    _event4.type = EV_SYN;
    _event4.code = SYN_REPORT;
    _event4.value = 0;
    ret = write(fd,&_event4,sizeof(_event4));
}

void InputServer::inputTouch(int32_t fd, int16_t x, int16_t y, int16_t action)
{
    input_event _event1;
    memset(&_event1,0,sizeof(_event1));
    _event1.type = EV_ABS;
    _event1.code = ABS_MT_POSITION_X;
    _event1.value = x;
    int32_t ret = write(fd,&_event1,sizeof(_event1));
    input_event _event2;
    memset(&_event2,0,sizeof(_event2));
    _event2.type = EV_ABS;
    _event2.code = ABS_MT_POSITION_Y;
    _event2.value = y;
    ret = write(fd,&_event2,sizeof(_event2));
    input_event _event3;
    memset(&_event3,0,sizeof(_event3));
    _event3.type = EV_ABS;
    _event3.code = ABS_MT_TRACKING_ID;
    _event3.value = 0;
    ret = write(fd,&_event3,sizeof(_event3));
    input_event _event4;
    memset(&_event4,0,sizeof(_event4));
    _event4.type = EV_SYN;
    _event4.code = SYN_MT_REPORT;
    _event4.value = 0;
    ret = write(fd,&_event4,sizeof(_event4));
    input_event _event5;
    memset(&_event5,0,sizeof(_event5));
    _event5.type = EV_KEY;
    _event5.code = BTN_TOUCH;
    _event5.value = action;
    ret = write(fd,&_event5,sizeof(_event5));
    input_event _event6;
    memset(&_event6,0,sizeof(_event6));
    _event6.type = EV_SYN;
    _event6.code = SYN_REPORT;
    _event6.value = 0;
    ret = write(fd,&_event6,sizeof(_event6));
}

void InputServer::runCommand(const char *kCommand, char* const argv[]){
    pid_t pid = fork();
    if (pid >= 0){
        execv(kCommand, argv);
        exit(1);
    }
}


