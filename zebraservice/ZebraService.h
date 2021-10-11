//
// Created by FlyZebra on 2021/10/11 0011.
//

#ifndef ANDROID_ZEBRASERVICE_H
#define ANDROID_ZEBRASERVICE_H

#include <utils/RefBase.h>
#include <binder/IInterface.h>
#include <binder/Parcel.h>
#include "ServerManager.h"

namespace android {

class ZebraService : public BBinder, public INotify
{
public:
    static int32_t init(ServerManager* manager);
    ZebraService(ServerManager* manager);
    virtual ~ZebraService();

public:
    virtual status_t onTransact(uint32_t, const Parcel&, Parcel*, uint32_t);
    virtual int32_t notify(const char* data, int32_t size);

private:
    ServerManager* mManager;
};

}//namespace android

#endif //ANDROID_ZEBRASERVICE_H