#!/bin/bash

set -e

scriptdir="$PWD/scripts/"
find output -name orig-boot.img |while read i;do
	pushd "$(dirname "$i")"


		#eng su
		bash "$scriptdir/bootimg.sh" orig-boot.img "$scriptdir/su/changes.sh" eng
		if [ -f new-boot.img.signed ];then
			mv -f new-boot.img.signed boot-su-eng.img
			rm -f new-boot.img
		else
			mv -f new-boot.img boot-su-eng.img
		fi

		#user su
		bash "$scriptdir/bootimg.sh" orig-boot.img "$scriptdir/su/changes.sh"
		if [ -f new-boot.img.signed ];then
			mv -f new-boot.img.signed boot-su-user.img
			rm -f new-boot.img
		else
			mv -f new-boot.img boot-su-user.img
		fi

		#Remove old power su
		rm -f boot-su-power.img

		#noverity eng su
		bash "$scriptdir/bootimg.sh" orig-boot.img "$scriptdir/su/changes.sh" eng noverity
		if [ -f new-boot.img.signed ];then
			mv -f new-boot.img.signed boot-su-noverity.img
			rm -f new-boot.img
		else
			mv -f new-boot.img boot-su-noverity.img
		fi

	popd
done
