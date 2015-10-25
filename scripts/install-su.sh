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
	if [ "$2" == "eng" ];then
		#su is the context of the file (nothing more)
		#su_daemon and su_client contexts should be explicit
		"$homedir"/bin/sepolicy-inject -Z su_daemon -P sepolicy
		"$homedir"/bin/sepolicy-inject -Z su_client -P sepolicy
		"$homedir"/bin/sepolicy-inject -Z su -P sepolicy
		#Transition from init to su_exec if filecon is "su"
		"$homedir"/bin/sepolicy-inject -s init -t su_daemon -c process -f su -P sepolicy
		#Transition from untrusted_app to su_client
		#TODO: other contexts want access to su?
		"$homedir"/bin/sepolicy-inject -s untrusted_app -t su_client -c process -f su -P sepolicy
		"$homedir"/bin/sepolicy-inject -s shell -t su_client -c process -f su -P sepolicy

		"$homedir"/bin/sepolicy-inject -Z init -P sepolicy
		"$homedir"/bin/sepolicy-inject -Z shell -P sepolicy
		"$homedir"/bin/sepolicy-inject -Z untrusted_app -P sepolicy
		"$homedir"/bin/sepolicy-inject -Z toolbox -P sepolicy
		"$homedir"/bin/sepolicy-inject -Z zygote -P sepolicy
		"$homedir"/bin/sepolicy-inject -Z servicemanager -P sepolicy
	else
		echo "Only eng mode supported yet"
		exit 1
	fi
fi

sed -i -E '/on init/a \\trestorecon /su' init.rc
echo -e 'service su /su --daemon\n\tclass main\n' >> init.rc
echo -e '/su\tu:object_r:su:s0' >> file_contexts

echo -e 'su\ninit.rc\nsepolicy\nfile_contexts' | cpio -o -H newc > ramdisk2

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
