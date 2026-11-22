EXTRA_DIST = \
	39-usbmuxd.rules.in

if WANT_UDEV

edit = \
	$(SED) -r \
	-e 's|@udev_activation_rule[@]|$(udev_activation_rule)|g' \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	< $< > $@ || rm $@

udevrules_DATA = \
	39-usbmuxd.rules

39-usbmuxd.rules: 39-usbmuxd.rules.in
	$(edit)

MAINTAINERCLEANFILES = \
	39-usbmuxd.rules

CLEANFILES = \
	39-usbmuxd.rules

endif
