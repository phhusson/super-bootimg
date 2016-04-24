#!/bin/bash

set -e

TOCLEAN=""
function clean() {
	rm -Rf $TOCLEAN
	TOCLEAN=""
}
trap clean EXIT

function mktempd() {
	d="$(mktemp -d)"
	TOCLEAN+=" $d"
}

list="$1"
[ -z "$list" ] && list="$(find known-imgs -type f)"

echo "$list" |while read i;do
	folder="$(cut -d / -f 2- <<<$i)"
	mkdir -p output/$folder
	destination="output/$folder/orig-boot.img"
	if [ -f "$destination" ];then
		continue
	fi

	curr=""
	OLD_IFS="$IFS"
	IFS=$'\n'
	for j in $(cat $i);do
		if grep -qE '^http' <<<$j;then
			mktempd
			curr="$d/$(basename "$j")"
			wget "$j" -O $curr
		elif grep -qE '\.zip$' <<<$curr;then
			mktempd
			unzip "$curr" "$j" -d "$d" ||	\
				unzip "$curr" "*/$j" -d "$d"

			curr="$(find "$d" -name "$(basename "$j")")"
		elif grep -qE '\.tgz$' <<<$curr;then
			mktempd
			file="$(tar tf "$curr" |grep -E "/$j")"
			tar xf "$curr" -C "$d" "$file"
			curr="$(find "$d" -name "$(basename "$j")")"
		fi
		echo $curr
	done
	IFS="$OLD_IFS"
	cp "${curr}" "$destination"
	rm -f result
	clean
done
