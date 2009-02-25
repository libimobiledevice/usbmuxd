Build
=====

  make

Use
===

  sudo ./usbmuxd &
  ./iproxy 2222 22 &
  ssh -p 2222 root@localhost

Muwahahaha.  Hopefully you get the normal SSH login prompt.

  Unfortunately, as of 2009-02-24 this only survives for a single
  connection.  You will have to restart the 'iproxy' part.

SSH
===

If your iphone is rooted, but isn't running SSH, you will need to
mount it with 'ifuse --afc2' (to access the root directory of the
device).

And then edit:

  /Library/LaunchDaemons/com.openssh.sshd.plist

to _remove_ the lines:

  <key>Diabled</key>
  <true/>

Reboot the device and then sshd should be running.
