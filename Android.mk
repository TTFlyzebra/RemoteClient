# Copyright 2013 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    screenrecord.cpp \
    EglWindow.cpp \
    FrameOutput.cpp \
    TextRenderer.cpp \
    Overlay.cpp \
    Program.cpp \
    mobilectl.cpp \
	TerminalSession.cpp \
    ServerManager.cpp \
    VideoEncoder.cpp \
    AudioEncoder.cpp \
    RtspServer.cpp \
    RtspClient.cpp \
    Base64.cpp \
    Controller.cpp \

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
