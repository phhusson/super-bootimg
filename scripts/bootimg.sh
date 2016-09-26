#!/system/bin/sh

export TMPDIR=/tmp/
mktempS() {
	v=$TMPDIR/tmp.$RANDOM
	mkdir -p $v
	echo $v
}

if ! which mktemp;then
	alias mktemp=mktempS
fi

if [ "$#" == 0 ];then
	echo "Usage: $0 <original boot.img> [eng|user]"
	exit 1
fi

set -e

if [ -f "$2" ];then
	scr="$(readlink -f "$2")"
	used_scr=1
else
	scr="$PWD/changes.sh"
fi

cleanup() {
	rm -Rf "$bootimg_extract" "$d2"
}

trap cleanup EXIT
#Ensure binaries are executables
scriptdir="$(dirname "$(readlink -f "$0")")"
for i in sepolicy-inject sepolicy-inject-v2 bootimg-repack bootimg-extract strip-cpio;do
	chmod 0755 $scriptdir/bin/$i || true
done

startBootImgEdit() {
	f="$(readlink -f "$1")"
	homedir="$PWD"
	bootimg_extract="$(mktemp -d)"
	cd "$bootimg_extract"

	"$scriptdir/bin/bootimg-extract" "$f"
	[ -f chromeos ] && CHROMEOS=1
	d2="$(mktemp -d)"
	cd "$d2"

	if [ -f "$bootimg_extract"/ramdisk.gz ];then
		gunzip -c < "$bootimg_extract"/ramdisk.gz |cpio -i
		gunzip -c < "$bootimg_extract"/ramdisk.gz > ramdisk1
	else
		echo "Unknown ramdisk format"
		cd "$homedir"
		rm -Rf "$bootimg_extract" "$d2"
		exit 1
	fi

	INITRAMFS_FILES=""

	if file init |grep -q Intel;then
		DST_ARCH=x86
	else
		DST_ARCH=arm
	fi
}

[[ "toto2" =~ "toto" ]] && good_expr=1

addFile() {
	#WARNING FIXME: If you want to add toto and toto2
	#You must add toto2 THEN toto
	if [ -n "$good_expr" ];then
		[[ "$INITRAMFS_FILES" =~ "$1" ]] || INITRAMFS_FILES="$INITRAMFS_FILES $*"
	else
		#Slower but doesn't go into the WARNING
		if ! echo $INITRAMFS_FILES |grep -qE "\b$1\b";then
			INITRAMFS_FILES="$INITRAMFS_FILES $*"
		fi
	fi
}

doneBootImgEdit() {
	find . -type f -exec touch -t 197001011200 {} \;
	#List of files to replace \n separated
	echo $INITRAMFS_FILES |tr ' ' '\n' | cpio -o -H newc > ramdisk2

	if [ -f "$bootimg_extract"/ramdisk.gz ];then
		#TODO: Why can't I recreate initramfs from scratch?
		#Instead I use the append method. files gets overwritten by the last version if they appear twice
		#Hence sepolicy/su/init.rc are our version
		#There is a trailer in CPIO file format. Hence strip-cpio
		rm -f cpio-*
		"$scriptdir/bin/strip-cpio" ramdisk1 $INITRAMFS_FILES
		cat cpio-* ramdisk2 > ramdisk.tmp
		touch -t 197001011200 ramdisk.tmp
		gzip -9 -c -n ramdisk.tmp > "$bootimg_extract"/ramdisk.gz
	else
		exit 1
	fi

	cd "$bootimg_extract"
	rm -Rf "$d2"
	"$scriptdir/bin/bootimg-repack" "$f"
	cp new-boot.img "$homedir"

	cd "$homedir"
	rm -Rf "$bootimg_extract"
}

#allow <list of scontext> <list of tcontext> <class> <list of perm>
allow() {
	addFile sepolicy
	[ -z "$1" -o -z "$2" -o -z "$3" -o -z "$4" ] && false
	for s in $1;do
		for t in $2;do
			"$scriptdir"/bin/sepolicy-inject$SEPOLICY -s $s -t $t -c $3 -p $(echo $4|tr ' ' ',') -P sepolicy
		done
	done
}

noaudit() {
	addFile sepolicy
	for s in $1;do
		for t in $2;do
			for p in $4;do
				"$scriptdir"/bin/sepolicy-inject"$SEPOLICY" -s $s -t $t -c $3 -p $p -P sepolicy
			done
		done
	done
}

#Extracted from global_macros
r_file_perms="getattr open read ioctl lock"
x_file_perms="getattr execute execute_no_trans"
rx_file_perms="$r_file_perms $x_file_perms"
w_file_perms="open append write"
rw_file_perms="$r_file_perms $w_file_perms"
rwx_file_perms="$rx_file_perms $w_dir_perms"
rw_socket_perms="ioctl read getattr write setattr lock append bind connect getopt setopt shutdown"
create_socket_perms="create $rw_socket_perms"
rw_stream_socket_perms="$rw_socket_perms listen accept"
create_stream_socket_perms="create $rw_stream_socket_perms"
r_dir_perms="open getattr read search ioctl"
w_dir_perms="open search write add_name remove_name"
ra_dir_perms="$r_dir_perms add name write"
rw_dir_perms="$r_dir_perms $w_dir_perms"
create_dir_perms="create reparent rename rmdir setattr $rw_dir_perms"

allowFSR() {
	allow "$1" "$2" dir "$r_dir_perms"
	allow "$1" "$2" file "$r_file_perms"
	allow "$1" "$2" lnk_file "read getattr"
}

allowFSRW() {
	allow "$1" "$2" dir "$rw_dir_perms create"
	allow "$1" "$2" file "$rw_file_perms create setattr unlink rename"
	allow "$1" "$2" lnk_file "read getattr"
}

allowFSRWX() {
	allowFSRW "$1" "$2"
	allow "$1" "$2" file "$x_file_perms"
}


startBootImgEdit "$1"

if [ -f sepolicy ] && \
	! "$scriptdir/bin/sepolicy-inject" -e -c filesystem -P sepolicy && \
	"$scriptdir/bin/sepolicy-inject-v2" -e -c filesystem -P sepolicy;then

	SEPOLICY="-v2"
	ANDROID=24
elif "$scriptdir/bin/sepolicy-inject" -e -s gatekeeper_service -P sepolicy;then
	#Android M
	ANDROID=23
elif "$scriptdir/bin/sepolicy-inject" -e -c service_manager -P sepolicy;then
	#Android L MR1
	ANDROID=21
#TODO: Android 5.0? Android 4.3?
else
	#Assume KitKat
	ANDROID=19
fi

shift
[ -n "$used_scr" ] && shift

. $scr

if [ -n "$VERSIONED" ];then
	if [ -f "$scriptdir"/gitversion ];then
		rev="$(cat $scriptdir/gitversion)"
	else
		pushd $scriptdir
		rev="$(git rev-parse --short HEAD)"
		popd
	fi

	echo $rev > super-bootimg
	addFile super-bootimg
fi

doneBootImgEdit
if [ -f $scriptdir/keystore.x509.pem -a -f $scriptdir/keystore.pk8 -a -z "$NO_SIGN" -a -z "$CHROMEOS" ];then
	java -jar $scriptdir/keystore_tools/BootSignature.jar /boot new-boot.img $scriptdir/keystore.pk8 $scriptdir/keystore.x509.pem new-boot.img.signed
fi

if [ -n "$CHROMEOS" ];then
	echo " " > toto1
	echo " " > toto2
	#TODO: Properly detect ARCH
	if $scriptdir/bin/futility-arm version > /dev/null;then
		ARCH=arm
	else
		ARCH=x86
	fi
	$scriptdir/bin/futility-$ARCH vbutil_keyblock --pack output.keyblock --datapubkey $scriptdir/kernel_data_key.vbpubk --signprivate $scriptdir/kernel_subkey.vbprivk --flags 0x7

	$scriptdir/bin/futility-$ARCH vbutil_kernel --pack new-boot.img.signed --keyblock output.keyblock --signprivate $scriptdir/kernel_data_key.vbprivk --version 1 --vmlinuz new-boot.img --config toto1 --arch arm --bootloader toto2 --flags 0x1


	rm -f toto1 toto2 output.keyblock
fi

# Silence warning when boot on Samsung phones
# XXX: This check ONLY works on LIVE devices, not from script
# Here this is not a problem because the change is purely cosmetic, but don't rely on this if for anything else
if getprop ro.product.manufacturer | grep -iq '^samsung$'; then
	echo "SEANDROIDENFORCE" >> "new-boot.img"
fi
