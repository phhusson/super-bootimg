#!/bin/bash

set -e

scriptdir="$PWD/scripts/"
find output -name orig-boot.img |while read i;do
	pushd "$(dirname "$i")"


		#eng su
		bash "$scriptdir/bootimg.sh" orig-boot.img "$scriptdir/su/changes.sh" eng
		mv -f new-boot.img boot-su-eng.img

		#user su
		bash "$scriptdir/bootimg.sh" orig-boot.img "$scriptdir/su/changes.sh"
		mv -f new-boot.img boot-su-user.img


	popd
done
