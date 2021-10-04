//
// Created by FlyZebra on 2021/9/30 0030.
//

#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "TerminalSession.h"
#include "FlyLog.h"
#include "Config.h"
#include "Command.h"

TerminalSession::TerminalSession(ServerManager* manager)
:mManager(manager)
,is_stop(false)
,is_connect(false)
{
    FLOGD("%s()", __func__);
    mManager->registerListener(this);
    recv_t = new std::thread(&TerminalSession::connThread, this);
    send_t = new std::thread(&TerminalSession::sendThread, this);
    hand_t = new std::thread(&TerminalSession::handThread, this);
    time_t = new std::thread(&TerminalSession::timerThread, this);
}

TerminalSession::~TerminalSession()
{
    is_stop = true;
    mManager->unRegisterListener(this);
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
        std::lock_guard<std::mutex> lock (mlock_recv);
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
    //char temp[4096] = {0};
    //memset(temp,0,4096);
    //for (int32_t i = 0; i < 10; i++) {
    //    sprintf(temp, "%s%02x:", temp, data[i]);
    //}
    //FLOGD("TerminalSession->notify->%s[%d]", temp, size);
    return -1;
}

void TerminalSession::connThread()
{
    char tempBuf[4096];
    while(!is_stop){
        if(!is_connect){
            mSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            struct sockaddr_in servaddr;
            memset(&servaddr, 0, sizeof(servaddr));
            servaddr.sin_family = AF_INET;
            servaddr.sin_port = htons(TERMINAL_SERVER_TCP_PORT);
            servaddr.sin_addr.s_addr = inet_addr("192.168.1.88");
            if (connect(mSocket, (struct sockaddr *) &servaddr, sizeof(servaddr)) != 0) {
                FLOGD("TerminalSession connect failed! %s errno :%d", strerror(errno), errno);
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
            FLOGD("TerminalSession recv data size[%d], errno=%d.", recvLen, errno);
            if(recvLen==0 || (!(errno==11 || errno== 0))) {
                //TODO::disconnect
                is_connect = false;
                continue;
            }else{
                std::lock_guard<std::mutex> lock (mlock_recv);
                recvBuf.insert(recvBuf.end(), tempBuf, tempBuf+recvLen);
                mcond_recv.notify_one();
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
            std::unique_lock<std::mutex> lock (mlock_send);
        	while (!is_stop && sendBuf.empty()) {
        	    mcond_send.wait(lock);
        	}
            if(is_stop) break;
            while(!is_stop && !sendBuf.empty()){
    	        int32_t sendLen = send(mSocket,(const char*)&sendBuf[0],sendBuf.size(), 0);
        	    if (sendLen < 0) {
        	        if(errno != 11 || errno != 0) {
                        FLOGE("TerminalSession send error, len=[%d] errno[%d]!",sendLen, errno);
        	            is_connect = false;
                        sendBuf.clear();
        	            break;
        	        }
        	    }else{
                    sendBuf.erase(sendBuf.begin(),sendBuf.begin()+sendLen);
        	    }
    	    }
        }
    }
}

void TerminalSession::handThread()
{
    while(!is_stop){
        std::unique_lock<std::mutex> lock (mlock_recv);
        while (!is_stop && recvBuf.empty()) {
            mcond_recv.wait(lock);
        }
        if(is_stop) break;
        std::fill(recvBuf.begin(), recvBuf.end(), 0);
        recvBuf.clear();
    }
}

void TerminalSession::sendData(const char* data, int32_t size)
{
    std::lock_guard<std::mutex> lock (mlock_send);
    if (sendBuf.size() > TERMINAL_MAX_BUFFER) {
        FLOGD("NOTE::RtspClient send buffer too max, wile clean %zu size", sendBuf.size());
    	sendBuf.clear();
    }
    sendBuf.insert(sendBuf.end(), data, data + size);
    mcond_send.notify_one();
}

void TerminalSession::timerThread()
{
    while(!is_stop){
        sendData((const char*)heartbeat,sizeof(heartbeat));
        for(int i=0;i<5000;i++){
            if(is_stop) break;
            usleep(1000);
        }
    }
}


