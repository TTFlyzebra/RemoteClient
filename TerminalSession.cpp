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
#include "config.h"
#include "Command.h"

TerminalSession::TerminalSession(ServerManager* manager)
:mManager(manager)
,is_stop(false)
,is_connect(false)
{
    printf("%s()\n", __func__);
    mManager->registerListener(this);
    recv_t = new std::thread(&TerminalSession::recvThread, this);
    send_t = new std::thread(&TerminalSession::sendThread, this);
    hand_t = new std::thread(&TerminalSession::handThread, this);
}

TerminalSession::~TerminalSession()
{
    mManager->unRegisterListener(this);
    is_stop = true;
	shutdown(mSocket, SHUT_RDWR);
    close(mSocket);
    printf("%s() 1\n", __func__);
    {
        std::lock_guard<std::mutex> lock (mlock_conn);
        mcond_conn.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock (mlock_send);
        mcond_send.notify_all();
    }
    printf("%s() 2\n", __func__);
    recv_t->join();
    send_t->join();
    hand_t->join();
    delete recv_t;
    delete send_t;
    delete hand_t;
    printf("%s()\n", __func__);
}

void TerminalSession::notify(char* data, int32_t size)
{
    char temp[4096];
    for (int32_t i = 0; i < 8; i++) {
        printf(temp, "%s%02x:\n", temp, data[i]);
    }
    printf("notify:size=[%d]\n%s\n", size, temp);
}

void TerminalSession::recvThread()
{
    printf("%s() start!\n", __func__);   
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
                printf("connect failed! %s errno :%d\n", strerror(errno), errno);
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
            printf("recv data size[%d], errno=%d.\n", recvLen, errno);
            if(recvLen==0 || (!(errno==11 || errno== 0))) {
                //TODO::disconnect
                is_connect = false;
                continue;
            }else{
                //std::lock_guard<std::mutex> lock (mlock_hand);
                //recvBuf.insert(recvBuf.end(), tempBuf, tempBuf+recvLen);
                //mcond_send.notify_one();
                notify(tempBuf, recvLen);
            }
        }
    }
    printf("%s() exit!\n", __func__);
}

void TerminalSession::sendThread()
{
    printf("%s() start!\n", __func__);
    while (!is_stop) {
        {
            std::unique_lock<std::mutex> lock (mlock_conn);
        	if (!is_connect) {
        	    mcond_conn.wait(lock);
                if(is_stop) break;
        	}
        }
        {
            std::unique_lock<std::mutex> lock (mlock_send);
        	if (sendBuf.empty()) {
        	    mcond_send.wait(lock);
                if(is_stop) break;
        	}
        	if (!sendBuf.empty()) {
        		int32_t sendSize = 0;
        		int32_t dataSize = sendBuf.size();
        		while(sendSize<dataSize){
        		    int32_t sendLen = send(mSocket,(const char*)&sendBuf[sendSize],dataSize-sendSize, 0);
        		    printf("%s sendLen=%d, errno=%d.\n", __func__, sendLen, errno);
        		    if (sendLen <= 0) {
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
    printf("%s() exit!\n", __func__);
}

void TerminalSession::handThread()
{
    printf("%s() start!\n", __func__);
    while(!is_stop){
        printf("%s() is running....\n", __func__);
        {
            std::lock_guard<std::mutex> lock (mlock_send);
            sendBuf.insert(sendBuf.end(), heartbeat, heartbeat + sizeof(heartbeat));
            mcond_send.notify_one();
        }
        for(int i=0;i<5000;i++){
            if(is_stop) break;
            usleep(1000);
        }
    }
    printf("%s() exit!\n", __func__);
}