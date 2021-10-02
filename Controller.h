//
// Created by FlyZebra on 2021/8/9 0009.
//

#ifndef ANDROID_CONTROLLER_H
#define ANDROID_CONTROLLER_H

#include <binder/IPCThreadState.h>
#include <media/stagefright/foundation/AHandler.h>
#include <media/stagefright/foundation/AMessage.h>

namespace android {

class Controller : public AHandler {
public:
    Controller();
    ~Controller();
    void start();
    void stop();
protected:
    virtual void onMessageReceived(const sp<AMessage> &msg);
private:
    static void *_controller_socket(void *arg);
    static void *_controller_client_socket(void *arg);
    void handleClientSocket(const sp<AMessage> &msg);
    void handleSocketRecvData(const sp<AMessage> &msg);
    void handleClientSocketExit(const sp<AMessage> &msg);

    void input_key(int32_t fd, int32_t key);
    void input_touch(int32_t fd, int32_t x, int32_t y, int32_t action);

    struct client_conn {
          int32_t socket;
          int32_t status;
    };

    Mutex mLock;
    std::vector<int32_t> thread_sockets;
    std::vector<client_conn> conn_sockets;
    volatile bool is_stop;

    pthread_t init_socket_tid;
    //int32_t key_fd;
    //int32_t ts_fd;
    int32_t server_socket;
};

}; // namespace android

#endif //ANDROID_CONTROLLER_H
