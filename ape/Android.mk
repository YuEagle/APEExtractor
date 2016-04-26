LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    APEExtractor.cpp \

LOCAL_C_INCLUDES:= \
    $(JNI_H_INCLUDE) \
    $(TOP)/frameworks/av/include/media/stagefright/openmax \
    $(TOP)/frameworks/av/media/libstagefright/include \
    $(TOP)/frameworks/av/media/libstagefright \

LOCAL_PRELINK_MODULE := false
#LOCAL_MODULE_TAGS := eng
LOCAL_MODULE:= libapeextractor

include $(BUILD_STATIC_LIBRARY)
