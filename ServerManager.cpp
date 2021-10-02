//
// Created by FlyZebra on 2021/9/15 0015.
//

#include "ServerManager.h"
#include "Config.h"

ServerManager::ServerManager()
:is_stop(false)
{
    printf("%s()\n", __func__);
    data_t = new std::thread(&ServerManager::handleData, this);
}

ServerManager::~ServerManager()
{
    is_stop = true;
    {
        std::lock_guard<std::mutex> lock (mlock_data);
        mcond_data.notify_all();
    }
    data_t->join();
    delete data_t;
    printf("%s()\n", __func__);
}

void ServerManager::registerListener(INotify* notify)
{
    std::lock_guard<std::mutex> lock (mlock_list);
    notifyList.push_back(notify);
}

void ServerManager::unRegisterListener(INotify* notify)
{
    std::lock_guard<std::mutex> lock (mlock_list);
    notifyList.remove(notify);
}

void ServerManager::updataSync(char* data, int32_t size)
{
    std::lock_guard<std::mutex> lock (mlock_list);
    for (std::list<INotify*>::iterator it = notifyList.begin(); it != notifyList.end(); ++it) {
        ((INotify*)*it)->notify(data, size);
    }
}

void ServerManager::updataAsync(char* data, int32_t size)
{
    std::lock_guard<std::mutex> lock (mlock_data);
    if (dataBuf.size() > TERMINAL_MAX_BUFFER) {
        printf("NOTE::terminalClient send buffer too max, will clean %zu size", dataBuf.size());
        dataBuf.clear();
    }
    dataBuf.insert(dataBuf.end(), data, data + size);
    mcond_data.notify_one();
}

void ServerManager::handleData()
{
    while(!is_stop){
        std::unique_lock<std::mutex> lock (mlock_data);
        while (!is_stop && dataBuf.empty()) {
            mcond_data.wait(lock);
        }
        if(is_stop) break;
        if(dataBuf.size()<8) continue;
        if(((dataBuf[0]&0xFF)!=0xEE)||((dataBuf[1]&0xFF)!=0xAA)){
            printf("handleData bad header[%02x:%02x]\n", dataBuf[0]&0xFF, dataBuf[1]&0xFF);
            dataBuf.clear();
            continue;
        }
        int32_t dataSize = dataBuf[4]<<24|dataBuf[5]<<16|dataBuf[6]<<8|dataBuf[7];
        if(dataSize+8>dataBuf.size()) {
            printf("handleData size error, dataSize=%d, bufSize=%zu\n", dataSize+8, dataBuf.size());
            continue;
        }
        updataSync(&dataBuf[0], dataSize+8);
        dataBuf.erase(dataBuf.begin(),dataBuf.begin()+dataSize+8);
    }
}