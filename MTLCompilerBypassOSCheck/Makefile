ARCHS = arm64 arm64e
INSTALL_TARGET_PROCESSES = MTLCompilerService

include ../config/production.mk
include $(THEOS)/makefiles/common.mk

TWEAK_NAME = MTLCompilerBypassOSCheck

MTLCompilerBypassOSCheck_FILES = Tweak.x
MTLCompilerBypassOSCheck_CFLAGS = -fobjc-arc

include $(THEOS_MAKE_PATH)/tweak.mk

# Theos's vendored CydiaSubstrate.framework installs its name as the rootful
# /Library/Frameworks path, which does not exist on a Dopamine rootless
# device.  libinjector then dlopens this tweak, the substrate dependency fails
# to resolve, and the service silently runs stock (runtime-confirmed
# 2026-10-07: adapter logs absent, request dumps only reappeared after the
# retarget).  Point the load command at the procursus compatibility
# framework, which is a symlink to libellekit and exports MSHookFunction.
after-stage::
	@for dylib in \
		"$(THEOS_STAGING_DIR)/Library/MobileSubstrate/DynamicLibraries/MTLCompilerBypassOSCheck.dylib" \
		"$(THEOS_OBJ_DIR)/MTLCompilerBypassOSCheck.dylib"; do \
		if [ -f "$$dylib" ]; then \
			install_name_tool -change \
				/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate \
				/var/jb/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate \
				"$$dylib"; \
		fi; \
	done
