//
// Created by FlyZebra on 2021/9/16 0016.
//
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>

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
    mManager->registerListener(this);
    recv_t = new std::thread(&InputClient::recvThread, this);
    send_t = new std::thread(&InputClient::sendThread, this);
    hand_t = new std::thread(&InputClient::handleData, this);
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
    char tempBuf[4096];
    while(!is_stop){
        memset(tempBuf,0,4096);
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
        std::unique_lock<std::mutex> lock (mlock_send);
    	while(!is_stop &&sendBuf.empty()) {
    	    mcond_send.wait(lock);
    	}
        if(is_stop) break;
    	while(!is_stop && !sendBuf.empty()){
    	    int32_t sendLen = send(mSocket,(const char*)&sendBuf[0],sendBuf.size(), 0);
    	    if (sendLen < 0) {
    	        if(errno != 11 || errno != 0) {
    	            is_stop = true;
                    FLOGE("InputClient send error, len=[%d] errno[%d]!",sendLen, errno);
    	            break;
    	        }
    	    }else{
                sendBuf.erase(sendBuf.begin(),sendBuf.begin()+sendLen);
    	    }
    	}
    }
    disConnect();
}

void InputClient::handleData()
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

void InputClient::sendData(const char* data, int32_t size)
{
    std::lock_guard<std::mutex> lock (mlock_send);
    if (sendBuf.size() > TERMINAL_MAX_BUFFER) {
        FLOGD("NOTE::InputClient send buffer too max, wile clean %zu size", sendBuf.size());
    	sendBuf.clear();
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
