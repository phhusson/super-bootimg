#!/bin/bash

function cleanup() {
	[ -d "$zipfolder" ] && rm -Rf "$zipfolder"
}

set -e
trap cleanup EXIT

if [ ! -f libs/armeabi/sepolicy-inject ];then
	echo "You must call ndk-build first"
	exit 1
fi

rm -f superuser.zip
zipfolder="$(mktemp -d)"

git ls-files scripts |while read i;do
	mkdir -p $zipfolder/$(dirname $i)
	cp $i $zipfolder/$(dirname $i)
done
#Do not include x86
rm $zipfolder/scripts/bin/futility-x86

for i in bootimg-repack bootimg-extract sepolicy-inject strip-cpio sepolicy-inject-v2 hidesu;do
	cp libs/armeabi/$i $zipfolder/scripts/bin/
done
git rev-parse --short HEAD > $zipfolder/scripts/gitversion

mkdir -p $zipfolder/META-INF/com/google/android/
echo > $zipfolder/META-INF/com/google/android/updater-script
cp zip/update-binary.sh $zipfolder/META-INF/com/google/android/update-binary

#Default mode is eng verity crypt
echo 'eng verity crypt' > $zipfolder/config.txt

p=$PWD
pushd $zipfolder
out="$p/superuser-r$(cd $p;git rev-list HEAD --count)".zip
rm -f "$out"
zip -r "$out" .
