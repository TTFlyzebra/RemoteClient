//
// Created by FlyZebra on 2021/10/02 0016.
//

#ifndef ANDROID_RtspCLIENT_H
#define ANDROID_RtspCLIENT_H

#include <stdint.h>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unistd.h>
#include <string>
#include <arpa/inet.h>
#include "ServerManager.h"
#include "FlyLog.h"

namespace android {


class RtspServer;

class RtspClient:public INotify {
public:
    RtspClient(RtspServer* server, ServerManager* manager, int32_t socket);
    ~RtspClient();

public:
    virtual int32_t notify(const char* data, int32_t size);

private:
    void sendThread();
    void recvThread();
    void handleData();
    void sendData(const char* data, int32_t size);

    void appendCommonResponse(std::string *response, int32_t cseq);
        
    void onOptionsRequest(const char* data, int32_t cseq);
    void onDescribeRequest(const char* data, int32_t cseq);
    void onSetupRequest(const char* data, int32_t cseq);
    void onPlayRequest(const char* data, int32_t cseq);
    void onGetParameterRequest(const char* data, int32_t cseq);
    void onOtherRequest(const char* data, int32_t cseq);

    void sendSPSPPS(const     char* data, int32_t size, int64_t ptsUsec);
    void sendVFrame(const     char* data, int32_t size, int64_t ptsUsec);
    void sendAFrame(const     char* data, int32_t size, int64_t ptsUsec);

private:

    enum {
        RTP_TCP,
        RTP_UDP,
    };

    enum {
        S_SETUP,
        S_PLAY,
    };
        
    RtspServer* mServer;
    ServerManager* mManager;     
    int32_t mSocket;
    volatile bool is_stop;
    volatile bool is_disconnect;
    
    std::thread *send_t;
    std::vector<char> sendBuf;
    std::mutex mlock_send;
    std::condition_variable mcond_send;

    std::thread *recv_t;
    std::thread *hand_t;
    std::vector<char> recvBuf;
    std::mutex mlock_recv;
    std::condition_variable mcond_recv;

    int32_t sequencenumber1 = 0;
	//int32_t sequencenumber2 = 0;

    int32_t conn_type;
    int32_t conn_status;
    int32_t conn_addrLen;
    int32_t conn_rtp_port;
    int32_t conn_rtcp_port;
    struct sockaddr_in conn_addr_in;

    std::mutex mlock_temp;

};

}; // namespace android

#endif //ANDROID_RtspCLIENT_H
