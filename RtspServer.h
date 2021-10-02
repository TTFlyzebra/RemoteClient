//
// Created by FlyZebra on 2020/10/22 0022.
//

#ifndef ANDROID_RTSPSERVER_H
#define ANDROID_RTSPSERVER_H

#include <binder/IPCThreadState.h>
#include <media/stagefright/foundation/AHandler.h>
#include <media/stagefright/foundation/AMessage.h>
#include <arpa/inet.h>
#include "ScreenDisplay.h"
#include "AudioEncoder.h"
#include "ServerManager.h"

namespace android {

class RtspServer : public AHandler, public INotify {
public:
    RtspServer(ServerManager* manager);
    ~RtspServer();
    void start();
    void stop();

protected:
    virtual void onMessageReceived(const sp<AMessage> &msg);
public:
    virtual void notify(const char* data, int32_t size);

private:
    enum {
        RTP_TCP,
        RTP_UDP,
    };

    enum {
        S_SETUP,
        S_PLAY,
    };

    static void AppendCommonResponse(AString *response, int32_t cseq);

    void handleStart(const sp<AMessage> &msg);
    void handleClientSocket(const sp<AMessage> &msg);
    void handleSocketRecvData(const sp<AMessage> &msg);
    void handleMediaNotify(const sp<AMessage> &msg);
    void handleClientSocketExit(const sp<AMessage> &msg);

    status_t onOptionsRequest(const char* data, int32_t socket_fd, int32_t cseq);
    status_t onDescribeRequest(const char* data, int32_t socket_fd, int32_t cseq);
    status_t onSetupRequest(const char* data, int32_t socket_fd, int32_t cseq);
    status_t onPlayRequest(const char* data, int32_t socket_fd, int32_t cseq);
    status_t onGetParameterRequest(const char* data, int32_t socket_fd, int32_t cseq);
    status_t onOtherRequest(const char* data, int32_t socket_fd, int32_t cseq);

	static void *_server_socket(void *arg);
	static void *_client_socket(void *arg);
	static void *_rtpudp_socket(void *arg);
	static void *_rtcpudp_socket(void *arg);

    void sendSPSPPS(const     char* data, int32_t size, int64_t ptsUsec);
    void sendVFrame(const     char* data, int32_t size, int64_t ptsUsec);
    void sendAFrame(const     char* data, int32_t size, int64_t ptsUsec);

    struct client_conn {
          int32_t socket;
          int32_t type;
          int32_t rtp_port;
          int32_t rtcp_port;
          struct sockaddr_in addr_in;
          socklen_t addrLen;
          int32_t status;
    };

	Mutex mLock;
	std::vector<int32_t> thread_sockets;
	std::vector<client_conn> conn_sockets;
	std::vector<unsigned char> sps_pps;
	pthread_t init_socket_tid;
	int32_t server_socket;
	int32_t rtp_socket;
    int32_t rtcp_socket;

	sp<ScreenDisplay> mScreenDisplay;
	sp<AudioEncoder> mAudioEncoder;

	int32_t sequencenumber1 = 0;
	//int32_t sequencenumber2 = 0;

	volatile bool is_stop = false;
	volatile bool has_client = false;

    ServerManager* mManager;
    std::mutex mlock_data;
    std::mutex mlock_send;
};

}; // namespace android

#endif //ANDROID_RTSPSERVER_H

