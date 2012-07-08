LOCAL_PATH:= $(call my-dir)

#
# omapupdater
#
include $(CLEAR_VARS)
LOCAL_SRC_FILES:= omapupdater.cpp
LOCAL_MODULE:= omapupdater
LOCAL_SHARED_LIBRARIES:= libc libcutils
LOCAL_MODULE_TAGS:= optional
include $(BUILD_EXECUTABLE)
