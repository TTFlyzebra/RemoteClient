//
// Created by FlyZebra on 2021/9/15 0015.
//

#ifndef ANDROID_SERVERMANAGER_H
#define ANDROID_SERVERMANAGER_H
#include <stdio.h>
#include <stdint.h>
#include <list>
#include <pthread.h>

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
    void update(char* data, int32_t size);

private:
    std::list<INotify*> notifyList;
    pthread_mutex_t mLock;
};

#endif //ANDROID_SERVERMANAGER_H
