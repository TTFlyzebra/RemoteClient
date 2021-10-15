//
// Created by FlyZebra on 2021/9/16 0016.
//
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>
#include <sys/syscall.h>
#include "RtspClient.h"
#include "RtspServer.h"
#include "Config.h"
#include "Command.h"
#include "Global.h"
#include "FlyLog.h"

RtspClient::RtspClient(RtspServer* server, ServerManager* manager, int32_t socket)
:mServer(server)
,mManager(manager)
,mSocket(socket)
,is_stop(false)
,is_disconnect(false)
,is_playing(false)
,is_send_iframe(false)
,sequencenumber1(0)
,sequencenumber2(0)
,conn_type(RTP_TCP)
,mPid(0)
,mTid(0)
{
    FLOGD("%s()", __func__);
    //int flags = fcntl(mSocket, F_GETFL, 0);
    //fcntl(mSocket, F_SETFL, flags | O_NONBLOCK);
    mManager->registerListener(this);
    recv_t = new std::thread(&RtspClient::recvThread, this);
}

RtspClient::~RtspClient()
{
    mManager->unRegisterListener(this);
    is_stop = true;
    shutdown(mSocket, SHUT_RDWR);
    close(mSocket);
    {
        std::lock_guard<std::mutex> lock (mlock_send);
        mcond_send.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock (mlock_recv);
        mcond_recv.notify_all();
    }
    recv_t->join();
    send_t->join();
    hand_t->join();
    delete recv_t;
    delete send_t;
    delete hand_t;
    FLOGD("%s()", __func__);
}

int32_t RtspClient::notify(const char* data, int32_t size)
{
    if(!is_playing) return 0;
    struct NotifyData* notifyData = (struct NotifyData*)data;    
    int32_t pts = (data[20] & 0xFF) << 24 | (data[21] & 0xFF) << 16 | (data[22] & 0xFF) << 8 | (data[23] & 0xFF);
    switch (notifyData->type){
    case TYPE_SPSPPS_DATA:
        sendSPSPPS(data+24, size-24, pts);
        is_send_iframe = true;
        return 0;
    case TYPE_VIDEO_DATA:
        if(is_send_iframe) {
            sendVFrame(data+24, size-24, pts);
        }
        return 0;
    case TYPE_AUDIO_DATA:
        if(is_send_iframe) {
            sendAFrame(data+24, size-24, pts);
        }
        return 0;
    }
    return 0;
}

void RtspClient::recvThread()
{
    mPid = (int32_t)syscall(SYS_getpid);
    mTid = (int32_t)syscall(SYS_gettid);
    send_t = new std::thread(&RtspClient::sendThread, this);
    hand_t = new std::thread(&RtspClient::handleData, this);

    char tempBuf[4096];
    while(!is_stop){
        memset(tempBuf,0,4096);
        int32_t recvLen = recv(mSocket, tempBuf, 4096, 0);
        FLOGD("RtspClient recv:len=[%d], errno=[%d]\n%s", recvLen, errno, tempBuf);
        if (recvLen <= 0) {
            if(recvLen==0 || (!(errno==11 || errno== 0))) {
                is_stop = true;
                break;
            }
        }else{
            if(is_playing){
                char heartbeat_audio[sizeof(HEARTBEAT_AUDIO)];
                memcpy(heartbeat_audio,HEARTBEAT_AUDIO,sizeof(HEARTBEAT_AUDIO));
                memcpy(heartbeat_audio+8,&mPid,4);
                memcpy(heartbeat_audio+12,&mTid,4);
                mManager->updataSync((const char*)heartbeat_audio, sizeof(heartbeat_audio));
                char heartbeat_video[sizeof(HEARTBEAT_VIDEO)];
                memcpy(heartbeat_video,HEARTBEAT_VIDEO,sizeof(HEARTBEAT_VIDEO));
                memcpy(heartbeat_video+8,&mPid,4);
                memcpy(heartbeat_video+12,&mTid,4);
                mManager->updataSync((const char*)heartbeat_video, sizeof(heartbeat_video));
            }
            std::lock_guard<std::mutex> lock (mlock_recv);
            recvBuf.insert(recvBuf.end(), tempBuf, tempBuf+recvLen);
            mcond_recv.notify_one();
        }
    }
    disConnect();
}

void RtspClient::sendThread()
{
    while (!is_stop) {
        char* sendData = nullptr;
        int32_t sendSize = 0;
        {
            std::unique_lock<std::mutex> lock (mlock_send);
    	    while(!is_stop &&sendBuf.empty()) {
    	        mcond_send.wait(lock);
    	    }
    	    if(is_stop) break;
    	    sendSize = sendBuf.size();
    	    if(sendSize > 0){
    	        sendData = (char *)malloc(sendSize * sizeof(char));
    	        memcpy(sendData, (char*)&sendBuf[0], sendSize);
    	        sendBuf.clear();
    	    }
    	}
    	int32_t sendLen = 0;
    	while(!is_stop && (sendLen < sendSize)){
    	    int32_t ret = send(mSocket,(const char*)sendData+sendLen, sendSize - sendLen, 0);
    	    if (ret <= 0) {
    	         if(ret==0 || (!(errno==11 || errno== 0)))  {
    	            is_stop = true;
                    FLOGE("RtspClient send error, len=[%d] errno[%d][%s]!",ret, errno, strerror(errno));
    	            break;
    	        }
    	    }else{
                sendLen+=ret;
    	    }
    	}
    	if(sendData != nullptr) free(sendData);
    }
    disConnect();
}

//TODO::one crash (RtspClient::handleData()+320) (BuildId: cef4e20c46299a389c071f449db3c7a9)
void RtspClient::handleData()
{
    while(!is_stop){
        std::unique_lock<std::mutex> lock (mlock_recv);
        while (!is_stop && recvBuf.empty()) {
            mcond_recv.wait(lock);
        }
        if(is_stop) break;
        char url[512] = {0};
        char ver[64] = {0};
        char action[64] = {0};
        if(sscanf((const char*)&recvBuf[0], "%s %s %s\r", action, url, ver) == 3) {
            int32_t cseq;
        	sscanf(strstr((const char*)&recvBuf[0], "CSeq"), "CSeq: %d", &cseq);
            std::string method(action);
            if (method == "OPTIONS") {
                onOptionsRequest((const char*)&recvBuf[0], cseq);
            }else if(method == "DESCRIBE"){
                onDescribeRequest((const char*)&recvBuf[0], cseq);
            }else if(method == "SETUP"){
                onSetupRequest((const char*)&recvBuf[0], cseq);
            }else if(method == "PLAY"){
                onPlayRequest((const char*)&recvBuf[0], cseq);
            }else if(method == "GET_PARAMETER"){
                onGetParameterRequest((const char*)&recvBuf[0], cseq);
            }
        }else{
            onOtherRequest((const char*)&recvBuf[0], -1);
        } 
        char *temp = (char *)malloc((sendBuf.size()+1) * sizeof(char));
        temp[sendBuf.size()] = 0;
        memcpy(temp, &sendBuf[0], sendBuf.size());
        FLOGD("RtspClient will send:len=[%zu]\n%s",sendBuf.size(), temp);
        free(temp);
        recvBuf.clear();
    }
}

void RtspClient::sendData(const char* data, int32_t size)
{
    std::lock_guard<std::mutex> lock (mlock_send);
    if (sendBuf.size() > TERMINAL_MAX_BUFFER) {
        FLOGD("NOTE::RtspClient send buffer too max, wile clean %zu size", sendBuf.size());
        //TODO::reset video recrod for I frame
        //sendBuf.clear();
        disConnect();
        return;
    }
    sendBuf.insert(sendBuf.end(), data, data + size);
    mcond_send.notify_one();
}


void RtspClient::disConnect()
{
    if(!is_disconnect){
        is_disconnect = true;
        shutdown(mSocket, SHUT_RDWR);
        close(mSocket);
        if(is_playing){
            mServer->disconnectClient(this);
            char video_stop[sizeof(VIDEO_STOP)];
            memcpy(video_stop,VIDEO_STOP,sizeof(VIDEO_STOP));
            memcpy(video_stop+8,&mPid,4);
            memcpy(video_stop+12,&mTid,4);
            mManager->updataSync((const char*)video_stop,sizeof(video_stop));
            char audio_stop[sizeof(AUDIO_STOP)];
            memcpy(audio_stop,AUDIO_STOP,sizeof(AUDIO_STOP));
            memcpy(audio_stop+8,&mPid,4);
            memcpy(audio_stop+12,&mTid,4);
            mManager->updataSync((const char*)audio_stop,sizeof(audio_stop));
        }
    }
}


void RtspClient::appendCommonResponse(std::string *response, int32_t cseq)
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


void RtspClient::onOptionsRequest(const char* data, int32_t cseq)
{
    std::string response = "RTSP/1.0 200 OK\r\n";
    appendCommonResponse(&response, cseq);
    response.append("Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER, SET_PARAMETER\r\n");
    response.append("\r\n");
    int32_t ret = send(mSocket,response.c_str(),response.size(),0);
    if(ret <= 0) disConnect();
}

void RtspClient::onDescribeRequest(const char* data, int32_t cseq)
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    getsockname(mSocket, (struct sockaddr *)&addr, &addrlen);
    std::string response = "RTSP/1.0 200 OK\r\n";
    appendCommonResponse(&response, cseq);
    std::string spd;
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
    sprintf(temp, "Content-Length: %d\r\n",(int32_t)spd.size());
    response.append(temp);
    response.append("Content-Type: application/sdp\r\n");
    response.append("\r\n");
    response.append(spd.c_str());
    int32_t ret = send(mSocket,response.c_str(),response.size(),0);
    if(ret <= 0) disConnect();
}

void RtspClient::onSetupRequest(const char* data, int32_t cseq)
{
    std::string response = "RTSP/1.0 200 OK\r\n";
    appendCommonResponse(&response, cseq);
    int32_t track1 = 0;
    int32_t track2 = 1;
    const char *temp0 = strstr((const char*)data, "interleaved=");
    if(temp0!=nullptr){
        sscanf(temp0, "interleaved=%d-%d", &track1, &track2);
    }
    if (strncmp(strstr((const char*)data, "RTP/AVP"), "RTP/AVP/TCP", 11) == 0) {
        conn_type = RTP_TCP;
        char temp1[128];
        sprintf(temp1, "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n", track1, track2);
        response.append(temp1);
    }else
    /*if (strncmp(strstr((const char*)data, "RTP/AVP"), "RTP/AVP/UDP", 11) == 0)*/
    {
        conn_type = RTP_UDP;
        const char *temp1 = strstr((const char*)data, "client_port=");
        if(temp1!=nullptr){
            sscanf(temp1, "client_port=%d-%d", &conn_rtp_port, &conn_rtcp_port);
        }
        conn_addrLen = sizeof(conn_addr_in);
        getpeername(mSocket, (struct sockaddr *)&conn_addr_in, (socklen_t*)&conn_addrLen);
        conn_addr_in.sin_port = htons(conn_rtp_port);
        char temp2[128];
        sprintf(temp2, "Transport: RTP/AVP/UDP;unicast;client_port=%d-%d;server_port=%d-%d;interleaved=%d-%d\r", \
                conn_rtp_port,RTSP_SERVER_UDP_PORT1,RTSP_SERVER_UDP_PORT2, conn_rtcp_port, track1, track2);
        response.append(temp2);
    }
    char temp[128];
    sprintf(temp, "Session: %d\r\n",mSocket);
    response.append(temp);
    response.append("\r\n");
    int32_t ret = send(mSocket,response.c_str(),response.size(),0);
    if(ret <= 0) disConnect();
}

void RtspClient::onPlayRequest(const char* data, int32_t cseq)
{
    std::string response = "RTSP/1.0 200 OK\r\n";
    appendCommonResponse(&response, cseq);
    response.append("Range: npt=0.000-\r\n");
    char temp[128];
    sprintf(temp, "Session: %d\r\n",mSocket);
    response.append(temp);
    response.append("\r\n");
    int32_t ret = send(mSocket,response.c_str(),response.size(),0);
    if(ret <= 0) disConnect();
    {
        char video_start[sizeof(VIDEO_START)];
	    memcpy(video_start, VIDEO_START, sizeof(VIDEO_START));
	    memcpy(video_start + 8, &mPid, 4);
	    memcpy(video_start + 12, &mTid, 4);
	    mManager->updataSync((const char*)video_start, sizeof(video_start));
	    char audio_start[sizeof(AUDIO_START)];
	    memcpy(audio_start, AUDIO_START, sizeof(AUDIO_START));
	    memcpy(audio_start + 8, &mPid, 4);
        memcpy(audio_start + 12, &mTid, 4);
	    mManager->updataSync((const char*)audio_start, sizeof(audio_start));
        is_playing = true;
    }
}

void RtspClient::onGetParameterRequest(const char* data, int32_t cseq)
{
    std::string response = "RTSP/1.0 200 OK\r\n";
    appendCommonResponse(&response, cseq);
    response.append("\r\n");
    int32_t ret = send(mSocket,response.c_str(),response.size(),0);
    if(ret <= 0) disConnect();
}

void RtspClient::onOtherRequest(const char* data, int32_t cseq)
{
    std::string response = "RTSP/1.0 200 OK\r\n";
    appendCommonResponse(&response, cseq);
    response.append("\r\n");
    int32_t ret = send(mSocket,response.c_str(),response.size(),0);
    if(ret <= 0) disConnect();
}

void RtspClient::sendSPSPPS(const     char* sps_pps, int32_t size, int64_t ptsUsec)
{
    if(size<=0 || is_stop) return;
    sequencenumber1++;
    char rtp_pack[16];
    rtp_pack[0] = '$';
    rtp_pack[1] = 0x00;
    rtp_pack[2] = ((size+12) & 0xFF00 ) >> 8;
    rtp_pack[3] = (size+12) & 0xFF;
    rtp_pack[4] = 0x80;
    rtp_pack[5] = 0x60;
    rtp_pack[6] = (sequencenumber1 & 0xFF00) >> 8;
    rtp_pack[7] =  sequencenumber1 & 0xFF;
    rtp_pack[8]  = (ptsUsec & 0xFF000000) >> 24;
    rtp_pack[9]  = (ptsUsec & 0xFF0000) >> 16;
    rtp_pack[10] = (ptsUsec & 0xFF00) >> 8;
    rtp_pack[11] =  ptsUsec & 0xFF;
    if(is_stop) return;
    char send_pack[16 + size];
    memcpy(send_pack, rtp_pack, 16);
    memcpy(send_pack+16, sps_pps, size);
    if(conn_type==RTP_TCP){
        sendData(send_pack, size+16);
    }else{
        sendto(mServer->rtp_socket, send_pack+4, 12+size, 0, (struct sockaddr*)&conn_addr_in, conn_addrLen);
    }
}

void RtspClient::sendVFrame(const     char* video, int32_t size, int64_t ptsUsec)
{
    if(size<=0 || is_stop) return;
    unsigned char nalu = video[0];
    if((nalu&0x1F)==5){
        sendSPSPPS((const  char*)&(mServer->sps_pps[0]), mServer->sps_pps.size(), ptsUsec);
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
        rtp_pack[7]  =  sequencenumber1 & 0xFF;
        rtp_pack[8]  = (ptsUsec & 0xFF000000) >> 24;
        rtp_pack[9]  = (ptsUsec & 0xFF0000) >> 16;
        rtp_pack[10] = (ptsUsec & 0xFF00) >> 8;
        rtp_pack[11] =  ptsUsec & 0xFF;
        if(is_stop) return;
        char send_pack[16 + size];
        memcpy(send_pack, rtp_pack, 16);
        memcpy(send_pack+16, video, size);
        if(conn_type==RTP_TCP){
            sendData(send_pack, size+16);
        }else{
            sendto(mServer->rtp_socket, send_pack+4, 12+size, 0, (struct sockaddr*)&conn_addr_in, conn_addrLen);
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
            rtp_pack[7]  =  sequencenumber1 & 0xFF;
            rtp_pack[8]  = (ptsUsec & 0xFF000000) >> 24;
            rtp_pack[9]  = (ptsUsec & 0xFF0000) >> 16;
            rtp_pack[10] = (ptsUsec & 0xFF00) >> 8;
            rtp_pack[11] =  ptsUsec & 0xFF;
            rtp_pack[16] =  (nalu&0xE0)|0x1C;
            rtp_pack[17] =  first?(0x80|(nalu&0x1F)):(last?(0x40|(nalu&0x1F)):(nalu&0x1F));
            if(is_stop) return;
            char send_pack[18 + size];
            memcpy(send_pack, rtp_pack, 18);
            memcpy(send_pack+18, video+num*fau_num+1, rtpsize);
            if(conn_type==RTP_TCP){
                sendData(send_pack,rtpsize+18);
            }else{
                char send_pack[18 + size];
                sendto(mServer->rtp_socket, send_pack+4, 14+rtpsize, 0, (struct sockaddr*)&conn_addr_in, conn_addrLen);
            }
            num++;
        }
    }
    //FLOGD("SEND VFrame[ptsUsec=%ld][%d]", ptsUsec, size);
}

void RtspClient::sendAFrame(const     char* audio, int32_t size, int64_t ptsUsec)
{
    if(size<=0 || is_stop) return;
    sequencenumber2++;
    char rtp_pack[20];
    rtp_pack[0]  = '$';
    rtp_pack[1]  = 0x02;
    rtp_pack[2]  = ((size+16) & 0xFF00 ) >> 8;
    rtp_pack[3]  = (size+16) & 0xFF;
    rtp_pack[4]  = 0x80;
    rtp_pack[5]  = 0x61;
    rtp_pack[6]  = (sequencenumber2 & 0xFF00) >> 8;
    rtp_pack[7]  =  sequencenumber2 & 0xFF;
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
    if(is_stop) return;
    char send_pack[20 + size];
    memcpy(send_pack, rtp_pack, 20);
    memcpy(send_pack+20, audio, size);
    if(conn_type==RTP_TCP){
        sendData(send_pack, size+20);
    }else{
        sendto(mServer->rtp_socket, send_pack+4, 16+size, 0, (struct sockaddr*)&conn_addr_in, conn_addrLen);
    }
    //FLOGE("SEND AFrame[ptsUsec=%ld][%d]", ptsUsec, size+20);
}


