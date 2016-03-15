#!/bin/bash

set -e

HERE="$(dirname "$(readlink -f -- "$0")")"
function testOn() {
	[ -n "$d" ] && rm -Rf "$d"
	d="$(mktemp -d)"
	echo "Testing on $1..."
	pushd $d
		"$HERE/../jni/extract" "$HERE/$1"
	popd
	testFile="$1"
}

function fail() {
	echo -e "\tTesting $1 on $testFile failed..."
	rm -Rf $d
	return 1
}

function ensureMD5() {
	if [ ! -f "$d/$1" ];then
		fail "$1"
	fi
	md5="$(md5sum "$d/$1" |grep -oE '^[0-9a-f]{32}')"
	if [ "$md5" != "$2" ];then
		fail "$1"
	fi
}

function ensureExists() {
	if [ ! -f "$d/$1" ];then
		fail "$1 existence"
	fi
}

function ensureNotExists() {
	if [ -f "$d/$1" ];then
	       fail "$1 non-existence"
       fi
}

#Generation mode
if [ -n "$1" ];then
	testOn "$1"
	echo testOn $1
	for i in kernel ramdisk.gz ramdisk second dt;do
		if [ -f $d/$i ];then
			echo ensureMD5 $i $(md5sum "$d/$i" |grep -oE '^[0-9a-f]{32}')
		else
			echo ensureNotExists $i
		fi
	done
	for i in $(cd $d; echo *);do
		echo ensureExists $i
	done
else
	testOn ../output/fairphone/fp2/orig-boot.img 
	ensureMD5 kernel eac6139f76f94e009b59d63ec264ce32
	ensureMD5 ramdisk.gz 3843a1551c04098ee03430cce84c7bd2
	ensureMD5 dt b68954a98624ca8f5a7da729ed2c06e5

	testOn ../output/nexus/ryu/MXC14G/orig-boot.img
	ensureMD5 kernel 773cdbcfdaa0bfc529ac0b673f357dde
	ensureMD5 ramdisk.gz 61e51b437419d2d1d6107692296588fc
	ensureExists chromeos
fi
rm -Rf $d
