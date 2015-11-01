#!/system/bin/sh

if [ "$#" == 0 ];then
	echo "Usage: $0 <original boot.img> [eng|user]"
	exit 1
fi

set -e

function cleanup() {
	rm -Rf "$d" "$d2"
}

trap cleanup EXIT

set -e

f="$(readlink -f "$1")"
homedir="$PWD"
scriptdir="$(dirname "$(readlink -f "$0")")"
d="$(mktemp -d)"
cd "$d"

"$scriptdir/bin/bootimg-extract" "$f"
d2="$(mktemp -d)"
cd "$d2"

if [ -f "$d"/ramdisk.gz ];then
	gunzip -c < "$d"/ramdisk.gz |cpio -i
	gunzip -c < "$d"/ramdisk.gz > ramdisk1
else
	echo "Unknown ramdisk format"
	cd "$homedir"
	rm -Rf "$d" "$d2"
	exit 1
fi

#allow <list of scontext> <list of tcontext> <class> <list of perm>
function allow() {
	for s in $1;do
		for t in $2;do
			for p in $4;do
				"$scriptdir"/bin/sepolicy-inject -s $s -t $t -c $3 -p $p -P sepolicy
			done
		done
	done
}

#allowSuClient <scontext>
function allowSuClient() {
	#All domain-s already have read access to rootfs
	allow $1 rootfs file "execute_no_trans execute" #TODO: Why do I need execute?!? (on MTK 5.1, kernel 3.10)
	allow $1 su_daemon unix_stream_socket "connectto getopt"

	allow $1 su_device dir "search read"
	allow $1 su_device sock_file "read write"
	allow su_daemon $1 "fd" "use"

	allow su_daemon $1 fifo_file "read write getattr ioctl"

	#Read /proc/callerpid/cmdline in from_init, drop?
	allow su_daemon "$1" "dir" "search"
	allow su_daemon "$1" "file" "read open"
	allow su_daemon "$1" "lnk_file" "read"
}

#allowLog <scontext>
function allowLog() {
	allow $1 logdw_socket sock_file "write"
	allow $1 logd unix_dgram_socket "sendto"
	allow logd $1 dir "search"
	allow logd $1 file "read open getattr"
}

function suDaemonTo() {
	allow su_daemon $1 "process" "transition"
	allow su_daemon $1 "process" "siginh rlimitinh noatsecure"
}

function suRights() {
	#am start superuser apk
	allow servicemanager $1 "dir" "search read"
	allow servicemanager $1 "file" "open read"
	allow servicemanager $1 "process" "getattr"
	allow servicemanager $1 "binder" "transfer"
}

function suDaemonRights() {
	allow su_daemon rootfs file "entrypoint"

	allow su_daemon su_daemon "dir" "search read"
	allow su_daemon su_daemon "file" "read write open"
	allow su_daemon su_daemon "lnk_file" "read"
	allow su_daemon su_daemon "unix_dgram_socket" "create connect write"
	allow su_daemon su_daemon "unix_stream_socket" "create bind listen accept getopt read write"

	allow su_daemon devpts chr_file "read write open"
	allow su_daemon untrusted_app_devpts chr_file "read write open"

	allow su_daemon su_daemon "capability" "setuid setgid"

	#Access to /data/data/me.phh.superuser/xxx
	allow su_daemon app_data_file "dir" "getattr search write add_name"
	allow su_daemon app_data_file "file" "getattr read open lock"

	#FIXME: This shouldn't exist
	allow su_daemon su_daemon "capability" "dac_override sys_ptrace"

	#I see some forks() before even getting a su call, normal?
	allow su_daemon su_daemon "process" "fork sigchld"

	#toolbox needed for log
	allow su_daemon toolbox_exec "file" "execute read open execute_no_trans"

	#Create /dev/me.phh.superuser. Could be done by init
	allow su_daemon device "dir" "write add_name"
	allow su_daemon su_device "dir" "create setattr remove_name add_name"
	allow su_daemon su_device "sock_file" "create unlink"

	#Allow su daemon to start su apk
	allow su_daemon zygote_exec "file" "execute read open execute_no_trans"

	#Send request to APK
	allow su_daemon su_device dir "search write add_name"

	#Allow su_daemon to switch to su or su_sensitive
	allow su_daemon su_daemon "process" "setexec"

	#Allow su_daemon to execute a shell (every commands are supposed to go through a shell)
	allow su_daemon shell_exec file "execute read open"

	allow su_daemon su_daemon "capability" "chown"

	suDaemonTo su
}

cp "$scriptdir"/bin/su sbin/su
if [ -f "sepolicy" ];then
	#Create domains if they don't exist
	"$scriptdir"/bin/sepolicy-inject -z su -P sepolicy
	"$scriptdir"/bin/sepolicy-inject -z su_device -P sepolicy
	"$scriptdir"/bin/sepolicy-inject -z su_daemon -P sepolicy

	#Autotransition su's socket to su_device
	"$scriptdir"/bin/sepolicy-inject -s su_daemon -f device -c file -t su_device -P sepolicy
	"$scriptdir"/bin/sepolicy-inject -s su_daemon -f device -c dir -t su_device -P sepolicy
	allow su_device tmpfs filesystem "associate"

	#Transition from untrusted_app to su_client
	#TODO: other contexts want access to su?
	allowSuClient shell
	allowSuClient untrusted_app

	#Allow init to execute su daemon/transition
	allow init su_daemon process "transition"
	suDaemonRights

	allowLog su
	suRights su

	#Need to set su_device/su as trusted to be accessible from other categories
	"$scriptdir"/bin/sepolicy-inject -a mlstrustedobject -s su_device -P sepolicy
	"$scriptdir"/bin/sepolicy-inject -a mlstrustedsubject -s su_daemon -P sepolicy
	if [ "$2" == "eng" ];then
		"$scriptdir"/bin/sepolicy-inject -Z su -P sepolicy
	else
		echo "Only eng mode supported yet"
		exit 1
	fi
fi

sed -i -E '/on init/a \\tchmod 0755 /sbin' init.rc
echo -e 'service su /sbin/su --daemon\n\tclass main\n\tseclabel u:r:su_daemon:s0\n' >> init.rc

echo -e 'sbin/su\ninit.rc\nsepolicy\nfile_contexts' | cpio -o -H newc > ramdisk2

if [ -f "$d"/ramdisk.gz ];then
	#TODO: Why can't I recreate initramfs from scratch?
	#Instead I use the append method. files gets overwritten by the last version if they appear twice
	#Hence sepolicy/su/init.rc are our version
	cat ramdisk1 ramdisk2 |gzip -9 -c > "$d"/ramdisk.gz
fi
cd "$d"
rm -Rf "$d2"
"$scriptdir/bin/bootimg-repack" "$f"
cp new-boot.img "$homedir"

cd "$homedir"
rm -Rf "$d"
