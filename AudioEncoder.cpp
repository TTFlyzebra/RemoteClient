//
// Created by FlyZebra on 2021/8/12 0012.
//
#include <errno.h>
#include <sys/socket.h>
#include <cutils/sockets.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cutils/properties.h>

#include "AudioEncoder.h"
#include "FlyLog.h"
#include "HandlerEvent.h"

using namespace android;

AudioEncoder::AudioEncoder(sp<AMessage> notify)
:mNotify(notify),
is_stop(false),
is_running(false),
is_codec(false),
out_buf((uint8_t *)av_malloc(OUT_SAMPLE_RATE))
{
}

AudioEncoder::~AudioEncoder()
{
    std::map<int32_t, struct SwrContext*>::iterator it;
    for(it=swr_cxts.begin();it!=swr_cxts.end();++it){
        if (it->second) swr_free(&it->second);
    }
    swr_cxts.clear();
    if (out_buf) av_free(out_buf);
}

void AudioEncoder::onMessageReceived(const sp<AMessage> &msg)
{
    switch (msg->what()) {
   	    case kWhatClientSocketExit:
   	        handleClientExit(msg);
   	        break;
   }

}

void AudioEncoder::codecInit()
{
    mLooper = new ALooper;
    mLooper->setName("AudioEncoder_looper");
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

void AudioEncoder::codecRelease()
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

void AudioEncoder::start()
{
    FLOGD("AudioEncoder::startRecord()");
    if(is_running) {
        FLOGE("AudioEncoder is running, exit!");
        return;
    }
    is_stop = false;
    is_running = true;
    int32_t ret = pthread_create(&init_socket_tid, nullptr, _audio_socket, (void *) this);
    if (ret != 0) {
    	FLOGE("create audio socket thread error!");
    }
}

void AudioEncoder::stop()
{
    is_stop = true;
    codecRelease();
    if(server_socket >= 0){
        close(server_socket);
        server_socket = -1;
        //try connect once for exit accept block
        int32_t socket_temp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in servaddr;
        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(AUDIO_SERVER_TCP_PORT);
        servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        connect(socket_temp, (struct sockaddr *) &servaddr, sizeof(servaddr));
        close(socket_temp);
    }else{
       close(server_socket);
       server_socket = -1;
    }
    pthread_join(init_socket_tid, nullptr);
    is_running = false;
    FLOGD("AudioEncoder::stopRecord()");
}

void AudioEncoder::startRecord()
{
    FLOGD("AudioEncoder::startRecord()");
    if(!is_codec) codecInit();
}

void AudioEncoder::stopRecord()
{
    FLOGD("AudioEncoder::stopRecord()");
    codecRelease();
}

void *AudioEncoder::_audio_socket(void *argv)
{
	FLOGD("AudioEncoder audio_socket start!");
	auto *p=(AudioEncoder *)argv;

	int32_t ret;
	char temp[PROPERTY_VALUE_MAX] = {0};
	property_get(PROP_IP, temp, SERVER_IP);
	if(temp[0]<'0' || temp[0] > '9'){
	    p->server_socket = socket_local_server(temp, ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
	    if (p->server_socket < 0) {
	       FLOGE("AudioEncoder localsocket server error %s errno: %d", strerror(errno), errno);
	       return 0;
	    }
	} else {
        struct sockaddr_in t_sockaddr;
        memset(&t_sockaddr, 0, sizeof(t_sockaddr));
        t_sockaddr.sin_family = AF_INET;
        t_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
        t_sockaddr.sin_port = htons(AUDIO_SERVER_TCP_PORT);
        p->server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (p->server_socket < 0) {
            FLOGE("AudioEncoder socket server error %s errno: %d", strerror(errno), errno);
            return 0;
        }
        ret = bind(p->server_socket,(struct sockaddr *) &t_sockaddr,sizeof(t_sockaddr));
        if (ret < 0) {
            FLOGE( "AudioEncoder bind %d socket error %s errno: %d", AUDIO_SERVER_TCP_PORT, strerror(errno), errno);
            return 0;
        }
    }
    ret = listen(p->server_socket, 5);
    if (ret < 0) {
        FLOGE("AudioEncoder listen error %s errno: %d", strerror(errno), errno);
    }
    while(!p->is_stop) {
        int32_t client_socket = accept(p->server_socket, (struct sockaddr*)NULL, NULL);
        if(client_socket < 0) {
            FLOGE("AudioEncoder accpet socket error: %s errno :%d", strerror(errno), errno);
            continue;
        }
        if(p->is_stop) break;
        {
            Mutex::Autolock autoLock(p->mLock);
            p->thread_sockets.push_back(client_socket);
            pthread_t client_socket_tid;
            int32_t ret = pthread_create(&client_socket_tid, nullptr, _audio_client_socket, (void *)argv);
            pthread_detach(client_socket_tid);
            if (ret != 0) {
            	FLOGE("AudioEncoder create audio client socket thread error!");
            	p->thread_sockets.pop_back();
            }
        }
    }
    if(p->server_socket >= 0){
        close(p->server_socket);
        p->server_socket = -1;
    }

    p->is_running = false;

    FLOGD("AudioEncoder audio_socket exit!");
	return 0;
}

void *AudioEncoder::_audio_client_socket(void *argv)
{
    FLOGD("AudioEncoder audio_client_socket start!");
    signal(SIGPIPE, SIG_IGN);
    auto *p=(AudioEncoder *)argv;
    int32_t socket_fd;
    {
	    Mutex::Autolock autoLock(p->mLock);
	    socket_fd = p->thread_sockets.back();
	    p->thread_sockets.pop_back();
	}
	unsigned char recvBuf[4096];
    int32_t recvLen;
	while(!p->is_stop){
	    if(!p->is_stop  && p->is_codec){
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
            FLOGE("AudioEncoder recv other audio recvLen=%d", recvLen);
            continue;
        }

        int32_t allLen = ((recvBuf[2]<<24)&0xff000000)+((recvBuf[3]<<16)&0x00ff0000)+((recvBuf[4]<<8)&0x0000ff00)+(recvBuf[5]&0x000000ff);
        int32_t dataSize =((recvBuf[14]<<24)&0xff000000)+((recvBuf[15]<<16)&0x00ff0000)+((recvBuf[16]<<8)&0x0000ff00)+(recvBuf[17]&0x000000ff);

        int32_t sample_rate = ((recvBuf[12]<<8)&0x0000ff00)+(recvBuf[13]&0x000000ff);
        int64_t ch_layout = ((recvBuf[11]>>4)&0x0F)==0x02?AV_CH_LAYOUT_STEREO:AV_CH_LAYOUT_MONO;
        int32_t sample_fmt =(recvBuf[11]&0x0F)==0x02?AV_SAMPLE_FMT_S16:AV_SAMPLE_FMT_U8;

        sp<ABuffer> recvData = new ABuffer(dataSize);
        int32_t recvSize = 0;
        while(recvSize<dataSize && !p->is_stop){
            recvLen = recv(socket_fd, recvData->data(), dataSize-recvSize, 0);
            if(recvLen==(dataSize-recvSize) && !p->is_stop  && p->is_codec){
                sp<AMessage> msg = new AMessage(kWhatSocketRecvData, (AudioEncoder *) argv);
                recvData->setRange(0, dataSize);
                msg->setBuffer("data", recvData);
                msg->setInt32("socket", socket_fd);
                msg->setInt32("sample_rate", sample_rate);
                msg->setInt64("ch_layout", ch_layout);
                msg->setInt32("sample_fmt", sample_fmt);
                msg->setInt64("ptsUsec", systemTime(SYSTEM_TIME_MONOTONIC) / 1000000);
                p->handleRecvPCMData(msg);
                //msg->post();
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
    sp<AMessage> msg = new AMessage(kWhatClientSocketExit, (AudioEncoder *) argv);
    msg->setInt32("socket", socket_fd);
    msg->post();
    close(socket_fd);
	FLOGD("AudioEncoder audio_client_socket exit!");
	return 0;
}

void AudioEncoder::handleRecvPCMData(const sp<AMessage> &msg)
{
    int32_t socket_fd;
    CHECK(msg->findInt32("socket", &socket_fd));
	sp<ABuffer> pcmdata;
    CHECK(msg->findBuffer("data", &pcmdata));
    int64_t ch_layout;
    CHECK(msg->findInt64("ch_layout", &ch_layout));
    int32_t sample_fmt;
    CHECK(msg->findInt32("sample_fmt", &sample_fmt));
    int32_t sample_rate;
    CHECK(msg->findInt32("sample_rate", &sample_rate));
    int64_t ptsUsec = 0;
    CHECK(msg->findInt64("ptsUsec", &ptsUsec));

    int32_t key = ((ch_layout<<24)&0xFF000000)+((sample_fmt<<16)&0x00FF0000)+(sample_rate&0x0000FFFF);
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
        return;
    }else if(!is_stop){
        //use mediacodec
        size_t inIndex, outIndex, offset, size;
        uint32_t flags;
         //input data
        err = mCodec->dequeueInputBuffer(&inIndex, 2000);
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
        err = mCodec->dequeueOutputBuffer(&outIndex, &offset, &size, &ptsUsec, &flags, 2000);
        switch (err) {
            case OK:
                if (size != 0) {
                    sp<ABuffer> buffer = ABuffer::CreateAsCopy(outBuffers[outIndex]->data(), outBuffers[outIndex]->size());
                    sp<AMessage> notify = mNotify->dup();
                    notify->setInt32("type", kWhatAudioFrameData);
                    notify->setInt64("ptsUsec", systemTime(SYSTEM_TIME_MONOTONIC) / 1000000);
                    notify->setBuffer("data", buffer);
                    notify->post();
                }
                err = mCodec->releaseOutputBuffer(outIndex);
                break;
            case INFO_OUTPUT_BUFFERS_CHANGED:
                FLOGE("AudioEncoder INFO_OUTPUT_BUFFERS_CHANGED");
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

void AudioEncoder::handleClientExit(const sp<AMessage> &msg)
{
    int32_t socket_fd;
    CHECK(msg->findInt32("socket", &socket_fd));
    int32_t size = conn_sockets.empty()?0:((int)conn_sockets.size());
    for(int32_t i=0; i<size; i++){
        if(conn_sockets[i].socket == socket_fd){
            conn_sockets.erase(conn_sockets.begin()+i);
            break;
        }
    }
    FLOGD("AudioEncoder conn_sockets size=%d.", conn_sockets.empty()?0:((int)conn_sockets.size()));
}