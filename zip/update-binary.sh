#!/sbin/sh

set -e
set -x

fd=$2
if [ ! -L /proc/self/fd/$fd ];then
	#We were given a non-fd
	#This means we are called from another update.zip we need to find out the fd by ourselves

	#Find the outfd of my parent
	mypid=$$
	ppid="$(grep PPid /proc/$mypid/status |grep -oE '[0-9]+')"
	parent_fd="$(tr '\0' ';' < /proc/$ppid/cmdline  |cut -d \; -f 3)"

	#Assume stupid run_program() which doesn't change FDs
	fd=$parent_fd
fi
zip="$3"

ui_print() {
	echo "ui_print $1" >> /proc/self/fd/$fd
}


rm -Rf /tmp/superuser
mkdir -p /tmp/superuser
unzip -o "$3" -d /tmp/superuser/
modes="$(cat /tmp/superuser/config.txt)"
cd /tmp/superuser/scripts/su/
fstab="/etc/recovery.fstab"
[ ! -f "$fstab" ] && fstab="/etc/recovery.fstab.bak"
bootimg="$(grep -E '\b/boot\b' "$fstab" |grep -oE '/dev/[a-zA-Z0-9_./-]*')"
if [ -z "$bootimg" ];then
	ui_print "Couldn't find boot.img partition"
	exit 1
fi

ui_print "Found bootimg @ $bootimg"
sh -x ../bootimg.sh $bootimg $modes
ui_print "Generated $pwd/new-boot.img"
dd if=new-boot.img of=$bootimg bs=8192
ui_print "Flashed root-ed boot.img"
