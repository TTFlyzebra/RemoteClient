//
// Created by FlyZebra on 2020/7/20 0020.
//

#ifndef ANDROID_FLYLOG_H
#define ANDROID_FLYLOG_H

#include <utils/Log.h>

#define TAG "ZEBRA-MCTL"

#define FLOGD(...) __android_log_print(ANDROID_LOG_DEBUG,TAG ,__VA_ARGS__)
#define FLOGI(...) __android_log_print(ANDROID_LOG_INFO,TAG ,__VA_ARGS__)
#define FLOGW(...) __android_log_print(ANDROID_LOG_WARN,TAG ,__VA_ARGS__)
#define FLOGE(...) __android_log_print(ANDROID_LOG_ERROR,TAG ,__VA_ARGS__)

#endif //ANDROID_FLYLOG_H
