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
    recv_t = new std::thread(&TerminalSession::recvThread, this);
    send_t = new std::thread(&TerminalSession::sendThread, this);
    hand_t = new std::thread(&TerminalSession::handThread, this);
}

TerminalSession::~TerminalSession()
{
    FLOGD("%s()", __func__);
    mManager->unRegisterListener(this);
    is_stop = true;
    close(mSocket);
    recv_t->join();
    send_t->join();
    hand_t->join();
    delete recv_t;
    delete send_t;
    delete hand_t;
}

void TerminalSession::notify(char* data, int32_t size)
{
    char temp[4096];
    for (int32_t i = 0; i < 8; i++) {
        FLOGD(temp, "%s%02x:", temp, data[i]);
    }
    FLOGD("notify:size=[%d]\n%s", size, temp);
}

void TerminalSession::recvThread()
{
    FLOGD("%s()", __func__);
    char tempBuf[4096];
    while(!is_stop){
        if(!is_connect){
            mSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            struct sockaddr_in servaddr;
            memset(&servaddr, 0, sizeof(servaddr));
            servaddr.sin_family = AF_INET;
            servaddr.sin_port = htons(TERMINAL_SERVER_TCP_PORT);
            servaddr.sin_addr.s_addr = inet_addr("192.168.8.243");
            if (connect(mSocket, (struct sockaddr *) &servaddr, sizeof(servaddr)) != 0) {
                FLOGD("connect failed! %s errno :%d", strerror(errno), errno);
                close(mSocket);
                continue;
            }else{
               is_connect = true;
            }
        }else{
            int recvLen = recv(mSocket, tempBuf, 4096, 0);
            FLOGD("recv data size[%d]", recvLen);
            if (recvLen < 0) {
                if(errno!=11 || errno!= 0) {
                    //TODO::disconnect
                    is_connect = false;
                    continue;
                }
            }else{
                //std::lock_guard<std::mutex> lock (mlock_hand);
                //recvBuf.insert(recvBuf.end(), tempBuf, tempBuf+recvLen);
                //mcond_send.notify_one();
                notify(tempBuf, recvLen);
            }
        }
    }

}

void TerminalSession::sendThread()
{
    FLOGD("%s()", __func__);
    while (!is_stop) {
        std::unique_lock<std::mutex> lock (mlock_send);
    	if (sendBuf.empty()) {
    	    mcond_send.wait(lock);
    	}
    	if (!sendBuf.empty()) {
    		int32_t sendSize = 0;
    		int32_t dataSize = sendBuf.size();
    		while(sendSize<dataSize){
    		    int32_t sendLen = send(mSocket,(const char*)&sendBuf[sendSize],dataSize-sendSize, 0);
    		    if (sendLen < 0) {
    		        if(errno!=11 || errno!= 0) {
    		            //TODO::disconnect
    		            sendBuf.clear();
    		            is_connect = false;
    		            break;
    		        }
    		    }else{
    		        sendSize+=sendLen;
    		    }
    		}
    		sendBuf.clear();
    	}
    }
}

void TerminalSession::handThread()
{
    FLOGD("%s()", __func__);
    while(!is_stop){
        std::lock_guard<std::mutex> lock (mlock_send);
        sendBuf.insert(sendBuf.end(), heartbeat, heartbeat + sizeof(heartbeat));
        mcond_send.notify_one();
        sleep(1);
    }
}