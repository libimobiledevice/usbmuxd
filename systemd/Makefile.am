EXTRA_DIST = \
	usbmuxd.service.in

if WANT_SYSTEMD

edit = \
	$(SED) -r \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@runstatedir[@]|$(runstatedir)|g' \
	< $< > $@ || rm $@

systemdsystemunit_DATA = \
	usbmuxd.service

usbmuxd.service: usbmuxd.service.in
	$(edit)

CLEANFILES = \
	usbmuxd.service

endif
