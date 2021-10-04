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
#include "FlyLog.h"
#include "HandlerEvent.h"
#include "Command.h"
#include "Config.h"

using namespace android;

EncoderAudio::EncoderAudio(ServerManager* manager)
:mManager(manager)
,is_stop(false)
,is_running(false)
,is_codec(false)
,out_buf((uint8_t *)av_malloc(OUT_SAMPLE_RATE))
,mClientNums(0)
{
    mManager->registerListener(this);
    server_t = new std::thread(&EncoderAudio::serverSocket, this);
}

EncoderAudio::~EncoderAudio()
{
    is_stop = true;
    mManager->unRegisterListener(this);
    std::map<int32_t, struct SwrContext*>::iterator it;
    for(it=swr_cxts.begin();it!=swr_cxts.end();++it){
        if (it->second) swr_free(&it->second);
    }
    swr_cxts.clear();
    if (out_buf) av_free(out_buf);
    close(server_socket);
    server_t->join();
    delete server_t;
}

int32_t EncoderAudio::notify(const char* data, int32_t size)
{
    struct NotifyData* notifyData = (struct NotifyData*)data;
    switch (notifyData->type){
    case 0x0102:
        if(mClientNums<=1) codecInit(); 
        return -1;
    case 0x0202:
        if(mClientNums<=0)codecRelease();
        return -1;
    }
    return -1;
}

void EncoderAudio::onMessageReceived(const sp<AMessage> &msg)
{

}

void EncoderAudio::serverSocket()
{
	FLOGD("EncoderAudio serverSocket start!");
	int32_t ret;
	char temp[PROPERTY_VALUE_MAX] = {0};
	property_get(PROP_IP, temp, SERVER_IP);
	if(temp[0]<'0' || temp[0] > '9'){
	    server_socket = socket_local_server(temp, ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
	    if (server_socket < 0) {
	       FLOGE("EncoderAudio localsocket server error %s errno: %d", strerror(errno), errno);
	       return;
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
            return;
        }
        ret = bind(server_socket,(struct sockaddr *) &t_sockaddr,sizeof(t_sockaddr));
        if (ret < 0) {
            FLOGE( "EncoderAudio bind %d socket error %s errno: %d", AUDIO_SERVER_TCP_PORT, strerror(errno), errno);
            return;
        }
    }
    ret = listen(server_socket, 5);
    if (ret < 0) {
        FLOGE("EncoderAudio listen error %s errno: %d", strerror(errno), errno);
        return;
    }
    while(!is_stop) {
        int32_t client_socket = accept(server_socket, (struct sockaddr*)NULL, NULL);
        if(client_socket < 0) {
            FLOGE("EncoderAudio accpet socket error: %s errno :%d", strerror(errno), errno);
            continue;
        }
        if(is_stop) break;
        {
            std::lock_guard<std::mutex> lock (mlock_client);
            client_t = new std::thread(&EncoderAudio::clientSocket, this);
            thread_sockets.push_back(client_socket);
            client_t->detach();
        }
    }
    
    close(server_socket);
    FLOGD("EncoderAudio serverSocket exit!");
	return;
}

void EncoderAudio::clientSocket()
{
    FLOGD("EncoderAudio clientSocket start!");
    int32_t socket_fd;
    {
	    std::lock_guard<std::mutex> lock (mlock_client);
	    socket_fd = thread_sockets.back();
	    thread_sockets.pop_back();
	}
	unsigned char recvBuf[4096];
    int32_t recvLen;
	while(!is_stop){
	    if(!is_stop  && is_codec){
	        recvLen = recv(socket_fd, recvBuf, 18, 0);
	    }else{
	        recvLen = recv(socket_fd, recvBuf, 4096, 0);
	        continue;
	    }
	    if (recvLen < 18 || (recvBuf[0]!=(unsigned char)0x7E) || (recvBuf[1]!=(unsigned char)0xA5)){
	        if(recvLen>=16 && (recvBuf[8]==(unsigned char)0x04) && (recvBuf[9]==(unsigned char)0x52)) {
	            continue;
	        }
            goto audio_exit;
        }

        if ((recvBuf[8]==(unsigned char)0x04) && (recvBuf[9]==(unsigned char)0x4A)){
            recvLen = recv(socket_fd, recvBuf, 4, 0);
            if(recvLen<4) goto audio_exit;
            continue;
        }

        if ((recvBuf[8]==(unsigned char)0x04) && (recvBuf[9]==(unsigned char)0x50)){
            recvLen = recv(socket_fd, recvBuf, 6, 0);
            if(recvLen<6) goto audio_exit;
            continue;
        }

        if ((recvBuf[8]!=(unsigned char)0x04) || (recvBuf[9]!=(unsigned char)0x4B)){
            recvLen = recv(socket_fd, recvBuf, 4096, 0);
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
            if(recvLen==(dataSize-recvSize) && !is_stop  && is_codec){
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
            goto audio_exit;
        }
	}
audio_exit:
    clientExit(socket_fd);
    close(socket_fd);
	FLOGD("EncoderAudio clientSocket exit!");
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
    
    is_codec = true;
}

void EncoderAudio::codecRelease()
{
    is_codec = false;
    
    if(mCodec!=nullptr){
        mCodec->stop();
        mCodec->release();
        mCodec = nullptr;
    }
    if(mLooper!=nullptr){
        mLooper->unregisterHandler(id());
        mLooper->stop();
    }
}

void EncoderAudio::encoderPCMData(sp<ABuffer> pcmdata,      int32_t sample_fmt, int32_t sample_rate, int64_t ch_layout)
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
                    int32_t dataLen = outBuffers[outIndex]->size() + sizeof(audiodata);
                    char adata[dataLen];
                    memcpy(adata, audiodata, sizeof(audiodata));
                    memcpy(adata+sizeof(audiodata), outBuffers[outIndex]->data(), outBuffers[outIndex]->size());
                    int32_t size = outBuffers[outIndex]->size() + 12;
                    int32_t ptsUsec = systemTime(SYSTEM_TIME_MONOTONIC) / 1000000;
                    adata[6] = (size & 0xFF000000) >> 24;
                    adata[7] = (size & 0xFF0000) >> 16;
                    adata[8] = (size & 0xFF00) >> 8;
                    adata[9] =  size & 0xFF;
                    adata[18] = (ptsUsec & 0xFF000000) >> 24;
                    adata[19] = (ptsUsec & 0xFF0000) >> 16;
                    adata[20] = (ptsUsec & 0xFF00) >> 8;
                    adata[21] =  ptsUsec & 0xFF;
                    std::lock_guard<std::mutex> lock (mManager->mlock_up);
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
    int32_t size = conn_sockets.empty()?0:((int)conn_sockets.size());
    for(int32_t i=0; i<size; i++){
        if(conn_sockets[i].socket == socket_fd){
            conn_sockets.erase(conn_sockets.begin()+i);
            break;
        }
    }
    FLOGD("EncoderAudio conn_sockets size=%d.", conn_sockets.empty()?0:((int)conn_sockets.size()));
}