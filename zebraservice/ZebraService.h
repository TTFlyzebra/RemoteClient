//
// Created by FlyZebra on 2021/10/11 0011.
//

#ifndef ANDROID_ZEBRASERVICE_H
#define ANDROID_ZEBRASERVICE_H

#include <utils/RefBase.h>
#include <binder/IInterface.h>
#include <binder/Parcel.h>

#include <android/hardware/zebra/1.0/IZebra.h>
#include <android/hardware/zebra/1.0/IZebraCallback.h>
#include <hidl/Status.h>
#include <hidl/LegacySupport.h>
#include <hidl/HidlSupport.h>
#include <vector>
#include "ServerManager.h"

using ::android::hardware::Return;
using ::android::hardware::hidl_vec;
using ::android::hardware::zebra::V1_0::IZebra;
using ::android::hardware::zebra::V1_0::IZebraCallback;

namespace android {

class ZebraService : public BBinder, public INotify, IZebraCallback
{
public:
    static int32_t init(ServerManager* manager);
    ZebraService(ServerManager* manager);
    virtual ~ZebraService();
public:
    virtual status_t onTransact(uint32_t, const Parcel&, Parcel*, uint32_t);
    virtual int32_t notify(const char* data, int32_t size);
    virtual Return<void> notifyEvent(const hidl_vec<int8_t>& event);

private:
    ServerManager* mManager;
    sp<IZebra> hwZebra;
};

}//namespace android

#endif //ANDROID_ZEBRASERVICE_H
