#!/bin/bash

#Marshmallow
regexp='https://dl.google.com/dl/android/aosp/([a-z]+)-([a-z0-9]{3}[0-9]{2}[a-z])-factory-[0-9a-f]*.tgz'
curl -s https://developers.google.com/android/nexus/images |
	grep -F '<a href="https://dl.google.com/dl/android/aosp/'|
	grep -oE "$regexp" |
	while read url;do
		device="$( sed -E 's|'"$regexp"'|\1|g' <<<$url )"
		releaseLower="$( sed -E 's|'"$regexp"'|\2|g' <<<$url )"
		release="$( sed -E 's|'"$regexp"'|\2|g' <<<$url |tr a-z A-Z)"

		#Skip any devices that we don't already have in the repo.  This means that new
		#devices must be manually added, but skips EOL'ed devices like the Nexus S.
		[[ ! -d "known-imgs/nexus/${device}" ]] && continue
		#Skip any releases older than Marshmallow.  For example, we don't care to add
		#Jelly Bean for the Nexus 7 2013 razorg to the repository.
		[[ ! $release == M* ]] && continue

		filename="known-imgs/nexus/${device}/${release}"
		[ -f $filename ] && continue
		mkdir -p $(dirname $filename)

		cat > $filename << EOF
$url
image-$device-${releaseLower}.zip
boot.img
EOF
done
