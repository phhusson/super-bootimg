#!/system/bin/sh

set -e

function cleanup() {
	rm -Rf "$d" "$d2"
}

trap cleanup EXIT

set -e

f="$(readlink -f "$1")"
homedir="$PWD"
d="$(mktemp -d)"
cd "$d"

"$homedir/bin/bootimg-extract" "$f"
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

cp "$homedir"/bin/su .
if [ -f "sepolicy" ];then
	"$homedir"/bin/sepolicy-inject -Z init -P sepolicy
	"$homedir"/bin/sepolicy-inject -Z shell -P sepolicy
	"$homedir"/bin/sepolicy-inject -Z untrusted_app -P sepolicy
	"$homedir"/bin/sepolicy-inject -Z toolbox -P sepolicy
	"$homedir"/bin/sepolicy-inject -Z zygote -P sepolicy
	"$homedir"/bin/sepolicy-inject -Z servicemanager -P sepolicy
fi

echo -e 'service su /su --daemon\n\tclass main\n' >> init.rc

echo -e 'su\ninit.rc\nsepolicy' | cpio -o -H newc > ramdisk2

if [ -f "$d"/ramdisk.gz ];then
	#TODO: Why can't I recreate initramfs from scratch?
	#Instead I use the append method. files gets overwritten by the last version if they appear twice
	#Hence sepolicy/su/init.rc are our version
	cat ramdisk1 ramdisk2 |gzip -9 -c > "$d"/ramdisk.gz
fi
cd "$d"
rm -Rf "$d2"
"$homedir/bin/bootimg-repack" "$f"
cp new-boot.img "$homedir"

cd "$homedir"
rm -Rf "$d"
