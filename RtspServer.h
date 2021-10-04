//
// Created by FlyZebra on 2020/10/22 0022.
//

#ifndef ANDROID_RTSPSERVER_H
#define ANDROID_RTSPSERVER_H

#include "ServerManager.h"
#include "RtspClient.h"

class RtspServer : public INotify {
public:
    RtspServer(ServerManager* manager);
    ~RtspServer();
    void disconnectClient(RtspClient *client);
    
public:
    virtual int32_t notify(const char* data, int32_t size);

private:
	void serverSocket();
	void rtpudpSocket();
	void rtcpudpSocket();
    void removeClient();

public:
    int32_t rtp_socket;
    int32_t rtcp_socket;
    std::vector<char> sps_pps;

private:

    ServerManager* mManager;
	volatile bool is_stop = false;
    
    int32_t server_socket;

    std::thread *server_t;
    std::thread *rtpudp_t;
    std::thread *rtcpudp_t;

    std::list<RtspClient*> rtsp_clients;
    std::mutex mlock_server;

    std::thread *remove_t;
    std::vector<RtspClient*> remove_clients;
    std::mutex mlock_remove;
    std::condition_variable mcond_remove;
};

#endif //ANDROID_RTSPSERVER_H

