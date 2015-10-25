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

#allow <list of scontext> <list of tcontext> <class> <list of perm>
function allow() {
	for s in $1;do
		for t in $2;do
			for p in $4;do
				"$homedir"/bin/sepolicy-inject -s $s -t $t -c $3 -p $p -P sepolicy
			done
		done
	done
}

#allowTransition scon fcon tcon
function allowTransition() {
	allow $1 $2 file "getattr execute read open"
	allow $1 $3 process transition
	allow $3 $1 process sigchld
	#Auto transition
	"$homedir"/bin/sepolicy-inject -s $1 -t $3 -c process -f $2 -P sepolicy
}

#allowSuClient <scontext>
function allowSuClient() {
	allow $1 su file "getattr execute read open"
	allow $1 su file "execute_no_trans"
	allow $1 su_daemon unix_stream_socket "connectto getopt"
}

#allowLog <scontext>
function allowLog() {
	allow $1 logdw_socket sock_file "write"
	allow $1 logd unix_dgram_socket "sendto"
	allow logd $1 dir "search"
	allow logd $1 file "read open getattr"
}

cp "$homedir"/bin/su .
if [ -f "sepolicy" ];then
	#Create domains if they don't exist
	"$homedir"/bin/sepolicy-inject -z su_daemon -P sepolicy
	"$homedir"/bin/sepolicy-inject -z su -P sepolicy

	#Init calls restorecon /su
	allow init su file "relabelto"
	allow su rootfs filesystem "associate"
	#Transition from init to su_exec if filecon is "su"
	allowTransition init su su_daemon

	#Transition from untrusted_app to su_client
	#TODO: other contexts want access to su?
	allowSuClient shell
	allowSuClient untrusted_app

	allowLog su_daemon

	if [ "$2" == "eng" ];then
		#su is the context of the file (nothing more)
		#su_daemon and su_client contexts should be explicit
		"$homedir"/bin/sepolicy-inject -Z su_daemon -P sepolicy

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
