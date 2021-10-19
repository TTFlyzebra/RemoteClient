//
// Created by FlyZebra on 2021/8/12 0012.
//
#include <errno.h>
#include <sys/socket.h>
#include <cutils/sockets.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cutils/properties.h>
#include "EncoderAudio.h"
#include "HandlerEvent.h"
#include "Command.h"
#include "Config.h"
#include "Global.h"
#include "FlyLog.h"

using namespace android;

EncoderAudio::EncoderAudio(ServerManager* manager)
:mManager(manager)
,is_stop(false)
,out_buf((uint8_t *)av_malloc(OUT_SAMPLE_RATE))
,client_t(nullptr)
,sequencenumber(0)
{
    FLOGD("%s()", __func__);
    mManager->registerListener(this);
    server_t = new std::thread(&EncoderAudio::serverSocket, this);
    check_t = new std::thread(&EncoderAudio::clientChecked, this);
}

EncoderAudio::~EncoderAudio()
{
    mManager->unRegisterListener(this);
    is_stop = true;
    {
        std::lock_guard<std::mutex> lock (mlock_work);
        mcond_work.notify_all();
    }

    shutdown(server_socket, SHUT_RDWR);
    close(server_socket);    
    {
        std::lock_guard<std::mutex> lock (mlock_client);
        for (std::list<int32_t>::iterator it = audio_clients.begin(); it != audio_clients.end(); ++it) {
            int32_t client_socket = (int32_t)*it;
            shutdown(client_socket, SHUT_RDWR);
            close(client_socket);
        }
        audio_clients.clear();
    }

    std::map<int32_t, struct SwrContext*>::iterator it;
    for(it=swr_cxts.begin();it!=swr_cxts.end();++it){
        if (it->second) swr_free(&it->second);
    }
    swr_cxts.clear();
    if (out_buf) av_free(out_buf);

    server_t->join();
    check_t->join();
    delete server_t;
    delete check_t;
    if(client_t!=nullptr){
        client_t->join();
    }
    FLOGD("%s()", __func__);
}

int32_t EncoderAudio::notify(const char* data, int32_t size)
{
    struct NotifyData* notifyData = (struct NotifyData*)data;
    switch (notifyData->type){
    case TYPE_AUDIO_START:
        {
            lastHeartBeat = systemTime(CLOCK_MONOTONIC);
            int64_t uid;
            memcpy(&uid, data+16, 8);
            std::lock_guard<std::mutex> lock (mlock_work);
            std::map<int64_t, int64_t>::iterator it = mUsers.find(uid);
            if(it != mUsers.end()){
                it->second = lastHeartBeat;
            }else{
                mUsers.emplace(uid,lastHeartBeat);
                mcond_work.notify_one();
            }
            FLOGD("EncoderAudio recv start mUsers.size=%zu", mUsers.size());
        }
        return 0;
    case TYPE_HEARTBEAT_AUDIO:
        {
            lastHeartBeat = systemTime(CLOCK_MONOTONIC);
            int64_t uid;
            memcpy(&uid, data+16, 8);
            std::lock_guard<std::mutex> lock (mlock_work);
            std::map<int64_t, int64_t>::iterator it = mUsers.find(uid);
            if(it != mUsers.end()){
                it->second = lastHeartBeat;
            }else{
                mUsers.emplace(uid,lastHeartBeat);
                mcond_work.notify_one();
            }
        }
        return 0;
    case TYPE_AUDIO_STOP:
        {
            int64_t uid;
            memcpy(&uid, data+16, 8);
            std::lock_guard<std::mutex> lock (mlock_work);
            mUsers.erase(uid);
            FLOGD("EncoderAudio recv stop mUsers.size=%zu", mUsers.size());
        }
        return 0;
    }
    return 0;
}

void EncoderAudio::onMessageReceived(const sp<AMessage> &msg)
{

}

void EncoderAudio::serverSocket()
{
	FLOGD("EncoderAudio serverSocket start!");
	codecInit();
    while(!is_stop) {
        {
            std::unique_lock<std::mutex> lock (mlock_work);
    	    while(!is_stop && mUsers.empty()) {
    	        mcond_work.wait(lock);
    	    }
        }
        if(is_stop) return;
        int32_t ret;
    	char temp[PROPERTY_VALUE_MAX] = {0};
    	property_get(PROP_IP, temp, SERVER_IP);
    	if(temp[0]<'0' || temp[0] > '9'){
    	    server_socket = socket_local_server(temp, ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
    	    if (server_socket < 0) {
    	       shutdown(server_socket, SHUT_RDWR);
    	       close(server_socket);
    	       FLOGE("EncoderAudio localsocket server error %s errno: %d", strerror(errno), errno);
    	       usleep(5000000);
    	       continue;
    	    }
    	} else {
            struct sockaddr_in t_sockaddr;
            memset(&t_sockaddr, 0, sizeof(t_sockaddr));
            t_sockaddr.sin_family = AF_INET;
            t_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
            t_sockaddr.sin_port = htons(AUDIO_SERVER_TCP_PORT);
            server_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (server_socket < 0) {
                FLOGE("EncoderAudio socket server error %s errno: %d", strerror(errno), errno);
                shutdown(server_socket, SHUT_RDWR);
                close(server_socket);
                usleep(5000000);
                continue;;
            }
            ret = bind(server_socket,(struct sockaddr *) &t_sockaddr,sizeof(t_sockaddr));
            if (ret < 0) {
                shutdown(server_socket, SHUT_RDWR);
                close(server_socket);
                FLOGE( "EncoderAudio bind %d socket error %s errno: %d", AUDIO_SERVER_TCP_PORT, strerror(errno), errno);
                usleep(5000000);
                continue;;
            }
        }
        ret = listen(server_socket, 1);
        if (ret < 0) {
            shutdown(server_socket, SHUT_RDWR);
            close(server_socket);
            FLOGE("EncoderAudio listen error %s errno: %d", strerror(errno), errno);
            continue;;
        }
        while(!is_stop && !mUsers.empty()){
            int32_t client_socket = accept(server_socket, (struct sockaddr*)NULL, NULL);
            if(client_socket < 0) {
                //shutdown(server_socket, SHUT_RDWR);
                //close(client_socket);
                FLOGE("EncoderAudio accpet socket error: %s errno :%d", strerror(errno), errno);
                usleep(1000000);
                break;
            }
            if(is_stop) return;
            if(mUsers.empty()) break;
            {
                std::lock_guard<std::mutex> lock (mlock_temp);
                temp_clients.push_back(client_socket);
            }
            if(client_t!=nullptr) client_t->join();
            client_t = new std::thread(&EncoderAudio::clientSocket, this);
            {
                std::lock_guard<std::mutex> lock (mlock_client);
                audio_clients.push_back(client_socket);
            }
        }
        shutdown(server_socket, SHUT_RDWR);
        close(server_socket);
    }
    codecRelease();
    FLOGD("EncoderAudio serverSocket exit!");
	return;
}

void EncoderAudio::clientSocket()
{
    FLOGD("EncoderAudio clientSocket start!");
    int32_t socket_fd;
    {
	    std::lock_guard<std::mutex> lock (mlock_temp);
	    socket_fd = temp_clients.back();
	    temp_clients.pop_back();
	}
	unsigned char recvBuf[1024];
    int32_t recvLen;
    bool is_firstclean = false;
	while(!is_stop){
	    if( is_stop || mUsers.empty()){
	        recvLen = recv(socket_fd, recvBuf, 1024, 0);
	        continue;
	    }

        //first clear buffer
	    while(!is_firstclean && !is_stop){
	        recvLen = recv(socket_fd, recvBuf, 1024, 0);
	        if(recvLen < 2){
	            FLOGE("EncoderAudio recv data error! len=%d, errno=%d", recvLen, errno);
	            goto EXIT;
	        }
	        if(recvLen==1024){
                continue;
            }
	        if((recvBuf[recvLen-2]!=(unsigned char)0x7E) && (recvBuf[recvLen-1]!=(unsigned char)0x0D)){
	            continue;
	        }else{
	            is_firstclean = true;
	        }
	    }
	    if(is_stop) goto EXIT;

	    recvLen = recv(socket_fd, recvBuf, 18, 0);
	    if (recvLen < 18 || (recvBuf[0]!=(unsigned char)0x7E) || (recvBuf[1]!=(unsigned char)0xA5)){
	        if(recvLen>=16 && (recvBuf[8]==(unsigned char)0x04) && (recvBuf[9]==(unsigned char)0x52)) {
	            continue;
	        }
	        FLOGE("EncoderAudio recv data error! len&head");
            goto EXIT;
        }

        if ((recvBuf[8]==(unsigned char)0x04) && (recvBuf[9]==(unsigned char)0x4A)){
            recvLen = recv(socket_fd, recvBuf, 4, 0);
            if(recvLen<4) {
                FLOGE("EncoderAudio recv data error! recvLen=%d", recvLen);
                goto EXIT;
            }
            continue;
        }

        if ((recvBuf[8]==(unsigned char)0x04) && (recvBuf[9]==(unsigned char)0x50)){
            recvLen = recv(socket_fd, recvBuf, 6, 0);
            if(recvLen<6) {
                FLOGE("EncoderAudio recv data error! recvLen=%d", recvLen);
                goto EXIT;
            }
            continue;
        }

        if ((recvBuf[8]!=(unsigned char)0x04) || (recvBuf[9]!=(unsigned char)0x4B)){
            recvLen = recv(socket_fd, recvBuf, 1024, 0);
            FLOGE("EncoderAudio recv other audio recvLen=%d", recvLen);
            continue;
        }

        int32_t allLen = ((recvBuf[2]<<24)&0xff000000)+((recvBuf[3]<<16)&0x00ff0000)+((recvBuf[4]<<8)&0x0000ff00)+(recvBuf[5]&0x000000ff);
        int32_t dataSize =((recvBuf[14]<<24)&0xff000000)+((recvBuf[15]<<16)&0x00ff0000)+((recvBuf[16]<<8)&0x0000ff00)+(recvBuf[17]&0x000000ff);

        int32_t sample_rate = ((recvBuf[12]<<8)&0x0000ff00)+(recvBuf[13]&0x000000ff);
        int64_t ch_layout = ((recvBuf[11]>>4)&0x0F)==0x02?AV_CH_LAYOUT_STEREO:AV_CH_LAYOUT_MONO;
        int32_t sample_fmt =(recvBuf[11]&0x0F)==0x02?AV_SAMPLE_FMT_S16:AV_SAMPLE_FMT_U8;

        sp<ABuffer> recvData = new ABuffer(dataSize);
        int32_t recvSize = 0;
        while(recvSize<dataSize && !is_stop){
            recvLen = recv(socket_fd, recvData->data(), dataSize-recvSize, 0);
            if(recvLen==(dataSize-recvSize) && !is_stop  && !mUsers.empty()){
                recvData->setRange(0, dataSize);
                encoderPCMData(recvData, sample_fmt, sample_rate, ch_layout);
                break;
            }else if(recvLen>0){
                recvSize+=recvLen;
                recvData->setRange(recvSize, 0);
            }else{
                break;
            }
        }
        recvLen = recv(socket_fd, recvBuf, 2, 0);
        if (recvLen < 2 || (recvBuf[0]!=(unsigned char)0x7E) || (recvBuf[1]!=(unsigned char)0x0D)){
            FLOGE("EncoderAudio recv data error! foot error");
            goto EXIT;
        }
	}
EXIT:
    clientExit(socket_fd);
	FLOGD("EncoderAudio clientSocket exit!");
	return;
}

void EncoderAudio::clientChecked()
{
    while(!is_stop){
        for(int i=0;i<50;i++){
            usleep(100000);
            if(is_stop) return;
        }
        {
            std::lock_guard<std::mutex> lock (mlock_work);
            std::map<int64_t, int64_t>::iterator it = mUsers.begin();
            while(it != mUsers.end()){
                int32_t lastTime = ((systemTime(SYSTEM_TIME_MONOTONIC) - it->second)/1000000)&0xFFFFFFFF;
                if(lastTime > 60000){
                    it = mUsers.erase(it);
                    FLOGD("EncoderAudio timeout to remove client! size=%zu", mUsers.size());
                }else{
                    it++;
                }
            }
        }
        if(mUsers.empty()){
            shutdown(server_socket, SHUT_RDWR);
            close(server_socket);
            {
                std::lock_guard<std::mutex> lock (mlock_client);
                for (std::list<int32_t>::iterator it = audio_clients.begin(); it != audio_clients.end(); ++it) {
                    int32_t client_socket = (int32_t)*it;
                    shutdown(client_socket, SHUT_RDWR);
                    close(client_socket);
                }
                audio_clients.clear();
            }
        }
    }
    return;
}


void EncoderAudio::codecInit()
{
    mLooper = new ALooper;
    mLooper->setName("EncoderAudio_looper");
    mLooper->start(false);
    mCodec = MediaCodec::CreateByType(mLooper, AUDIO_MIMETYPE, true);
    if (mCodec == nullptr) {
        FLOGE("ERROR: unable to create %s codec instance", AUDIO_MIMETYPE);
        return;
    }
    mLooper->registerHandler(this);
    sp<AMessage> format = new AMessage;
    format->setString("mime", AUDIO_MIMETYPE);
    format->setInt32("aac-profile", OMX_AUDIO_AACObjectLC);
    format->setInt32("channel-count", 2);
    format->setInt32("sample-rate", OUT_SAMPLE_RATE);
    format->setInt32("bitrate", OUT_SAMPLE_RATE * 2 * 2);
    format->setInt32("max-input-size", 64*1024);
    format->setInt32("priority", 0 );
    err = mCodec->configure(format, NULL, NULL, MediaCodec::CONFIGURE_FLAG_ENCODE);
    if (err != OK) {
        FLOGE("codec->configure %s (err=%d)",  AUDIO_MIMETYPE, err);
        mCodec->release();
        return;
    }
    err = mCodec->start();
    if (err != OK) {
        FLOGE("codec->start() (err=%d)", err);
        mCodec->release();
        return;
    }
    err = mCodec->getInputBuffers(&inBuffers);
    if (err != OK) FLOGE("codec->getInputBuffers (err=%d)", err);
    err = mCodec->getOutputBuffers(&outBuffers);
    if (err != OK) FLOGE("codec->getOutputBuffers (err=%d)", err);
}

void EncoderAudio::codecRelease()
{
    if(mCodec!=nullptr){
        mCodec->stop();
        mCodec->release();
        mCodec = nullptr;
    }
    if(mLooper!=nullptr){
        mLooper->unregisterHandler(id());
        mLooper->stop();
        mLooper = nullptr;
    }
}

void EncoderAudio::encoderPCMData(sp<ABuffer> pcmdata, int32_t sample_fmt, int32_t sample_rate, int64_t ch_layout)
{
    int32_t key = ((ch_layout<<24)&0xFF000000)+((sample_fmt<<16)&0x00FF0000)+(sample_rate&0x0000FFFF);
    int64_t ptsUsec = systemTime(SYSTEM_TIME_MONOTONIC) / 1000000;
    struct SwrContext* swr_cxt = NULL;
    auto swr_cxt_find = swr_cxts.find(key);
    if (swr_cxt_find != swr_cxts.end()) {
        swr_cxt = swr_cxt_find->second;
    } else {
        swr_cxt = swr_alloc_set_opts(
        	NULL,
        	OUT_CH_LAYOUT,
        	OUT_SAMPLE_FMT,
        	OUT_SAMPLE_RATE,
        	ch_layout,
        	(AVSampleFormat)sample_fmt,
        	sample_rate,
        	0,
        	NULL);
        if(!swr_cxt) {
            FLOGE("swr_alloc_set_opts failed");
            return;
        }
        if(swr_init(swr_cxt) < 0) {
            FLOGE("swr_init failed");
            return;
        }
        swr_cxts.emplace(key, swr_cxt);
    }
    int64_t in_count = pcmdata->capacity()/(ch_layout==AUDIO_CHANNEL_IN_MONO?2:4);
    int64_t delay = swr_get_delay(swr_cxt, OUT_SAMPLE_RATE);
    int64_t out_count = av_rescale_rnd(
        in_count + delay,
        OUT_SAMPLE_RATE,
        sample_rate,
        AV_ROUND_UP);
    uint8_t *pdata = pcmdata->data();

    int retLen = swr_convert(
        swr_cxt,
        (uint8_t **) &out_buf,
        out_count,
        (const uint8_t **) &pdata,
        in_count);

    if(retLen<=0){
        FLOGE("swr_convert failed, delay=%ld, out_count=%ld, retLen=%d", delay, out_count, retLen);
    }else if(!is_stop){
        //use mediacodec
        size_t inIndex, outIndex, offset, size;
        uint32_t flags;
        ptsUsec = 0;
         //input data
        err = mCodec->dequeueInputBuffer(&inIndex, 1000);
        if(err != OK) {
            FLOGE("codec->dequeueInputBuffer inIdex=%zu, err=%d", inIndex, err);
            return;
        }

        sp<MediaCodecBuffer> inBuffer;
        err = mCodec->getInputBuffer(inIndex, &inBuffer);
        if(err != OK) {
            FLOGE("codec->getInputBuffer inIdex=%zu, err=%d", inIndex, err);
            return;
        }
        inBuffer->setRange(0, retLen * 4);
        memcpy(inBuffer->data(), out_buf,  retLen * 4);
        err = mCodec->queueInputBuffer(inIndex, 0, retLen * 4, ptsUsec, 0);
        if(err != OK) {
            FLOGE("codec->queueInputBuffer inIdex=%zu, err=%d", inIndex, err);
            return;
        }
        //output data
        err = mCodec->dequeueOutputBuffer(&outIndex, &offset, &size, &ptsUsec, &flags, 1000);
        switch (err) {
            case OK:
                if (size != 0) {
                    sequencenumber++;
                    int32_t dataLen = outBuffers[outIndex]->size() + sizeof(AUDIO_DATA);
                    char adata[dataLen];
                    memcpy(adata, AUDIO_DATA, sizeof(AUDIO_DATA));
                    memcpy(adata+sizeof(AUDIO_DATA), outBuffers[outIndex]->data(), outBuffers[outIndex]->size());
                    int32_t size = outBuffers[outIndex]->size() + 16;
                    int32_t ptsUsec = systemTime(SYSTEM_TIME_MONOTONIC) / 1000000;
                    adata[4] = (size & 0xFF000000) >> 24;
                    adata[5] = (size & 0xFF0000) >> 16;
                    adata[6] = (size & 0xFF00) >> 8;
                    adata[7] =  size & 0xFF;
                    memcpy(adata+8,mTerminal.tid,8);
                    adata[16] = (sequencenumber & 0xFF000000) >> 24;
                    adata[17] = (sequencenumber & 0xFF0000) >> 16;
                    adata[18] = (sequencenumber & 0xFF00) >> 8;
                    adata[19] =  sequencenumber & 0xFF;
                    adata[20] = (ptsUsec & 0xFF000000) >> 24;
                    adata[21] = (ptsUsec & 0xFF0000) >> 16;
                    adata[22] = (ptsUsec & 0xFF00) >> 8;
                    adata[23] =  ptsUsec & 0xFF;
                    mManager->updataAsync(adata, sizeof(adata));
                }
                err = mCodec->releaseOutputBuffer(outIndex);
                break;
            case INFO_OUTPUT_BUFFERS_CHANGED:
                FLOGE("EncoderAudio INFO_OUTPUT_BUFFERS_CHANGED");
                err = mCodec->getOutputBuffers(&outBuffers);
                break;
            case -EAGAIN:
                //ALOGV("Got -EAGAIN, looping");
                break;
            default:
                FLOGW("codec->dequeueOutputBuffer err=%d", err);
                break;
        }
    }
}

void EncoderAudio::clientExit(int32_t socket_fd)
{
    shutdown(server_socket, SHUT_RDWR);
    close(socket_fd);
    if(is_stop) return;
    std::lock_guard<std::mutex> lock (mlock_client);
    audio_clients.remove(socket_fd);
    FLOGD("EncoderAudio client exit, client size=[%zu].", audio_clients.size());
}


