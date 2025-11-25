MUSIC_GPIO_VERSION = 1.0
MUSIC_GPIO_SITE = $(BR2_EXTERNAL_final_project_PATH)/package/music-gpio/src
MUSIC_GPIO_SITE_METHOD = local
MUSIC_GPIO_LICENSE = GPL-2.0
MUSIC_GPIO_LICENSE_FILES =

define MUSIC_GPIO_BUILD_CMDS
	# Copy source to build directory
	cp $(MUSIC_GPIO_SITE)/*.c $(MUSIC_GPIO_SITE)/*.h $(MUSIC_GPIO_SITE)/Makefile $(@D)/ 2>/dev/null || true
	
	# Build kernel module in build directory
	$(MAKE) -C $(LINUX_DIR) \
		M=$(@D) \
		ARCH=$(KERNEL_ARCH) \
		CROSS_COMPILE="$(TARGET_CROSS)" \
		modules
	
	# Compile device tree overlay
	$(LINUX_DIR)/scripts/dtc/dtc -@ -I dts -O dtb \
		-o $(@D)/music-input.dtbo \
		$(MUSIC_GPIO_SITE)/music-input.dts
endef

define MUSIC_GPIO_INSTALL_TARGET_CMDS
	# Install kernel module
		$(INSTALL) -D -m 644 $(@D)/music_input_driver.ko \
		$(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/extra/music_input_driver.ko
	
	# Install device tree overlay to /boot/overlays/
	$(INSTALL) -D -m 644 $(@D)/music-input.dtbo \
		$(BINARIES_DIR)/rpi-firmware/overlays/music-input.dtbo
endef

$(eval $(kernel-module))
$(eval $(generic-package))
