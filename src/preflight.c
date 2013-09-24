/*
        usbmuxd - iPhone/iPod Touch USB multiplex server daemon

Copyright (C) 2013      Nikias Bassen <nikias@gmx.li>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 or version 3.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>

#include <sys/time.h>

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/notification_proxy.h>

#include "preflight.h"
#include "client.h"
#include "log.h"

#ifdef HAVE_LIBIMOBILEDEVICE
enum connection_type {
	CONNECTION_USBMUXD = 1
};

struct idevice_private {
	char *udid;
	enum connection_type conn_type;
	void *conn_data;
};

struct np_cb_data {
	idevice_t dev;
	np_client_t np;
};

static void set_untrusted_host_buid(lockdownd_client_t lockdown)
{
	char* system_buid = NULL;
	userpref_get_system_buid(&system_buid);
	usbmuxd_log(LL_DEBUG, "%s: Setting UntrustedHostBUID to %s", __func__, system_buid);
	lockdownd_set_value(lockdown, NULL, "UntrustedHostBUID", plist_new_string(system_buid));
	free(system_buid);
}

static void np_callback(const char* notification, void* userdata)
{
	struct np_cb_data *cbdata = (struct np_cb_data*)userdata;
	idevice_t dev = cbdata->dev;
	struct idevice_private *_dev = (struct idevice_private*)dev;

	lockdownd_client_t lockdown = NULL;
	lockdownd_error_t lerr;

	if (strlen(notification) == 0) {
		cbdata->np = NULL;
		return;
	}

	if (strcmp(notification, "com.apple.mobile.lockdown.request_pair") == 0) {
		usbmuxd_log(LL_INFO, "%s: user trusted this computer on device %s, pairing now", __func__, _dev->udid);
		lerr = lockdownd_client_new(dev, &lockdown, "usbmuxd");
		if (lerr != LOCKDOWN_E_SUCCESS) {
			usbmuxd_log(LL_ERROR, "%s: ERROR: Could not connect to lockdownd on device %s, lockdown error %d", __func__, _dev->udid, lerr);
			return;
		}

		lerr = lockdownd_pair(lockdown, NULL);
		if (lerr != LOCKDOWN_E_SUCCESS) {
			usbmuxd_log(LL_ERROR, "%s: ERROR: Pair failed for device %s, lockdown error %d", __func__, _dev->udid, lerr);
			lockdownd_client_free(lockdown);
			return;
		}
		lockdownd_client_free(lockdown);
		// device will reconnect by itself at this point.

	} else if (strcmp(notification, "com.apple.mobile.lockdown.request_host_buid") == 0) {
		lerr = lockdownd_client_new(cbdata->dev, &lockdown, "usbmuxd");
		if (lerr != LOCKDOWN_E_SUCCESS) {
			usbmuxd_log(LL_ERROR, "%s: ERROR: Could not connect to lockdownd on device %s, lockdown error %d", __func__, _dev->udid, lerr);
		} else {
			set_untrusted_host_buid(lockdown);
			lockdownd_client_free(lockdown);
		}
	}
}

static void* preflight_worker_handle_device_add(void* userdata)
{
	struct device_info *info = (struct device_info*)userdata;
	struct idevice_private *_dev = (struct idevice_private*)malloc(sizeof(struct idevice_private));
	_dev->udid = strdup(info->serial);
	_dev->conn_type = CONNECTION_USBMUXD;
	_dev->conn_data = (void*)(long)info->id;

	idevice_t dev = (idevice_t)_dev;

	lockdownd_client_t lockdown = NULL;
	lockdownd_error_t lerr;

retry:
	lerr = lockdownd_client_new(dev, &lockdown, "usbmuxd");
	if (lerr != LOCKDOWN_E_SUCCESS) {
		usbmuxd_log(LL_ERROR, "%s: ERROR: Could not connect to lockdownd on device %s, lockdown error %d", __func__, _dev->udid, lerr);
		goto leave;
	}

	char *type = NULL;
	lerr = lockdownd_query_type(lockdown, &type);
	if (!type) {
		usbmuxd_log(LL_ERROR, "%s: ERROR: Could not get lockdownd type from device %s, lockdown error %d", __func__, _dev->udid, lerr);
		goto leave;
	}

	if (strcmp(type, "com.apple.mobile.lockdown") != 0) {
		// make restore mode devices visible
		client_device_add(info);
		goto leave;
	}

	int is_device_paired = 0;
	char *host_id = NULL;
	userpref_device_record_get_host_id(dev->udid, &host_id);
	lerr = lockdownd_start_session(lockdown, host_id, NULL, NULL);
	free(host_id);
	if (lerr == LOCKDOWN_E_SUCCESS) {
		usbmuxd_log(LL_INFO, "%s: StartSession success for device %s", __func__, _dev->udid);
		client_device_add(info);
		goto leave;
	}

	usbmuxd_log(LL_INFO, "%s: StartSession failed on device %s, lockdown error %d", __func__, _dev->udid, lerr);
	switch (lerr) {
	case LOCKDOWN_E_INVALID_HOST_ID:
		usbmuxd_log(LL_INFO, "%s: Device %s is not paired with this host.", __func__, _dev->udid);
		break;
	case LOCKDOWN_E_SSL_ERROR:
		usbmuxd_log(LL_ERROR, "%s: The stored pair record for device %s is invalid. Removing.", __func__, _dev->udid);
		if (userpref_remove_device_record(_dev->udid) == 0) {
			lockdownd_client_free(lockdown);
			lockdown = NULL;
			goto retry;
		} else {
			usbmuxd_log(LL_ERROR, "%s: Could not remove pair record for device %s\n", __func__, _dev->udid);
		}
		break;
	default:
		is_device_paired = 1;
		break;
	}

	plist_t value = NULL;
	lerr = lockdownd_get_value(lockdown, NULL, "ProductVersion", &value);
	if (lerr != LOCKDOWN_E_SUCCESS) {
		usbmuxd_log(LL_ERROR, "%s: ERROR: Could not get ProductVersion from device %s, lockdown error %d", __func__, _dev->udid, lerr);
		goto leave;
	}

	char* version_str = NULL;
	plist_get_string_val(value, &version_str);
	if (!version_str) {
		usbmuxd_log(LL_ERROR, "%s: Could not get ProductVersion string from device %s handle %d", __func__, _dev->udid, (int)(long)_dev->conn_data);
		goto leave;
	}

	int version_major = strtol(version_str, NULL, 10);
	if (version_major >= 7) {
		/* iOS 7.0 and later */
		usbmuxd_log(LL_INFO, "%s: Found ProductVersion %s device %s", __func__, version_str, _dev->udid);

		set_untrusted_host_buid(lockdown);

		/* if not paired, trigger the trust dialog to make sure it appears */
		if (!is_device_paired) {
			if (lockdownd_pair(lockdown, NULL) == LOCKDOWN_E_SUCCESS) {
				/* if device is still showing the setup screen it will pair even without trust dialog */
				usbmuxd_log(LL_INFO, "%s: Pair success for device %s", __func__, _dev->udid);
				client_device_add(info);
				goto leave;
			}
		}

		lockdownd_service_descriptor_t service = NULL;
		lerr = lockdownd_start_service(lockdown, "com.apple.mobile.insecure_notification_proxy", &service);
		if (lerr != LOCKDOWN_E_SUCCESS) {
			usbmuxd_log(LL_ERROR, "%s: ERROR: Could not start insecure_notification_proxy on %s, lockdown error %d", __func__, _dev->udid, lerr);
			goto leave;
		}

		np_client_t np = NULL;
		np_client_new(dev, service, &np);

		lockdownd_service_descriptor_free(service);
		service = NULL;

		lockdownd_client_free(lockdown);
		lockdown = NULL;

		struct np_cb_data cbdata;
		cbdata.dev = dev;
		cbdata.np = np;

		np_set_notify_callback(np, np_callback, (void*)&cbdata);

		const char* spec[] = {
			"com.apple.mobile.lockdown.request_pair",
			"com.apple.mobile.lockdown.request_host_buid",
			NULL
		}; 
		np_observe_notifications(np, spec);

		usbmuxd_log(LL_INFO, "%s: Waiting for user to trust this computer on device %s", __func__, _dev->udid);
		/* TODO send notification to user's desktop */
		while (cbdata.np) {
			sleep(1);
		}

		if (cbdata.np) {
			np_client_free(cbdata.np);
		}
	} else {
		/* iOS 6.x and earlier */
		lerr = lockdownd_pair(lockdown, NULL);
		if (lerr != LOCKDOWN_E_SUCCESS) {
			if (lerr == LOCKDOWN_E_PASSWORD_PROTECTED) {
				usbmuxd_log(LL_INFO, "%s: Device %s is locked with a passcode. Cannot pair.", __func__, _dev->udid);
				/* TODO send notification to user's desktop */
			} else {
				usbmuxd_log(LL_ERROR, "%s: ERROR: Pair failed for device %s, lockdown error %d", __func__, _dev->udid, lerr);
			}

			/* make device visible anyways */
			client_device_add(info);

			goto leave;
		}

		host_id = NULL;
		userpref_device_record_get_host_id(dev->udid, &host_id);
		lerr = lockdownd_start_session(lockdown, host_id, NULL, NULL);
		free(host_id);
		if (lerr != LOCKDOWN_E_SUCCESS) {
			usbmuxd_log(LL_ERROR, "%s: ERROR StartSession failed on device %s, lockdown error %d", __func__, _dev->udid, lerr);
			goto leave;
		}

		lerr = lockdownd_validate_pair(lockdown, NULL);
		if (lerr != LOCKDOWN_E_SUCCESS) {
			usbmuxd_log(LL_ERROR, "%s: ERROR: ValidatePair failed for device %s, lockdown error %d", __func__, _dev->udid, lerr);
			goto leave;
		}

		/* emit device added event and thus make device visible to clients */
		client_device_add(info);
	}

leave:
	if (lockdown)
		lockdownd_client_free(lockdown);
	if (dev)
		idevice_free(dev);

	free(info);

	return NULL;
}
#endif

void preflight_worker_device_add(struct device_info* info)
{
#ifdef HAVE_LIBIMOBILEDEVICE
	struct device_info *infocopy = (struct device_info*)malloc(sizeof(struct device_info));

	memcpy(infocopy, info, sizeof(struct device_info));

	pthread_t th;
	pthread_create(&th, NULL, preflight_worker_handle_device_add, infocopy);
#else
	client_device_add(info);
#endif
}
