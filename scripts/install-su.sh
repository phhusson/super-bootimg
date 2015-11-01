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
	allow $1 su unix_stream_socket "connectto getopt"

	allow $1 su_device dir "search read"
	allow $1 su_device sock_file "read write"
}

#allowLog <scontext>
function allowLog() {
	allow $1 logdw_socket sock_file "write"
	allow $1 logd unix_dgram_socket "sendto"
	allow logd $1 dir "search"
	allow logd $1 file "read open getattr"
}

cp "$scriptdir"/bin/su sbin/su
if [ -f "sepolicy" ];then
	#Create domains if they don't exist
	"$scriptdir"/bin/sepolicy-inject -z su -P sepolicy
	"$scriptdir"/bin/sepolicy-inject -z su_device -P sepolicy

	#Autotransition su's socket to su_device
	"$scriptdir"/bin/sepolicy-inject -s su -f device -c file -t su_device -P sepolicy
	"$scriptdir"/bin/sepolicy-inject -s su -f device -c dir -t su_device -P sepolicy
	allow su_device tmpfs filesystem "associate"

	#Transition from untrusted_app to su_client
	#TODO: other contexts want access to su?
	allowSuClient shell
	allowSuClient untrusted_app

	#Allow init to execute su daemon/transition
	allow init su process "transition"

	allowLog su

	#Need to set su_device/su as trusted to be accessible from other categories
	"$scriptdir"/bin/sepolicy-inject -a mlstrustedobject -s su_device -P sepolicy
	"$scriptdir"/bin/sepolicy-inject -a mlstrustedsubject -s su  -P sepolicy
	if [ "$2" == "eng" ];then
		"$scriptdir"/bin/sepolicy-inject -Z su -P sepolicy
	else
		echo "Only eng mode supported yet"
		exit 1
	fi
fi

sed -i -E '/on init/a \\tchmod 0755 /sbin' init.rc
echo -e 'service su /sbin/su --daemon\n\tclass main\n\tseclabel u:r:su:s0\n' >> init.rc

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
