edit = \
	$(SED) -r \
	-e 's|@udev_activation_rule[@]|$(udev_activation_rule)|g' \
	-e 's|@udev_deactivation_rule[@]|$(udev_deactivation_rule)|g' \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@rcservicedir[@]|$(rcservicedir)|g' \
	< $< > $@ || rm $@

udevrules_DATA = \
	39-usbmuxd.rules

39-usbmuxd.rules: 39-usbmuxd.rules.in
	$(edit)

EXTRA_DIST = \
	39-usbmuxd.rules.in

MAINTAINERCLEANFILES = \
	39-usbmuxd.rules

CLEANFILES = \
	39-usbmuxd.rules
