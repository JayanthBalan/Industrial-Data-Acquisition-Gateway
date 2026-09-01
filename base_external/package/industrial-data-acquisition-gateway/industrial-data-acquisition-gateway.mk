
INDUSTRIAL_DATA_ACQUISITION_GATEWAY_VERSION = 9ab1aa913da2049b605251217aacbd782d3fd616
INDUSTRIAL_DATA_ACQUISITION_GATEWAY_SITE = git@github.com:JayanthBalan/Industrial-Data-Acquisition-Gateway.git
INDUSTRIAL_DATA_ACQUISITION_GATEWAY_SITE_METHOD = git
INDUSTRIAL_DATA_ACQUISITION_GATEWAY_GIT_SUBMODULES = YES

define INDUSTRIAL_DATA_ACQUISITION_GATEWAY_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)/builder all
endef

define INDUSTRIAL_DATA_ACQUISITION_GATEWAY_INSTALL_TARGET_CMDS	
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/var/sensorlog

	$(INSTALL) -m 0755 $(@D)/builder/acquiringd $(TARGET_DIR)/usr/bin/acquiringd
	$(INSTALL) -m 0755 $(@D)/builder/processingd $(TARGET_DIR)/usr/bin/processingd
	$(INSTALL) -m 0755 $(@D)/builder/loggingd $(TARGET_DIR)/usr/bin/loggingd
	$(INSTALL) -m 0755 $(@D)/builder/telemetryd $(TARGET_DIR)/usr/bin/telemetryd

	$(INSTALL) -m 0755 $(@D)/daemons4d-start-stop $(TARGET_DIR)/etc/init.d/S99daemons4d
endef

$(eval $(generic-package))
