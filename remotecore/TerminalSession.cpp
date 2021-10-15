//
// Created by FlyZebra on 2021/9/30 0030.
//

#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include "TerminalSession.h"
#include "Config.h"
#include "Command.h"
#include "Global.h"
#include "FlyLog.h"


TerminalSession::TerminalSession(ServerManager* manager)
:mManager(manager)
,is_stop(false)
,is_connect(false)
{
    FLOGD("%s()", __func__);
    //int flags = fcntl(mSocket, F_GETFL, 0);
    //fcntl(mSocket, F_SETFL, flags | O_NONBLOCK);
    mManager->registerListener(this);
    recv_t = new std::thread(&TerminalSession::connThread, this);
    send_t = new std::thread(&TerminalSession::sendThread, this);
    hand_t = new std::thread(&TerminalSession::handThread, this);
    time_t = new std::thread(&TerminalSession::timerThread, this);
}

TerminalSession::~TerminalSession()
{
    mManager->unRegisterListener(this);
    is_stop = true;
    shutdown(mSocket, SHUT_RDWR);
    close(mSocket);
    {
        std::lock_guard<std::mutex> lock (mlock_conn);
        mcond_conn.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock (mlock_send);
        mcond_send.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock(mlock_recv);
        mcond_recv.notify_all();
    }
    recv_t->join();
    send_t->join();
    hand_t->join();
    time_t->join();
    delete recv_t;
    delete send_t;
    delete hand_t;
    delete time_t;
    FLOGD("%s()", __func__);
}

int32_t TerminalSession::notify(const char* data, int32_t size)
{
    struct NotifyData* notifyData = (struct NotifyData*)data;
    switch (notifyData->type){
    case TYPE_VIDEO_DATA:
    case TYPE_AUDIO_DATA:
    case TYPE_SPSPPS_DATA:
        sendData(data, size);
        return 0;
    default: 
        //{
        //    char temp[256] = {0};
        //    int num = size<24?size:24;
        //    for (int32_t i = 0; i < num; i++) {
        //        sprintf(temp, "%s%02x:", temp, data[i]&0xFF);
        //    }
        //    FLOGE("notify:->%s", temp);
        //}
        return 0;
    }
    return 0;
}

void TerminalSession::connThread()
{
    char tempBuf[4096];
    while(!is_stop){
        if(!is_connect){
            {
                std::lock_guard<std::mutex> lock(mlock_send);
                sendBuf.clear();
            }
            {
                std::lock_guard<std::mutex> lock(mlock_recv);
                recvBuf.clear();
            }
            mSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            struct sockaddr_in servaddr;
            memset(&servaddr, 0, sizeof(servaddr));
            servaddr.sin_family = AF_INET;
            servaddr.sin_port = htons(TERMINAL_SERVER_TCP_PORT);
            servaddr.sin_addr.s_addr = inet_addr(REMOTEPC_SERVER_IP);
            if (connect(mSocket, (struct sockaddr *) &servaddr, sizeof(servaddr)) != 0) {
                FLOGD("TerminalSession connect failed! %s errno :%d", strerror(errno), errno);
                shutdown(mSocket, SHUT_RDWR);
                close(mSocket);
                for(int i=0;i<3000;i++){
                    if(is_stop) break;
                    usleep(1000);
                }
                if(is_stop) {
                    break;
                }else{
                    continue;
                }
            }else{
                std::lock_guard<std::mutex> lock (mlock_conn);
                is_connect = true;
                mcond_conn.notify_one();
            }
        }else{
            int recvLen = recv(mSocket, tempBuf, 4096, 0);
            //FLOGD("TerminalSession recv data size[%d], errno=%d.", recvLen, errno);
            if(recvLen>0){
                std::lock_guard<std::mutex> lock(mlock_recv);
                recvBuf.insert(recvBuf.end(), tempBuf, tempBuf + recvLen);
                mcond_recv.notify_one();
            }else if(recvLen == 0 || (!(errno == 11 || errno == 0))) {
                //TODO::disconnect
                FLOGE("TerminalSession recv data error, will disconnect, Len[%d]errno[%d][%s].", recvLen, errno,strerror(errno));
                is_connect = false;
                shutdown(mSocket, SHUT_RDWR);
                close(mSocket);
                continue;
            }
        }
    }
}

void TerminalSession::sendThread()
{
    while (!is_stop) {
        {
            std::unique_lock<std::mutex> lock (mlock_conn);
        	while (!is_stop && !is_connect) {
        	    mcond_conn.wait(lock);
        	}
            if(is_stop) break;
        }
        {
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
                        is_connect = false;
                        shutdown(mSocket, SHUT_RDWR);
                        close(mSocket);
                        break;
                    }
                }else{
                    sendLen+=ret;
                }
            }
            if(sendData != nullptr) free(sendData);
        }
    }
}

void TerminalSession::handThread()
{
     while(!is_stop){
        {
            std::unique_lock<std::mutex> lock(mlock_recv);
            while (!is_stop && recvBuf.size() < 8) {
                mcond_recv.wait(lock);
            }
            if(is_stop) break;
            if(((recvBuf[0]&0xFF)!=0xEE)||((recvBuf[1]&0xFF)!=0xAA)){
                FLOGE("TerminalSession handleData bad header[%02x:%02x]", recvBuf[0]&0xFF, recvBuf[1]&0xFF);
                recvBuf.clear();
                continue;
            }
        }
        {
            std::unique_lock<std::mutex> lock(mlock_recv);
            int32_t dLen = (recvBuf[4]&0xFF)<<24|(recvBuf[5]&0xFF)<<16|(recvBuf[6]&0xFF)<<8|(recvBuf[7]&0xFF);
            int32_t aLen = dLen + 8;
            while(!is_stop && (aLen>(int32_t)recvBuf.size())) {
                mcond_recv.wait(lock);
            }
            if(is_stop) break;
            mManager->updataSync(&recvBuf[0], aLen);
            recvBuf.erase(recvBuf.begin(),recvBuf.begin()+aLen);
        }
    }
}

void TerminalSession::sendData(const char* data, int32_t size)
{
    std::lock_guard<std::mutex> lock (mlock_send);
    if (sendBuf.size() > TERMINAL_MAX_BUFFER) {
        FLOGD("NOTE::TerminalSession send buffer too max, wile clean %zu size", sendBuf.size());
    	sendBuf.clear();
    }
    sendBuf.insert(sendBuf.end(), data, data + size);
    mcond_send.notify_one();
}

void TerminalSession::timerThread()
{
    while(!is_stop){
        char heartbeat_t[sizeof(HEARTBEAT_T)];
        memcpy(heartbeat_t,HEARTBEAT_T,sizeof(HEARTBEAT_T));
        memcpy(heartbeat_t+8,mTerminal.tid,8);
        sendData((const char*)heartbeat_t,sizeof(heartbeat_t));
        for(int i=0;i<50;i++){
            if(is_stop) break;
            usleep(100000);
        }
    }
}

void TerminalSession::resetConnect()
{
    
}


