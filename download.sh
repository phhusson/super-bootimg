#!/bin/bash

set -e

TOCLEAN=""
function clean() {
	rm -Rf $TOCLEAN
	TOCLEAN=""
}

find known-imgs -type f |while read i;do
	folder="$(cut -d / -f 2- <<<$i)"
	mkdir -p output/$folder
	destination="output/$folder/orig-boot.img"

	curr=""
	cat $i |while read j;do
		if grep -qE '^http' <<<$j;then
			curr="$(mktemp -d)/$(basename "$j")"
			wget "$j" -O $curr
		elif grep -qE '\.zip$' <<<$curr;then
			d="$(mktemp -d)"
			TOCLEAN="$TOCLEAN $d"
			unzip "$curr" "$j" -d "$d"
			curr="$(find "$d" -name "$(basename "$j")")"
		elif grep -qE '\.tgz$' <<<$curr;then
			d="$(mktemp -d)"
			TOCLEAN="$TOCLEAN $d"
			tar xf "$curr" "$j" -C "$d"
			curr="$(find "$d" -name "$(basename "$j")")"
		fi
	done
	cp "$curr" "$destination"
	clean
done
