MUSIC_DAEMON_VERSION = 1.0
MUSIC_DAEMON_SITE = $(BR2_EXTERNAL_final_project_PATH)/package/music-daemon/src
MUSIC_DAEMON_SITE_METHOD = local
MUSIC_DAEMON_LICENSE = MIT

define MUSIC_DAEMON_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) \
		-o $(@D)/music_daemon \
		$(MUSIC_DAEMON_SITE)/music_daemon.c
endef

define MUSIC_DAEMON_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 755 $(@D)/music_daemon \
		$(TARGET_DIR)/usr/bin/music_daemon
endef

$(eval $(generic-package))

