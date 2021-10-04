//
// Created by FlyZebra on 2020/10/22 0022.
//

#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include "RtspServer.h"
#include "FlyLog.h"
#include "Config.h"
#include "Command.h"

using namespace android;

RtspServer::RtspServer(ServerManager* manager)
:mManager(manager)
,is_stop(false)
{
    mManager->registerListener(this);
    server_t = new std::thread(&RtspServer::serverSocket, this);
    rtpudp_t = new std::thread(&RtspServer::rtpudpSocket, this);
    rtcpudp_t = new std::thread(&RtspServer::rtcpudpSocket, this);
    remove_t = new std::thread(&RtspServer::removeClient, this);
}

RtspServer::~RtspServer()
{
    is_stop = true;
    shutdown(rtp_socket, SHUT_RDWR);
    close(rtp_socket);
    shutdown(rtcp_socket, SHUT_RDWR);
    close(rtcp_socket);
    shutdown(server_socket, SHUT_RDWR);
    close(server_socket);
    mManager->unRegisterListener(this);
    {
        std::lock_guard<std::mutex> lock (mlock_server);
        for (std::list<RtspClient*>::iterator it = rtsp_clients.begin(); it != rtsp_clients.end(); ++it) {
            delete ((RtspClient*)*it);
        }
        rtsp_clients.clear();
    }
    {
        std::lock_guard<std::mutex> lock (mlock_remove);
        mcond_remove.notify_one();
    }
    server_t->join();
    rtpudp_t->join();
    rtcpudp_t->join();
    remove_t->join();
    delete server_t;
    delete rtpudp_t;
    delete rtcpudp_t;
    delete remove_t;
    FLOGD("%s()", __func__);
}


int32_t RtspServer::notify(const char* data, int32_t size)
{
    struct NotifyData* notifyData = (struct NotifyData*)data;
    int32_t len = data[6]<<24|data[7]<<16|data[8]<<8|data[9];
    int32_t pts = data[18]<<24|data[19]<<16|data[20]<<8|data[21];
    switch (notifyData->type){
    case 0x0302:
        sps_pps.clear();
        sps_pps.insert(sps_pps.end(), data+22, data+(size-22));
        return -1;
    }
    return -1;
}

void RtspServer::serverSocket()
{
	FLOGD("RtspServer serverSocket start!");
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        FLOGE("serverSocket socket error %s errno: %d", strerror(errno), errno);
        return;
    }
    
    struct sockaddr_in t_sockaddr;
    memset(&t_sockaddr, 0, sizeof(t_sockaddr));
    t_sockaddr.sin_family = AF_INET;
    t_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    t_sockaddr.sin_port = htons(RTSP_SERVER_TCP_PORT);
    int32_t ret = bind(server_socket,(struct sockaddr *) &t_sockaddr,sizeof(t_sockaddr));
    if (ret < 0) {
        FLOGE( "serverSocket bind %d socket error %s errno: %d", RTSP_SERVER_TCP_PORT,strerror(errno), errno);
        return;
    }
    
    ret = listen(server_socket, 5);
    if (ret < 0) {
        FLOGE("serverSocket listen error %s errno: %d", strerror(errno), errno);
        return;
    }
    while(!is_stop) {
        int32_t client_socket = accept(server_socket, (struct sockaddr*)NULL, NULL);
        if(client_socket < 0) {
            FLOGE("accpet socket error: %s errno :%d", strerror(errno), errno);
            continue;
        }
        if(is_stop) break;
        RtspClient *client = new RtspClient(this, mManager, client_socket);
        std::lock_guard<std::mutex> lock (mlock_server);
        rtsp_clients.push_back(client);
    }
    close(server_socket);
    FLOGD("RtspServer serverSocket exit!");
}

void RtspServer::rtpudpSocket()
{
    FLOGD("RtspServer rtpudpSocket start!");
    char recvBuf[1024];
    char temp[4096];
    int32_t addr_len;
    int32_t recvLen;
    struct sockaddr_in addr_in;
    
    rtp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if(rtp_socket < 0) {
       FLOGE("RTCP udp socket error %s errno: %d", strerror(errno), errno);
       return;
    }
    memset(&addr_in, 0, sizeof(struct sockaddr_in));//每个字节都用0填充
    addr_in.sin_family = AF_INET;//使用IPV4地址
    addr_in.sin_port = htons(RTSP_SERVER_UDP_PORT1);//端口
    addr_in.sin_addr.s_addr = htonl(INADDR_ANY);//自动获取IP地址
    int32_t ret = bind(rtp_socket, (struct sockaddr *)&addr_in, sizeof(addr_in));
    if(ret < 0){
        FLOGE( "bind RTCP udp socket error %s errno: %d", strerror(errno), errno);
        return;
    }
    while(!is_stop){
        int32_t recvLen = recvfrom(rtp_socket, recvBuf, 1024, 0, (struct sockaddr *)&addr_in, (socklen_t *)&addr_len);
        if(recvLen > 0){
            memset(temp,0, 4096);
            for (int32_t i = 0; i < recvLen; i++) {
                sprintf(temp, "%s%02x:", temp, recvBuf[i]);
            }
            //FLOGV("rtp_recv:len=[%d],errno=[%d]\n%s", recvLen, errno, temp);
        }else{
            FLOGE("rtp_recv:len=[%d],errno=[%d].", recvLen, errno);
        }
    }
    close(rtp_socket);
    FLOGD("RtspServer rtpudpSocket exit!");
    return;
}

void RtspServer::rtcpudpSocket()
{
    FLOGD("RtspServer rtcpudpSocket start!");
    char recvBuf[1024];
    char temp[4096];
    int32_t addr_len;
    int32_t recvLen;
    struct sockaddr_in addr_in;
    
    rtcp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if(rtcp_socket < 0) {
        FLOGE("RTP udp socket error %s errno: %d", strerror(errno), errno);
        return;
    }
    memset(&addr_in, 0, sizeof(struct sockaddr_in));//每个字节都用0填充
    addr_in.sin_family = AF_INET;//使用IPV4地址
    addr_in.sin_port = htons(RTSP_SERVER_UDP_PORT2);//端口
    addr_in.sin_addr.s_addr = htonl(INADDR_ANY);//自动获取IP地址
    int32_t ret = bind(rtcp_socket, (struct sockaddr *)&addr_in, sizeof(addr_in));
    if(ret < 0){
        FLOGE( "bind RTP udp socket error %s errno: %d", strerror(errno), errno);
        return;
    }
    while(!is_stop){
        int32_t recvLen = recvfrom(rtcp_socket, recvBuf, 1024, 0, (struct sockaddr *)&addr_in, (socklen_t *)&addr_len);
        if(recvLen > 0){
            memset(temp,0, 4096);
            for (int32_t i = 0; i < recvLen; i++) {
                sprintf(temp, "%s%02x:", temp, recvBuf[i]);
            }
            //FLOGV("rtcp_recv:len=[%d],errno=[%d]\n%s", recvLen, errno, temp);
        }else{
            FLOGE("rtcp_recv:len=[%d],errno=[%d].", recvLen, errno);
        }
    }
    close(rtcp_socket);
    FLOGD("RtspServer rtcpudpSocket exit!");
    return;
}

void RtspServer::disconnectClient(RtspClient* client)
{
    std::lock_guard<std::mutex> lock (mlock_remove);
    remove_clients.push_back(client);
    mcond_remove.notify_one();
}

void RtspServer::removeClient()
{
    while(!is_stop){
        {
            std::unique_lock<std::mutex> lock (mlock_remove);
            while (!is_stop && remove_clients.empty()) {
                mcond_remove.wait(lock);
            }
            if(is_stop) break;
            for (std::vector<RtspClient*>::iterator it = remove_clients.begin(); it != remove_clients.end(); ++it) {
                {
                    std::lock_guard<std::mutex> lock (mlock_server);
                    rtsp_clients.remove(((RtspClient*)*it));
                }
                delete ((RtspClient*)*it);
            }
            remove_clients.clear();
        }
        FLOGD("RtspServer::removeClient rtsp_clients.size=%zu", rtsp_clients.size());
    }
}

