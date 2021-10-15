//
// Created by FlyZebra on 2021/9/16 0016.
//
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "InputClient.h"
#include "InputServer.h"
#include "Config.h"
#include "Command.h"
#include "FlyLog.h"

InputClient::InputClient(InputServer* server, ServerManager* manager, int32_t socket)
:mServer(server)
,mManager(manager)
,mSocket(socket)
,is_stop(false)
,is_disconnect(false)
{
    FLOGD("%s()", __func__);
    //int flags = fcntl(mSocket, F_GETFL, 0);
    //fcntl(mSocket, F_SETFL, flags | O_NONBLOCK);
    int flags = fcntl(mSocket, F_GETFL, 0);
    fcntl(mSocket, F_SETFL, flags | O_NONBLOCK);
    mManager->registerListener(this);
    recv_t = new std::thread(&InputClient::recvThread, this);
}

InputClient::~InputClient()
{
    mManager->unRegisterListener(this);
    is_stop = true;
    {
        std::lock_guard<std::mutex> lock (mlock_send);
        mcond_send.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock (mlock_recv);
        mcond_recv.notify_all();
    }
    
    shutdown(mSocket, SHUT_RDWR);
    close(mSocket);
    
    recv_t->join();
    send_t->join();
    hand_t->join();
    delete recv_t;
    delete send_t;
    delete hand_t;
    FLOGD("%s()", __func__);
}

int32_t InputClient::notify(const char* data, int32_t size)
{
    return 0;
}

void InputClient::recvThread()
{
    send_t = new std::thread(&InputClient::sendThread, this);
    hand_t = new std::thread(&InputClient::handleData, this);
    char tempBuf[4096];
    while(!is_stop){
        memset(tempBuf, 0, 4096);
        int recvLen = recv(mSocket, tempBuf, 4096, 0);
        FLOGD("InputClient recv:len=[%d], errno=[%d]\n%s", recvLen, errno, tempBuf);
        if (recvLen <= 0) {
            if(recvLen==0 || (!(errno==11 || errno== 0))) {
                is_stop = true;
                break;
            }
        }else{
            std::lock_guard<std::mutex> lock (mlock_recv);
            recvBuf.insert(recvBuf.end(), tempBuf, tempBuf+recvLen);
            mcond_recv.notify_one();
        }
    }
    disConnect();
}

void InputClient::sendThread()
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
                    FLOGE("InputClient send error, len=[%d] errno[%d][%s]!",ret, errno, strerror(errno));
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

void InputClient::handleData()
{
    while(!is_stop){
        {
            std::unique_lock<std::mutex> lock (mlock_recv);
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
            std::unique_lock<std::mutex> lock (mlock_recv);
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

void InputClient::sendData(const char* data, int32_t size)
{
    std::lock_guard<std::mutex> lock (mlock_send);
    if (sendBuf.size() > TERMINAL_MAX_BUFFER) {
        FLOGD("NOTE::InputClient send buffer too max, wile clean %zu size", sendBuf.size());
    	//sendBuf.clear();
    	disConnect();
    	return;
    }
    sendBuf.insert(sendBuf.end(), data, data + size);
    mcond_send.notify_one();
}


void InputClient::disConnect()
{
    if(!is_disconnect){
        is_disconnect = true;
        mServer->disconnectClient(this);
    }
}
