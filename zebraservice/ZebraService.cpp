////
//// Created by FlyZebra on 2021/10/11 0011.
////
//
//#include <binder/IServiceManager.h>
//#include <binder/IPCThreadState.h>
//
//#include "ZebraService.h"
//#include "FlyLog.h"
//
//namespace android {
//
//int32_t ZebraService::init(ServerManager* manager)
//{
//    return defaultServiceManager()->addService(String16("zebra.svc"), new ZebraService(manager));
//}
//
//ZebraService::ZebraService(ServerManager* manager)
//: mManager(manager)
//, hwZebra(IZebra::getService())
//{
//    FLOGD("%s",__func__);
//    mManager->registerListener(this);
//    if(hwZebra==nullptr){
//        FLOGE("Get hw zebra service error!");
//    }else{
//        hwZebra->helloWorld("Zebra", [&](hidl_string result){
//            FLOGE("IZebra helloWorld %s", result.c_str());
//        });
//    }
//}
//
//ZebraService::~ZebraService()
//{
//    mManager->unRegisterListener(this);
//    FLOGD("%s",__func__);
//}
//
//int32_t ZebraService::notify(const char* data, int32_t size)
//{
//    return 0;
//}
//
//status_t ZebraService::onTransact(uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
//{
//    switch(code) {
//    case 0:
//        {
//            pid_t pid = data.readInt32();
//            int num = data.readInt32();
//            num += 1000;
//            reply->writeInt32(num);
//            return NO_ERROR;
//        } break;
//	case 1:
//        {
//            pid_t pid = data.readInt32();
//            String8 str = data.readString8();
//			String8 add_str = String8("zebra_service get ")+str;
//            reply->writeString8(add_str);
//            return NO_ERROR;
//        } break;
//    default:
//        return BBinder::onTransact(code, data, reply, flags);
//    }
//}
//
//}

