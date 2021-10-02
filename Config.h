//
// Created by FlyZebra on 2021/9/2 0002.
//

#ifndef ANDROID_CONFIG_H
#define ANDROID_CONFIG_H

#define AUDIO_MIMETYPE          "audio/mp4a-latm"
#define OUT_SAMPLE_RATE         48000
#define OUT_SAMPLE_FMT          AV_SAMPLE_FMT_S16
#define OUT_CH_LAYOUT           AV_CH_LAYOUT_STEREO
#define RTSP_SERVER_TCP_PORT    8554
#define RTSP_SERVER_UDP_PORT1   9002
#define RTSP_SERVER_UDP_PORT2   9003
#define RTSP_SERVER_UDP_PORT3   9004
#define AUDIO_SERVER_TCP_PORT   9006
#define CONTROLLER_TCP_PORT     9008

#define KEYEVENT_DEV_KEY        "/dev/input/event2"
#define KEYEVENT_DEV_TS         "/dev/input/event2"

#define REMOTE_SERVER_TCP_PORT      9036
#define TERMINAL_SERVER_TCP_PORT    9038
#define TERMINAL_MAX_BUFFER         8388608


#endif //ANDROID_CONFIG_H
