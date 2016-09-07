#!/bin/bash

minimumVersion=M #Gingerbread is the oldest factory image on https://developers.google.com/android/nexus/images
existingDevicesOnly=YES #By default, we'll add images only for devices that already exist in the repository.

for i in "$@"; do
case $i in
	-m=*|--minimum-version=*)
	minimumVersion="${i#*=}"
	shift
	;;
	-e=*|--existing-devices-only=*)
	existingDevicesOnly="${i#*=}"
	shift
	;;
esac
done

minimumVersion="$( echo $minimumVersion | awk '{print toupper($0)}' )"
existingDevicesOnly="$( echo $existingDevicesOnly | awk '{print toupper($0)}' )"

#Stable
regexp='https://dl.google.com/dl/android/aosp/([a-z]+)-([a-z0-9]{3}[0-9]{2}[a-z])-factory-[0-9a-f]*.(zip|tgz)'
curl -s https://developers.google.com/android/nexus/images |
	grep -F '<a href="https://dl.google.com/dl/android/aosp/'|
	grep -oE "$regexp" |
	while read url;do
		device="$( sed -E 's;'"$regexp"';\1;g' <<<$url )"
		releaseLower="$( sed -E 's;'"$regexp"';\2;g' <<<$url )"
		release="$( sed -E 's;'"$regexp"';\2;g' <<<$url |tr a-z A-Z)"
		version=$( echo $release | cut -c 1 )

		if [ $existingDevicesOnly == "YES" ]; then
			#Skip any devices that we don't already have in the repo.  This means that new
			#devices must be manually added, but skips EOL'ed devices like the Nexus S.
			[[ ! -d "known-imgs/nexus/${device}" ]] && continue
		fi

		#Skip any releases older than the specified version.  We don't want to add
		#Jelly Bean for the Razor, for example, but we do want to add M images for Razor.
		[[ $version < $minimumVersion ]] && continue

		filename="known-imgs/nexus/${device}/${release}"
		[ -f $filename ] && continue
		mkdir -p $(dirname $filename)

		cat > $filename << EOF
$url
image-$device-${releaseLower}.zip
boot.img
EOF
done

#N preview
regexp='([a-z]+)-([a-z0-9]{3}[0-9]{2}[a-z])-factory-[0-9a-f]*.tgz'
curl -s https://developer.android.com/preview/download.html |
	grep -oE "$regexp" |
	while read archive;do
		url="http://storage.googleapis.com/androiddevelopers/shareables/preview/${archive}"
		device="$( sed -E 's|'"$regexp"'|\1|g' <<<$archive )"
		releaseLower="$( sed -E 's|'"$regexp"'|\2|g' <<<$archive )"
		release="$( sed -E 's|'"$regexp"'|\2|g' <<<$archive |tr a-z A-Z)"
		version=$( echo $release | cut -c 1 )

		if [ $existingDevicesOnly == "YES" ]; then
			#Skip any devices that we don't already have in the repo.  This means that new
			#devices must be manually added, but skips EOL'ed devices like the Nexus S.
			[[ ! -d "known-imgs/nexus/${device}" ]] && continue
		fi

		#Skip any releases older than the specified version.  We don't want to add
		#Jelly Bean for the Razor, for example, but we do want to add M images for Razor.
		[[ $version < $minimumVersion ]] && continue

		filename="known-imgs/nexus/${device}/${release}"
		[ -f $filename ] && continue
		mkdir -p $(dirname $filename)

		cat > $filename << EOF
$url
image-$device-${releaseLower}.zip
boot.img
EOF
done
