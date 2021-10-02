//
// Created by FlyZebra on 2021/7/30 0030.
//

#ifndef ANDROID_SCREENDISPLAY_H
#define ANDROID_SCREENDISPLAY_H

#include <media/stagefright/foundation/AMessage.h>

namespace android {

class ScreenDisplay : public RefBase {
public:
    ScreenDisplay(sp<AMessage> mNotify);
    ~ScreenDisplay();
    void startRecord();
    void stopRecord();

private:
	static void *_run_record(void *arg);
    bool isRunning = false;
    sp<AMessage> mNotify;
};

}; // namespace android

#endif //ANDROID_SCREENDISPLAY_H
