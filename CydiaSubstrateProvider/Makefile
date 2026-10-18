TARGET := macosx:clang:latest:13.0
ARCHS = arm64 arm64e
include ../config/production.mk
include $(THEOS)/makefiles/common.mk

# Theos uses the instance name verbatim as the output basename and appends
# TARGET_LIB_EXT unless _LIBRARY_EXTENSION is '-'.  Do not rename a built
# dylib by hand: this package stages the exact framework binary name that
# dyld loads.  See theos/makefiles/instance/library.mk ($(_LOCAL_LIBRARY_EXTENSION)).
LIBRARY_NAME = CydiaSubstrate

CydiaSubstrate_FILES = ../misc/cydiasubstrate_macos.m
CydiaSubstrate_CFLAGS = -I$(CURDIR)/../include
# The package-owned staging path is /usr/macOS/Frameworks/..., but the chroot
# dyld path is /System/Library/Frameworks/....  Keep LC_ID_DYLIB aligned with
# the identity the postinstall transaction publishes and verifies.
CydiaSubstrate_LDFLAGS = -Wl,-install_name,/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate
CydiaSubstrate_LIBRARY_EXTENSION = -
CydiaSubstrate_INSTALL_PATH = /usr/macOS/Frameworks/CydiaSubstrate.framework

include $(THEOS_MAKE_PATH)/library.mk
