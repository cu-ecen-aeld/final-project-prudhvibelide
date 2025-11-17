MUSIC_GPIO_VERSION = 1.0
MUSIC_GPIO_SITE = $(BR2_EXTERNAL_final_project_PATH)/package/music-gpio/src
MUSIC_GPIO_SITE_METHOD = local
MUSIC_GPIO_LICENSE = GPL-2.0
MUSIC_GPIO_LICENSE_FILES =

define MUSIC_GPIO_BUILD_CMDS
	$(MAKE) -C $(LINUX_DIR) \
		M=$(MUSIC_GPIO_SITE) \
		ARCH=$(KERNEL_ARCH) \
		CROSS_COMPILE="$(TARGET_CROSS)" \
		modules

endef

define MUSIC_GPIO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 644 $(MUSIC_GPIO_SITE)/music_input_driver.ko \
		$(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/extra/music_input_driver.ko

endef

$(eval $(kernel-module))
$(eval $(generic-package))


