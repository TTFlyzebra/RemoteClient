//
// Created by FlyZebra on 2020/10/22 0022.
//
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <string>
#include <error.h>
#include <unistd.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include "RtspServer.h"
#include "FlyLog.h"
#include "Base64.h"
#include "HandlerEvent.h"
#include "Config.h"
#include "Command.h"

using namespace android;

RtspServer::RtspServer(ServerManager* manager)
:mManager(manager)
{
    mManager->registerListener(this);
}

RtspServer::~RtspServer()
{
    mManager->unRegisterListener(this);
}


void RtspServer::notify(const char* data, int32_t size)
{
    char temp[4096] = {0};
    memset(temp,0,4096);
    for (int32_t i = 0; i < 10; i++) {
        sprintf(temp, "%s%02x:", temp, data[i]);
    }
    printf("RtspServer->notify->%s[%d]\n", temp, size);
}

void RtspServer::onMessageReceived(const sp<AMessage> &msg)
{
    switch (msg->what()) {
        case kWhatStart:            
            handleStart(msg);
       		break;
		case kWhatClientSocket:
            handleClientSocket(msg);
			break;
		case kWhatClientSocketExit:
		    handleClientSocketExit(msg);
		    break;
		case kWhatSocketRecvData:
			handleSocketRecvData(msg);
			break;
		case kWhatMediaNotify:
			handleMediaNotify(msg);
		    break;
    }
}


void RtspServer::AppendCommonResponse(AString *response, int32_t cseq)
{
    char temp[16];
    sprintf(temp, "CSeq: %d\r\n",cseq);
    response->append(temp);
    response->append("User-Agent: Android screen rtsp(author zebra)\r\n");
    time_t now = time(NULL);
    struct tm *now2 = gmtime(&now);
    char buf[128];
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S %z", now2);
    response->append("Date: ");
    response->append(buf);
    response->append("\r\n");
}

void RtspServer::start()
{
    FLOGD("RtspServer::start()");
    is_stop = false;

    sp<AMessage> notify = new AMessage(kWhatMediaNotify, this);

    mAudioEncoder = new AudioEncoder(notify);
    mAudioEncoder->start();
    mScreenDisplay = new ScreenDisplay(notify);

    sp<AMessage> msg = new AMessage(kWhatStart, this);
    msg->post();
}

void RtspServer::stop()
{
    mAudioEncoder->stop();
    mScreenDisplay->stopRecord();
    is_stop = true;
    close(rtp_socket);
    close(rtcp_socket);
    if(server_socket >= 0){
        close(server_socket);
        server_socket = -1;
        //try connect once for exit accept block
        int32_t socket_temp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in servaddr;
        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(RTSP_SERVER_TCP_PORT);
        servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        connect(socket_temp, (struct sockaddr *) &servaddr, sizeof(servaddr));
        close(socket_temp);
    }else{
       close(server_socket);
       server_socket = -1;
    }
    pthread_join(init_socket_tid, nullptr);
    FLOGD("RtspServer::stop()");
}

void *RtspServer::_server_socket(void *argv)
{
	FLOGD("RtspServer server_socket start!");
	auto *p=(RtspServer *)argv;
	//Bind TCP SOCKET
    struct sockaddr_in t_sockaddr;
    memset(&t_sockaddr, 0, sizeof(t_sockaddr));
    t_sockaddr.sin_family = AF_INET;
    t_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    t_sockaddr.sin_port = htons(RTSP_SERVER_TCP_PORT);

    p->server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (p->server_socket < 0) {
        FLOGE("socket error %s errno: %d", strerror(errno), errno);
        return 0;
    }

    int32_t ret = bind(p->server_socket,(struct sockaddr *) &t_sockaddr,sizeof(t_sockaddr));
    if (ret < 0) {
        FLOGE( "bind %d socket error %s errno: %d", RTSP_SERVER_TCP_PORT,strerror(errno), errno);
        return 0;
    }
    ret = listen(p->server_socket, 5);
    if (ret < 0) {
        FLOGE("listen error %s errno: %d", strerror(errno), errno);
    }
    while(!p->is_stop) {
        int32_t client_socket = accept(p->server_socket, (struct sockaddr*)NULL, NULL);
        if(client_socket < 0) {
            FLOGE("accpet socket error: %s errno :%d", strerror(errno), errno);
            continue;
        }
        if(p->is_stop) break;
		sp<AMessage> msg = new AMessage(kWhatClientSocket, (RtspServer *) argv);
		msg->setInt32("socket", client_socket);
		msg->post();
    }
    close(p->server_socket);
    p->server_socket = -1;
    FLOGD("RtspServer server_socket exit!");
	return 0;
}

void *RtspServer::_client_socket(void *argv)
{
    FLOGD("RtspServer client_socket start!");
    signal(SIGPIPE, SIG_IGN);
    auto *p=(RtspServer *)argv;
    int32_t socket_fd;
    {
	    Mutex::Autolock autoLock(p->mLock);
	    socket_fd = p->thread_sockets.back();
	    p->thread_sockets.pop_back();
	}
	char recvBuf[1024];
    int32_t recvLen;
	while(!p->is_stop){
	    memset(recvBuf,0,1024);
	    recvLen = recv(socket_fd, recvBuf, 1024, 0);
	    FLOGD("RtspServer recv:len=[%d], errno=[%d]\n%s", recvLen, errno, recvBuf);
        if (recvLen <= 0) {
            //if(errno == 0 || errno == 11) continue;
            sp<AMessage> msg = new AMessage(kWhatClientSocketExit, (RtspServer *) argv);
            msg->setInt32("socket", socket_fd);
            //msg->post();
            //close(socket_fd);
            p->handleClientSocketExit(msg);
            break;
        }else {
            sp<ABuffer> data = ABuffer::CreateAsCopy(recvBuf, recvLen);
            sp<AMessage> msg = new AMessage(kWhatSocketRecvData, (RtspServer *) argv);
            msg->setBuffer("data", data);
            msg->setInt32("socket", socket_fd);
            msg->post();
        }
	}
	FLOGD("RtspServer client_socket exit!");
	return 0;
}

void *RtspServer::_rtpudp_socket(void *argv)
{
    FLOGD("RtspServer rtp_socket start!");
    signal(SIGPIPE, SIG_IGN);
    auto *p=(RtspServer *)argv;
    int32_t socket_fd = p->rtp_socket;
    char recvBuf[1024];
    char temp[4096];
    int32_t addr_len;
    int32_t recvLen;
    struct sockaddr_in addr_client;
    while(!p->is_stop){
        memset(recvBuf,0, 1024);
        int32_t recvLen = recvfrom(socket_fd, recvBuf, 1024, 0, (struct sockaddr *)&addr_client, (socklen_t *)&addr_len);
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
    FLOGD("RtspServer rtp_socket exit!");
    return 0;
}

void *RtspServer::_rtcpudp_socket(void *argv)
{
    FLOGD("RtspServer rtcp_socket start!");
    signal(SIGPIPE, SIG_IGN);
    auto *p=(RtspServer *)argv;
    int32_t socket_fd = p->rtcp_socket;
    char recvBuf[1024];
    char temp[4096];
    int32_t addr_len;
    int32_t recvLen;
    struct sockaddr_in addr_client;
    while(!p->is_stop){
        memset(recvBuf, 0, 1024);
        int32_t recvLen = recvfrom(socket_fd, recvBuf, 1024, 0, (struct sockaddr *)&addr_client, (socklen_t *)&addr_len);
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
    FLOGD("RtspServer rtcp_socket exit!");
    return 0;
}

void RtspServer::handleStart(const sp<AMessage> &msg)
{
    int32_t ret;
    //Bind UDP 1 SOCKET

    rtp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if(rtp_socket < 0) {
       FLOGE("RTP udp socket error %s errno: %d", strerror(errno), errno);
       exit(-1);
    }
    struct sockaddr_in addr_serv1;
    memset(&addr_serv1, 0, sizeof(struct sockaddr_in));//每个字节都用0填充
    addr_serv1.sin_family = AF_INET;//使用IPV4地址
    addr_serv1.sin_port = htons(RTSP_SERVER_UDP_PORT1);//端口
    addr_serv1.sin_addr.s_addr = htonl(INADDR_ANY);//自动获取IP地址
    ret = bind(rtp_socket, (struct sockaddr *)&addr_serv1, sizeof(addr_serv1));
    if(ret < 0){
      FLOGE( "bind RTP udp socket error %s errno: %d", strerror(errno), errno);
      exit(-1);
    }
    pthread_t rtpudp_socket_tid;
    ret = pthread_create(&rtpudp_socket_tid, nullptr, _rtpudp_socket, (void *) this);
    if (ret != 0) {
    	FLOGE("create RTP udp socket thread error!");
    	exit(-1);
    }

    //Bind UDP 2 SOCKET
    rtcp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if(rtcp_socket < 0) {
       FLOGE("RTCP udp socket error %s errno: %d", strerror(errno), errno);
       exit(-1);
    }
    struct sockaddr_in addr_serv2;
    memset(&addr_serv2, 0, sizeof(struct sockaddr_in));//每个字节都用0填充
    addr_serv2.sin_family = AF_INET;//使用IPV4地址
    addr_serv2.sin_port = htons(RTSP_SERVER_UDP_PORT2);//端口
    addr_serv2.sin_addr.s_addr = htonl(INADDR_ANY);//自动获取IP地址
    ret = bind(rtcp_socket, (struct sockaddr *)&addr_serv2, sizeof(addr_serv2));
    if(ret < 0){
      FLOGE( "bind RTCP udp socket error %s errno: %d", strerror(errno), errno);
      exit(-1);
    }
    pthread_t rtcpudp_socket_tid;
    ret = pthread_create(&rtcpudp_socket_tid, nullptr, _rtcpudp_socket, (void *) this);
    if (ret != 0) {
    	FLOGE("create RTCP udp socket thread error!");
    	exit(-1);
    }

    //TCP 554 socket
    ret = pthread_create(&init_socket_tid, nullptr, _server_socket, (void *) this);
    if (ret != 0) {
    	FLOGE("create rtsp socket thread error!");
    	exit(-1);
    }
}

void RtspServer::handleClientSocket(const sp<AMessage> &msg)
{
    int32_t socket_fd;
    CHECK(msg->findInt32("socket", &socket_fd));
    Mutex::Autolock autoLock(mLock);
    thread_sockets.push_back(socket_fd);
    pthread_t client_socket_tid;
    int32_t ret = pthread_create(&client_socket_tid, nullptr, _client_socket, (void *)this);
    pthread_detach(client_socket_tid);
    if (ret != 0) {
    	FLOGE("RtspServer create client socket thread error!");
    	thread_sockets.pop_back();
    }
}

void RtspServer::handleSocketRecvData(const sp<AMessage> &msg)
{
    int32_t socket_fd;
    CHECK(msg->findInt32("socket", &socket_fd));
	sp<ABuffer> data;
    CHECK(msg->findBuffer("data", &data));
    char action[64] = {0};
    char url[512] = {0};
    char ver[64] = {0};
    if(sscanf((const char*)data->data(), "%s %s %s\r\n", action, url, ver) == 3) {
        int32_t cseq;
    	sscanf(strstr((const char*)data->data(), "CSeq"), "CSeq: %d", &cseq);
        std::string method(action);
        if (method == "OPTIONS") {
            onOptionsRequest((const char*)data->data(),socket_fd, cseq);
        }else if(method == "DESCRIBE"){
            onDescribeRequest((const char*)data->data(),socket_fd, cseq);
        }else if(method == "SETUP"){
            onSetupRequest((const char*)data->data(),socket_fd, cseq);
        }else if(method == "PLAY"){
            onPlayRequest((const char*)data->data(),socket_fd, cseq);
        }else if(method == "GET_PARAMETER"){
            onGetParameterRequest((const char*)data->data(),socket_fd, cseq);
        }
    }else{
        onOtherRequest((const char*)data->data(),socket_fd, -1);
    }
}

void RtspServer::handleMediaNotify(const sp<AMessage> &msg)
{
    int32_t type;
    CHECK(msg->findInt32("type", &type));
    switch (type) {
        case kWhatSPSPPSData:
            {
                sp<ABuffer> data;
                CHECK(msg->findBuffer("data", &data));
                sps_pps.clear();
                sps_pps.insert(sps_pps.end(), data->data(), data->data()+data->capacity());
                sendSPSPPS((const  char*)&sps_pps[0], sps_pps.size(), 0);
            }
            break;
        case kWhatVideoFrameData:
            {
                sp<ABuffer> data;
                CHECK(msg->findBuffer("data", &data));
                int64_t ptsUsec;
                CHECK(msg->findInt64("ptsUsec", &ptsUsec));
                sendVFrame((const char*)data->data(), data->capacity(), ptsUsec);
            }
            break;
        case kWhatAudioFrameData:
            {
                sp<ABuffer> data;
                CHECK(msg->findBuffer("data", &data));
                int64_t ptsUsec;
                CHECK(msg->findInt64("ptsUsec", &ptsUsec));
                sendAFrame((const char*)data->data(), data->capacity(), ptsUsec);
            }
            break;
    }
}

void RtspServer::handleClientSocketExit(const sp<AMessage> &msg)
{
    int32_t socket_fd;
    CHECK(msg->findInt32("socket", &socket_fd));
    int32_t size = conn_sockets.empty()?0:((int)conn_sockets.size());
    for(int32_t i=0;i<size;i++){
        if(conn_sockets[i].socket == socket_fd){
            conn_sockets.erase(conn_sockets.begin()+i);
            break;
        }
    }
    FLOGD("RtspServer conn_sockets size=%d.", conn_sockets.empty()?0:((int)conn_sockets.size()));
    if(conn_sockets.empty()) {
        has_client = false;
        mScreenDisplay->stopRecord();
        mAudioEncoder->stopRecord();
    }
}

status_t RtspServer::onOptionsRequest(const char* data, int32_t socket_fd, int32_t cseq)
{
    AString response = "RTSP/1.0 200 OK\r\n";
    AppendCommonResponse(&response, cseq);
    response.append("Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER, SET_PARAMETER\r\n");
    response.append("\r\n");
    send(socket_fd,response.c_str(),response.size(),0);
    FLOGD("RtspServer send:len=[%d],errno=[%d]\n%s",(int)response.size(), errno, response.c_str());
    return 0;
}

status_t RtspServer::onDescribeRequest(const char* data, int32_t socket_fd, int32_t cseq)
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    getsockname(socket_fd, (struct sockaddr *)&addr, &addrlen);
    AString response = "RTSP/1.0 200 OK\r\n";
    AppendCommonResponse(&response, cseq);
    AString spd;
    spd.append("v=0\r\n");
    char temp[128];
    sprintf(temp, "o=- 1627453750119587 1 in IP4 %s\r\n",inet_ntoa(addr.sin_addr));
    spd.append(temp);
    spd.append("t=0 0\r\n");
    spd.append("a=contol:*\r\n");

    spd.append("m=video 0 RTP/AVP 96\r\n");
    spd.append("a=rtpmap:96 H264/90000\r\n");
    //spd.append("a=fmtp:96 profile-level-id=1;packetization-mode=1;sprop-parameter-sets=Z0KAFtoGQW/llIKDAwNoUJqA,aM4G4g==\r\n");
    spd.append("a=control:track1\r\n");

    //spd.append("m=audio 0 RTP/AVP 98\r\n");
    //spd.append("a=rtpmap:98 L16/16000/2\r\n");
    //1190(48000-2) 1210(44100-2) 1410(16000-2)
    spd.append("m=audio 0 RTP/AVP 97\r\n");
    switch (OUT_SAMPLE_RATE){
        case 48000:
            spd.append("a=rtpmap:97 mpeg4-generic/48000/2\r\n");
            spd.append("a=fmtp:97 streamtype=5;profile-level-id=1;mode=AAC-hbr;");
            spd.append("sizelength=13;indexlength=3;indexdeltalength=3;config=1190\r\n");
            break;
        case 44100:
            spd.append("a=rtpmap:97 mpeg4-generic/44100/2\r\n");
            spd.append("a=fmtp:97 streamtype=5;profile-level-id=1;mode=AAC-hbr;");
            spd.append("sizelength=13;indexlength=3;indexdeltalength=3;config=1210\r\n");
            break;
        default:
            spd.append("a=rtpmap:97 mpeg4-generic/16000/2\r\n");
            spd.append("a=fmtp:97 streamtype=5;profile-level-id=1;mode=AAC-hbr;");
            spd.append("sizelength=13;indexlength=3;indexdeltalength=3;config=1410\r\n");
            break;
    }
    spd.append("a=control:track2\r\n\r\n");

    memset(temp,0,strlen(temp));
    sprintf(temp, "Content-Length: %d\r\n",(int)spd.size());
    response.append(temp);
    response.append("Content-Type: application/sdp\r\n");
    response.append("\r\n");
    response.append(spd.c_str());
    send(socket_fd,response.c_str(),response.size(),0);
    FLOGD("RtspServer send:len=[%d],errno=[%d]\n%s",(int)response.size(), errno, response.c_str());
    return 0;
}

status_t RtspServer::onSetupRequest(const char* data, int32_t socket_fd, int32_t cseq)
{
    struct client_conn conn;
    conn.socket = socket_fd;
    AString response = "RTSP/1.0 200 OK\r\n";
    AppendCommonResponse(&response, cseq);
    int32_t track1 = 0;
    int32_t track2 = 1;
    const char *temp0 = strstr((const char*)data, "interleaved=");
    if(temp0!=nullptr){
        sscanf(temp0, "interleaved=%d-%d", &track1, &track2);
    }
    if (strncmp(strstr((const char*)data, "RTP/AVP"), "RTP/AVP/TCP", 11) == 0) {
        conn.type = RTP_TCP;
        char temp1[128];
        sprintf(temp1, "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n", track1, track2);
        response.append(temp1);
    }else
    /*if (strncmp(strstr((const char*)data, "RTP/AVP"), "RTP/AVP/UDP", 11) == 0)*/
    {
        conn.type = RTP_UDP;
        const char *temp1 = strstr((const char*)data, "client_port=");
        if(temp1!=nullptr){
            sscanf(temp1, "client_port=%d-%d", &conn.rtp_port, &conn.rtcp_port);
        }
        conn.addrLen = sizeof(conn.addr_in);
        getpeername(socket_fd, (struct sockaddr *)&conn.addr_in, (socklen_t*)&conn.addrLen);
        conn.addr_in.sin_port = htons(conn.rtp_port);
        char temp2[128];
        sprintf(temp2, "Transport: RTP/AVP/UDP;unicast;client_port=%d-%d;server_port=%d-%d;interleaved=%d-%d\r\n", \
                conn.rtp_port,RTSP_SERVER_UDP_PORT1,RTSP_SERVER_UDP_PORT2, conn.rtcp_port, track1, track2);
        response.append(temp2);
    }
    char temp[128];
    sprintf(temp, "Session: %d\r\n",socket_fd);
    response.append(temp);
    response.append("\r\n");
    conn.status = S_SETUP;
    //不能重复添加
    int32_t size = conn_sockets.empty()?0:((int)conn_sockets.size());
    for(int32_t i=0;i<size;i++){
        if(conn_sockets[i].socket == socket_fd){
            conn_sockets.erase(conn_sockets.begin()+i);
            break;
        }
    }
    conn_sockets.push_back(conn);
    send(socket_fd,response.c_str(),response.size(),0);
    FLOGD("RtspServer send:len=[%d],errno=[%d]\n%s",(int)response.size(), errno, response.c_str());
    return 0;
}

status_t RtspServer::onPlayRequest(const char* data, int32_t socket_fd, int32_t cseq)
{
    int32_t size = conn_sockets.empty()?0:((int)conn_sockets.size());
    for(int32_t i=0;i<size;i++){
        if(conn_sockets[i].socket == socket_fd){
            conn_sockets[i].status = S_PLAY;
            break;
        }
    }
    AString response = "RTSP/1.0 200 OK\r\n";
    AppendCommonResponse(&response, cseq);
    response.append("Range: npt=0.000-\r\n");
    char temp[128];
    sprintf(temp, "Session: %d\r\n",socket_fd);
    response.append(temp);
    response.append("\r\n");
    send(socket_fd,response.c_str(),response.size(),0);
    FLOGD("RtspServer send:len=[%d],errno=[%d]\n%s",(int)response.size(), errno, response.c_str());
    FLOGD("RtspServer conn_sockets size=%d.", conn_sockets.empty()?0:((int)conn_sockets.size()));
    has_client = true;
    mScreenDisplay->stopRecord();
    mScreenDisplay->startRecord();
    mAudioEncoder->startRecord();
    return 0;
}

status_t RtspServer::onGetParameterRequest(const char* data, int32_t socket_fd, int32_t cseq)
{
    AString response = "RTSP/1.0 200 OK\r\n";
    AppendCommonResponse(&response, cseq);
    response.append("\r\n");
    send(socket_fd,response.c_str(),response.size(),0);
    FLOGD("RtspServer send:len=[%d],errno=[%d]\n%s",(int)response.size(), errno, response.c_str());
    return 0;
}

status_t RtspServer::onOtherRequest(const char* data, int32_t socket_fd, int32_t cseq)
{
    AString response = "RTSP/1.0 200 OK\r\n";
    AppendCommonResponse(&response, cseq);
    response.append("\r\n");
    send(socket_fd,response.c_str(),response.size(),0);
    FLOGE("send:len=[%d],errno=[%d]\n%s",(int)response.size(), errno, response.c_str());
    return 0;
}

void RtspServer::sendSPSPPS(const     char* sps_pps, int32_t size, int64_t ptsUsec)
{
    if(size<=0 || is_stop) return;
    char head_video[sizeof(videodata)];
    memcpy(head_video, videodata, sizeof(videodata));
    int32_t dataLen = size + 10;
    head_video[4]  = (dataLen & 0xFF000000) >> 24;
    head_video[5]  = (dataLen & 0xFF0000) >> 16;
    head_video[6] =  (dataLen & 0xFF00) >> 8;
    head_video[7] =   dataLen & 0xFF;
    {
        std::lock_guard<std::mutex> lock (mlock_data);
        mManager->updataAsync(head_video, sizeof(videodata));
        mManager->updataAsync(sps_pps, size);
    }
        
    int32_t vsize = conn_sockets.size();
    if(vsize<=0) return;
    sequencenumber1++;
    char rtp_pack[16];
    rtp_pack[0] = '$';
    rtp_pack[1] = 0x00;
    rtp_pack[2] = ((size+12) & 0xFF00 ) >> 8;
    rtp_pack[3] = (size+12) & 0xFF;
    rtp_pack[4] = 0x80;
    rtp_pack[5] = 0x60;
    rtp_pack[6] = (sequencenumber1 & 0xFF00) >> 8;
    rtp_pack[7] = sequencenumber1 & 0xFF;
    rtp_pack[8]  = (ptsUsec & 0xFF000000) >> 24;
    rtp_pack[9]  = (ptsUsec & 0xFF0000) >> 16;
    rtp_pack[10] = (ptsUsec & 0xFF00) >> 8;
    rtp_pack[11] =  ptsUsec & 0xFF;
    int32_t sendLen;    
    for(int32_t i=0; i<vsize; i++){
        if(!has_client || is_stop) break;
        if(conn_sockets[i].type==RTP_TCP){
            std::lock_guard<std::mutex> lock (mlock_send);
            sendLen =  send(conn_sockets[i].socket, rtp_pack, 16, 0);
            sendLen += send(conn_sockets[i].socket, sps_pps, size, 0);
        }else{
            char udp_pack[16 + size];
            memcpy(udp_pack, rtp_pack, 16);
            memcpy(udp_pack+16, sps_pps, size);
            sendLen = sendto(rtp_socket, rtp_pack+4, 12+size, 0, (struct sockaddr*)&conn_sockets[i].addr_in, conn_sockets[i].addrLen);
        }
        if(sendLen <= 0) {
            FLOGE("SEND SPS_[%d][%d] errno=%d!", sendLen, 12+size, errno);
            if(errno==0 || errno == 11) continue;
            sp<AMessage> msg = new AMessage(kWhatClientSocketExit, this);
            msg->setInt32("socket", conn_sockets[i].socket);
            //msg->post();
            handleClientSocketExit(msg);
            return;
        }
    }
}

void RtspServer::sendVFrame(const     char* video, int32_t size, int64_t ptsUsec)
{
    if(size<=0 || is_stop) return;
    char head_video[sizeof(videodata)];
    memcpy(head_video, videodata, sizeof(videodata));
    int32_t dataLen = size + 10;
    head_video[4]  = (dataLen & 0xFF000000) >> 24;
    head_video[5]  = (dataLen & 0xFF0000) >> 16;
    head_video[6] =  (dataLen & 0xFF00) >> 8;
    head_video[7] =   dataLen & 0xFF;
    {
        std::lock_guard<std::mutex> lock (mlock_data);
        mManager->updataAsync(head_video, sizeof(videodata));
        mManager->updataAsync(video, size);
    }
    
    int32_t vsize = conn_sockets.size();
    if(vsize<=0) return;
    unsigned char nalu = video[0];
    if((nalu&0x1F)==5){
        sendSPSPPS((const  char*)&sps_pps[0], sps_pps.size(), ptsUsec);
    }
    int32_t fau_num = 1280 - 18;
    if(size <= fau_num){
        sequencenumber1++;
        char rtp_pack[16];
        rtp_pack[0]  = '$';
        rtp_pack[1]  = 0x00;
        rtp_pack[2]  = ((size+12) & 0xFF00 ) >> 8;
        rtp_pack[3]  = (size+12) & 0xFF;
        rtp_pack[4]  = 0x80;
        rtp_pack[5]  = 0x60;
        rtp_pack[6]  = (sequencenumber1 & 0xFF00) >> 8;
        rtp_pack[7]  = sequencenumber1 & 0xFF;
        rtp_pack[8]  = (ptsUsec & 0xFF000000) >> 24;
        rtp_pack[9]  = (ptsUsec & 0xFF0000) >> 16;
        rtp_pack[10] = (ptsUsec & 0xFF00) >> 8;
        rtp_pack[11] =  ptsUsec & 0xFF;
        int32_t sendLen;
        for(int32_t i=0; i<vsize; i++){
            if(!has_client || is_stop) break;
            if(conn_sockets[i].type==RTP_TCP){
                std::lock_guard<std::mutex> lock (mlock_send);
                sendLen =  send(conn_sockets[i].socket,rtp_pack,16,0);
                sendLen += send(conn_sockets[i].socket,video,size,0);
            }else{
                char udp_pack[16 + size];
                memcpy(udp_pack, rtp_pack, 16);
                memcpy(udp_pack+16, video, size);
                sendLen =  sendto(rtp_socket, udp_pack+4, 12+size, 0, (struct sockaddr*)&conn_sockets[i].addr_in, conn_sockets[i].addrLen);
            }
            if(sendLen <= 0) {
                FLOGE("SEND FULL[%d][%d] errno=%d!", sendLen,12+size, errno);
                if(errno==0 || errno == 11) continue;
                sp<AMessage> msg = new AMessage(kWhatClientSocketExit, this);
                msg->setInt32("socket", conn_sockets[i].socket);
                //msg->post();
                handleClientSocketExit(msg);
                return;
            }
        }
    } else {
        int32_t num = 0;
        while((size-1-num*fau_num)>0){
            bool first = (num==0);
            bool last = ((size -1 - num * fau_num)<=fau_num);
            int32_t rtpsize = last?(size -1 - num * fau_num) : fau_num;
            sequencenumber1++;
            char rtp_pack[18];
            rtp_pack[0]  = '$';
            rtp_pack[1]  = 0x00;
            rtp_pack[2]  = ((rtpsize+14) & 0xFF00 ) >> 8;
            rtp_pack[3]  = (rtpsize+14) & 0xFF;
            rtp_pack[4]  = 0x80;
            rtp_pack[5]  = 0x60;
            rtp_pack[6]  = (sequencenumber1 & 0xFF00) >> 8;
            rtp_pack[7]  = sequencenumber1 & 0xFF;
            rtp_pack[8]  = (ptsUsec & 0xFF000000) >> 24;
            rtp_pack[9]  = (ptsUsec & 0xFF0000) >> 16;
            rtp_pack[10] = (ptsUsec & 0xFF00) >> 8;
            rtp_pack[11] =  ptsUsec & 0xFF;
            rtp_pack[16] =  (nalu&0xE0)|0x1C;
            rtp_pack[17] =  first?(0x80|(nalu&0x1F)):(last?(0x40|(nalu&0x1F)):(nalu&0x1F));
            int32_t sendLen;
            for(int32_t i=0; i<vsize; i++){
                if(!has_client || is_stop) break;
                if(conn_sockets[i].type==RTP_TCP){
                    std::lock_guard<std::mutex> lock (mlock_send);
                    sendLen =  send(conn_sockets[i].socket,rtp_pack,18,0);
                    sendLen += send(conn_sockets[i].socket,video+num*fau_num+1,rtpsize,0);
                }else{
                    char udp_pack[18 + size];
                    memcpy(udp_pack, rtp_pack, 18);
                    memcpy(udp_pack+18, video+num*fau_num+1, rtpsize);
                    sendLen =  sendto(rtp_socket, udp_pack+4, 14+rtpsize, 0, (struct sockaddr*)&conn_sockets[i].addr_in, conn_sockets[i].addrLen);
                }
                if(sendLen <= 0) {
                    FLOGE("SEND FU-A[%d][%d][%d][%d] errno=%d", sendLen,rtpsize+14,num,size,errno);
                    if(errno==0 || errno == 11) continue;
                    sp<AMessage> msg = new AMessage(kWhatClientSocketExit, this);
                    msg->setInt32("socket", conn_sockets[i].socket);
                    //msg->post();
                    handleClientSocketExit(msg);
                    return;
                }
            }
            num++;
        }
    }
    //FLOGD("SEND VFrame[ptsUsec=%ld][%d]", ptsUsec, size);
}

void RtspServer::sendAFrame(const     char* audio, int32_t size, int64_t ptsUsec)
{
    if(size<=0 || is_stop) return;
    char head_audio[sizeof(audiodata)];
    memcpy(head_audio, audiodata, sizeof(audiodata));
    int32_t dataLen = size + 10;
    head_audio[4]  = (dataLen & 0xFF000000) >> 24;
    head_audio[5]  = (dataLen & 0xFF0000) >> 16;
    head_audio[6] =  (dataLen & 0xFF00) >> 8;
    head_audio[7] =   dataLen & 0xFF;
    {
        std::lock_guard<std::mutex> lock (mlock_data);
        mManager->updataAsync(head_audio, sizeof(audiodata));
        mManager->updataAsync(audio, size);
    }

    int32_t vsize = conn_sockets.size();
    if(vsize<=0) return;
    sequencenumber1++;
    char rtp_pack[20];
    rtp_pack[0]  = '$';
    rtp_pack[1]  = 0x02;
    rtp_pack[2]  = ((size+16) & 0xFF00 ) >> 8;
    rtp_pack[3]  = (size+16) & 0xFF;
    rtp_pack[4]  = 0x80;
    rtp_pack[5]  = 0x61;
    rtp_pack[6]  = (sequencenumber1 & 0xFF00) >> 8;
    rtp_pack[7]  = sequencenumber1 & 0xFF;
    rtp_pack[8]  = (ptsUsec & 0xFF000000) >> 24;
    rtp_pack[9]  = (ptsUsec & 0xFF0000) >> 16;
    rtp_pack[10] = (ptsUsec & 0xFF00) >> 8;
    rtp_pack[11] =  ptsUsec & 0xFF;
    rtp_pack[16] = 0x00;
    rtp_pack[17] = 0x10;
    rtp_pack[18] = ((size+0) & 0x1FE0) >> 5;
    rtp_pack[19] = ((size+0) & 0x1F) << 3;
    //rtp_pack[20] = 0xFF;
    //rtp_pack[21] = 0xF1;
    //rtp_pack[22] = (1<<6) + (4 << 2) ;
    //rtp_pack[23] = ((2 & 3) << 6) + ((size+7) >> 11);
    //rtp_pack[24] = ((size+7) & 0x7ff) >> 3;
    //rtp_pack[25] = (((size+7) & 0x7) << 5)|0x1F;
    //rtp_pack[26] = 0xFC;
    int32_t sendLen;
    for(int32_t i=0; i<vsize; i++){
        if(!has_client || is_stop) break;
        if(conn_sockets[i].type==RTP_TCP){
            std::lock_guard<std::mutex> lock (mlock_send);
            sendLen =  send(conn_sockets[i].socket, rtp_pack, 20, 0);
            sendLen += send(conn_sockets[i].socket, audio, size, 0);
        }else{
            char udp_pack[20 + size];
            memcpy(udp_pack, rtp_pack, 20);
            memcpy(udp_pack+20, audio, size);
            sendLen =  sendto(rtp_socket, rtp_pack+4, 16+size, 0, (struct sockaddr*)&conn_sockets[i].addr_in, conn_sockets[i].addrLen);
        }
        if(sendLen <= 0) {
            FLOGE("SEND AUDIO[%d][%d] errno=%d", sendLen, 20+size, errno);
            if(errno==0 || errno == 11) continue;
            sp<AMessage> msg = new AMessage(kWhatClientSocketExit, this);
            msg->setInt32("socket", conn_sockets[i].socket);
            //msg->post();
            handleClientSocketExit(msg);
            return;
        }
    }
    //FLOGE("SEND AFrame[ptsUsec=%ld][%d]", ptsUsec, size);
}
