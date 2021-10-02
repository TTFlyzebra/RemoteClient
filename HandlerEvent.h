//
// Created by FlyZebra on 2021/8/3 0003.
//

#ifndef ANDROID_HANDLEREVENT_H
#define ANDROID_HANDLEREVENT_H

enum {
    kWhatStart,
    kWhatStop,
	kWhatClientSocket,
	kWhatUpClientStatus,
	kWhatClientSocketExit,
	kWhatSocketRecvData,
	kWhatSPSPPSData,
	kWhatVideoFrameData,
	kWhatAudioFrameData,
	kWhatMediaNotify,
};

#endif //ANDROID_HANDLEREVENT_H
