//
// Created by FlyZebra on 2021/9/15 0015.
//

#ifndef ANDROID_SERVERMANAGER_H
#define ANDROID_SERVERMANAGER_H
#include <stdint.h>
#include <list>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>


struct NotifyData {
    int16_t head;
    int16_t version;
    int16_t type;
    int32_t size;
    char*   data;
};

class INotify{
public:
    virtual ~INotify() {};
    virtual int32_t notify(const char* data, int32_t size) = 0;
};

class ServerManager {
public:
    ServerManager();
    ~ServerManager();
    void registerListener(INotify* notify);
    void unRegisterListener(INotify* notify);
    void updataSync(const char* data, int32_t size);
    void updataAsync(const char* data, int32_t size);
    
public:
    std::mutex mlock_up;
    
private:
    void handleData();

private:
    volatile bool is_stop;

    std::mutex mlock_list;
    std::list<INotify*> notifyList;

    std::thread *data_t;
    std::mutex mlock_data;
    std::vector<char> dataBuf;
    std::condition_variable mcond_data;
};

#endif //ANDROID_SERVERMANAGER_H
