#!/system/bin/sh

if mount | grep tmpfs | grep /sbin > /dev/null; then
	echo Mount on /sbin already exists
else
	mkdir /dev/sbin.tmp
	mount tmpfs /dev/sbin.tmp -t tmpfs -o size=64m

	cp -a /sbin/* /dev/sbin.tmp/
	cp /root/su /dev/sbin.tmp/
	chmod 755 /dev/sbin.tmp/su
	chcon "u:object_r:rootfs:s0" /dev/sbin.tmp
	chcon -h "u:object_r:rootfs:s0" /dev/sbin.tmp/*

	mount /dev/sbin.tmp /sbin -o bind
	umount /dev/sbin.tmp
	rmdir /dev/sbin.tmp
	mount /sbin -o remount,ro
fi

/root/hidesu
