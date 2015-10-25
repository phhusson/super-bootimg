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
fi

echo -e 'service su /su --daemon\n\tclass main\n' >> init.rc

if [ -f "$d"/ramdisk.gz ];then
	find . |cpio -o -H newc | gzip -9 -c > "$d"/ramdisk.gz
fi
cd "$d"
rm -Rf "$d2"
"$homedir/bin/bootimg-repack" "$f"
cp new-boot.img "$homedir"

cd "$homedir"
rm -Rf "$d"
