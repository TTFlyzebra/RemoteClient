LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
    main.cpp \
    Base64.cpp \
    Global.cpp \
    ServerManager.cpp \
    remotecore/TerminalSession.cpp \
    remotecore/EncoderVideo.cpp \
    remotecore/EncoderAudio.cpp \
    remotecore/RtspServer.cpp \
    remotecore/RtspClient.cpp \
    remotecore/InputServer.cpp \
    remotecore/InputClient.cpp \
    screenrecord/screenrecord.cpp \
    screenrecord/EglWindow.cpp \
    screenrecord/FrameOutput.cpp \
    screenrecord/TextRenderer.cpp \
    screenrecord/Overlay.cpp \
    screenrecord/Program.cpp \
    zebraservice/android/zebra/IZebraService.aidl \
    zebraservice/ZebraService.cpp \

LOCAL_SHARED_LIBRARIES := \
	libstagefright \
	libmedia \
	libmedia_omx \
	libutils \
	libbinder \
	libstagefright_foundation \
	libjpeg \
	libui \
	libgui \
	libcutils \
	liblog \
	libEGL \
	libGLESv2 \
	libavcodec-57 \
	libavdevice-57 \
	libavfilter-6 \
	libavformat-57 \
	libavutil-55 \
	libpostproc-54 \
	libswresample-2 \
	libswscale-4

LOCAL_C_INCLUDES := \
	frameworks/av/media/libstagefright \
	frameworks/av/media/libstagefright/include \
	frameworks/native/include/media/openmax \
	vendor/zebra/library/ffmpeg/include

LOCAL_CFLAGS :=  \
    -Werror -Wall \
    -Wno-multichar \
    -Wno-unused-parameter \
    -Wno-unused-variable \
    -Wno-unused-function

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE:= mctl
include $(BUILD_EXECUTABLE)
