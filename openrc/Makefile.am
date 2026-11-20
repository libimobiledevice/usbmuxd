if WANT_OPENRC
edit = \
    $(SED) -r \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	< $< > $@ || rm $@

rcservice_SCRIPTS = usbmuxd

usbmuxd: usbmuxd.in
	$(edit)

EXTRA_DIST = \
	usbmuxd.in

MAINTAINERCLEANFILES = \
	usbmuxd

CLEANFILES = \
	usbmuxd
endif
