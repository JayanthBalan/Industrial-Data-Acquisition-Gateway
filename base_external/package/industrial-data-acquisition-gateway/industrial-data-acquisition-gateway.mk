
AESD_ASSIGNMENTS_VERSION = 40f55dc8af21693708b642afe383cfe6a78acf5e
AESD_ASSIGNMENTS_SITE = git@github.com:JayanthBalan/Industrial-Data-Acquisition-Gateway.git
AESD_ASSIGNMENTS_SITE_METHOD = git
AESD_ASSIGNMENTS_GIT_SUBMODULES = YES

define AESD_ASSIGNMENTS_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)/builder all
endef

define AESD_ASSIGNMENTS_INSTALL_TARGET_CMDS	
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/var/sensorlog

	$(INSTALL) -m 0755 $(@D)/builder/acquiringd $(TARGET_DIR)/usr/bin/acquiringd
	$(INSTALL) -m 0755 $(@D)/builder/processingd $(TARGET_DIR)/usr/bin/processingd
	$(INSTALL) -m 0755 $(@D)/builder/loggingd $(TARGET_DIR)/usr/bin/loggingd
	$(INSTALL) -m 0755 $(@D)/builder/telemetryd $(TARGET_DIR)/usr/bin/telemetryd

	$(INSTALL) -m 0755 $(@D)/daemons4d-start-stop $(TARGET_DIR)/etc/init.d/S99daemons4d
endef

$(eval $(generic-package))
