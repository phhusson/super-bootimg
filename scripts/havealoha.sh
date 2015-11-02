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

cat >> init.rc <<EOF

service host_mount mount -o bind /data/hosts /system/etc/hosts
	class main
	oneshot
EOF

allow init system_data_file "file" "mounton"

(echo 'ro.sf.lcd_density=320' ;cat default.prop) > t
mv -f t default.prop

#List of files to replace \n separated
echo -e 'sepolicy\ninit.rc\ndefault.prop' | cpio -o -H newc > ramdisk2

if [ -f "$d"/ramdisk.gz ];then
	#TODO: Why can't I recreate initramfs from scratch?
	#Instead I use the append method. files gets overwritten by the last version if they appear twice
	#Hence sepolicy/su/init.rc are our version
	cat ramdisk1 ramdisk2 |gzip -9 -c > "$d"/ramdisk.gz
fi
cp "sepolicy" "$homedir"
cd "$d"
rm -Rf "$d2"
"$scriptdir/bin/bootimg-repack" "$f"
cp new-boot.img "$homedir"

cd "$homedir"
rm -Rf "$d"
