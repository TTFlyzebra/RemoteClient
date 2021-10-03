//
// Created by FlyZebra on 2021/9/15 0015.
//

#include "ServerManager.h"
#include "Config.h"
#include "FlyLog.h"

ServerManager::ServerManager()
:is_stop(false)
{
    FLOGD("%s()\n", __func__);
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
    FLOGD("%s()\n", __func__);
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

void ServerManager::updataSync(const char* data, int32_t size)
{
    std::lock_guard<std::mutex> lock (mlock_list);
    for (std::list<INotify*>::iterator it = notifyList.begin(); it != notifyList.end(); ++it) {
        if(((INotify*)*it)->notify(data, size)==0) {
            break;
        }
    }
}

void ServerManager::updataAsync(const char* data, int32_t size)
{
    std::lock_guard<std::mutex> lock (mlock_data);
    if (dataBuf.size() > TERMINAL_MAX_BUFFER) {
        FLOGE("NOTE::terminalClient send buffer too max, will clean %zu size", dataBuf.size());
        dataBuf.clear();
    }
    dataBuf.insert(dataBuf.end(), data, data + size);
    mcond_data.notify_one();
}

void ServerManager::handleData()
{
    while(!is_stop){
        {
            std::unique_lock<std::mutex> lock (mlock_data);
            while (!is_stop && dataBuf.size() < 20) {
                mcond_data.wait(lock);
            }
            if(is_stop) break;
            //char temp[4096] = {0};
            //for (int32_t i = 0; i < 8; i++) {
            //    sprintf(temp, "%s%02x:", temp, dataBuf[i]);
            //}
            //FLOGE("notify:->%s\n", temp);
            if(((dataBuf[0]&0xFF)!=0xEE)||((dataBuf[1]&0xFF)!=0xAA)){
                FLOGE("handleData bad header[%02x:%02x]\n", dataBuf[0], dataBuf[1]);
                dataBuf.clear();
                continue;
            }
        }
        {
            std::unique_lock<std::mutex> lock (mlock_data);
            int32_t dataLen = dataBuf[6]<<24|dataBuf[7]<<16|dataBuf[8]<<8|dataBuf[9];
            int32_t allLen = dataLen+10;
            while(!is_stop && allLen>dataBuf.size()) {
                mcond_data.wait(lock);
            }
            if(is_stop) break;
            updataSync(&dataBuf[0], allLen);
            dataBuf.erase(dataBuf.begin(),dataBuf.begin()+allLen);
        }
    }
}