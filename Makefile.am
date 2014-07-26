AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src $(UDEV_SUB) $(SYSTEMD_SUB)

DISTCHECK_CONFIGURE_FLAGS =				\
	--with-udevrulesdir=$$dc_install_base/$(udevrulesdir) \
	--with-systemdsystemunitdir=$$dc_install_base/$(systemdsystemunitdir)