//
// Created by FlyZebra on 2021/9/15 0015.
//

#ifndef ANDROID_SERVERMANAGER_H
#define ANDROID_SERVERMANAGER_H
#include <stdio.h>
#include <stdint.h>
#include <list>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>


class INotify{
public:
    virtual ~INotify() {};
    virtual void notify(char* data, int32_t size) = 0;
};

class ServerManager {
public:
    ServerManager();
    ~ServerManager();
    void registerListener(INotify* notify);
    void unRegisterListener(INotify* notify);
    void updataSync(char* data, int32_t size);
    void updataAsync(char* data, int32_t size);
    
private:
    void handleData();

private:
    volatile bool is_stop;

    std::list<INotify*> notifyList;
    std::mutex mlock_list;

    std::thread *data_t;
    std::vector<char> dataBuf;
    std::mutex mlock_data;
    std::condition_variable mcond_data;
};

#endif //ANDROID_SERVERMANAGER_H
