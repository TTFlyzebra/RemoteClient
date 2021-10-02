//
// Created by FlyZebra on 2021/9/15 0015.
//

#include "ServerManager.h"
#include "FlyLog.h"

ServerManager::ServerManager()
:is_stop(false)
{
    printf("%s()\n", __func__);
    data_t = new std::thread(&ServerManager::handleData, this);
}

ServerManager::~ServerManager()
{
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

void ServerManager::handleData()
{

}