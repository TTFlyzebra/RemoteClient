//
// Created by FlyZebra on 2021/9/2 0002.
//

#ifndef ANDROID_CONFIG_H
#define ANDROID_CONFIG_H

#define AUDIO_MIMETYPE                      "audio/mp4a-latm"
#define OUT_SAMPLE_RATE                     44100
#define OUT_SAMPLE_FMT                      AV_SAMPLE_FMT_S16
#define OUT_CH_LAYOUT                       AV_CH_LAYOUT_STEREO
#define RTSP_SERVER_TCP_PORT                8554
#define RTSP_SERVER_UDP_PORT1               9002
#define RTSP_SERVER_UDP_PORT2               9003
#define RTSP_SERVER_UDP_PORT3               9004

#define AUDIO_SERVER_TCP_PORT               9006
#define INPUT_SERVER_TCP_PORT               9008


#define KEYEVENT_DEV_KEY                    "/dev/input/event0"
#define KEYEVENT_DEV_TS                     "/dev/input/event0"

#define REMOTEPC_SERVER_IP                  "192.168.1.88"
#define REMOTEPC_SERVER_TCP_PORT            9036
#define TERMINAL_SERVER_TCP_PORT            9038
#define TERMINAL_MAX_BUFFER                 8388608

#define AUDIO_SERVER_IP                    "127.0.0.1"
#define AUDIO_SERVER_PORT                  "18183"
#define AUDIO_PROP_IP                      "persist.sys.audio.serverip"
#define AUDIO_PROP_PROT                    "persist.sys.audio.serverport"




#endif //ANDROID_CONFIG_H
