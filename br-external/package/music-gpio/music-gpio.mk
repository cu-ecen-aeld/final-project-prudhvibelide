################################################################################
# Buildroot package for gpio driver + test app
################################################################################

MUSIC_GPIO_VERSION = 1.0
MUSIC_GPIO_SITE = $(BR2_EXTERNAL_FINAL_PROJECT_PATH)/package/music-gpio/src
MUSIC_GPIO_SITE_METHOD = local

define MUSIC_GPIO_BUILD_CMDS
	$(MAKE) -C $(LINUX_DIR) M=$(@D) modules
	$(TARGET_CC) -O2 -Wall -o $(@D)/music_input_test $(@D)/music_input_test.c
endef

define MUSIC_GPIO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/music_input_driver.ko $(TARGET_DIR)/lib/modules/music_input_driver.ko
	$(INSTALL) -D -m 0755 $(@D)/music_input_test $(TARGET_DIR)/usr/bin/music_input_test
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_FINAL_PROJECT_PATH)/overlay/etc/init.d/S90music-gpio \
		$(TARGET_DIR)/etc/init.d/S90music-gpio
endef

$(eval $(generic-package))
