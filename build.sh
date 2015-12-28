#!/bin/bash

set -e
scriptdir="$PWD/scripts/"
rev="$(cd $scriptdir; git rev-list HEAD --count)"
function putBoot() {
	version="$(basename "$PWD")"
	t="$(dirname "$PWD")"
	device="$(basename "$t")"
	variant="$1"
	if [ -f new-boot.img.signed ];then
		mv -f new-boot.img.signed boot-${device}-${version}-su-${variant}-r${rev}.img
		rm -f new-boot.img
	else
		mv -f new-boot.img boot-${device}-${version}-su-${variant}-r${rev}.img
	fi
}

find output -name orig-boot.img |while read i;do
	pushd "$(dirname "$i")"

		rm -f boot-*.img
		#eng su
		bash "$scriptdir/bootimg.sh" orig-boot.img "$scriptdir/su/changes.sh" eng
		putBoot eng

		#user su
		bash "$scriptdir/bootimg.sh" orig-boot.img "$scriptdir/su/changes.sh"
		putBoot user

		#noverity eng su
		bash "$scriptdir/bootimg.sh" orig-boot.img "$scriptdir/su/changes.sh" eng noverity
		putBoot noverity

		#noverity nocrypt eng su
		bash "$scriptdir/bootimg.sh" orig-boot.img "$scriptdir/su/changes.sh" eng noverity nocrypt
		putBoot nocrypt
	popd
done
