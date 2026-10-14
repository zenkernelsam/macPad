TARGET := macosx:clang:latest:13.0
ARCHS = arm64

include ../config/production.mk
include $(THEOS)/makefiles/common.mk

TOOL_NAME = macwstccdprobe

macwstccdprobe_FILES = main.c
macwstccdprobe_CFLAGS = -std=c11 -Wall -Wextra -Werror
macwstccdprobe_LDFLAGS = -framework CoreFoundation
macwstccdprobe_CODESIGN_FLAGS = -S../layout/usr/macOS/bin/entitlements.plist
macwstccdprobe_INSTALL_PATH = /usr/macOS/bin

include $(THEOS_MAKE_PATH)/tool.mk
