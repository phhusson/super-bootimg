my_path := $(call my-dir)

LOCAL_PATH := $(my_path)

include $(CLEAR_VARS)
LOCAL_MODULE := bootimg-extract
LOCAL_MODULE_TAGS := optional
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_STATIC_LIBRARIES := libc libcutils
LOCAL_SRC_FILES := extract.c
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := bootimg-repack
LOCAL_MODULE_TAGS := optional
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_STATIC_LIBRARIES := libc libcutils
LOCAL_SRC_FILES := repack.c
include $(BUILD_EXECUTABLE)
