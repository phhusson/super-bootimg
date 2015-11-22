#!/system/bin/sh

#self = $scr

. "$(dirname "$scr")"/su-communication.sh
. "$(dirname "$scr")"/rights.sh

cp "$scriptdir"/bin/su-$DST_ARCH sbin/su
addFile sbin/su
chmod 0755 sbin/su

if [ -f "sepolicy" ];then
	#Create domains if they don't exist
	"$scriptdir"/bin/sepolicy-inject -z su -P sepolicy
	"$scriptdir"/bin/sepolicy-inject -z su_device -P sepolicy
	"$scriptdir"/bin/sepolicy-inject -z su_daemon -P sepolicy

	#Autotransition su's socket to su_device
	"$scriptdir"/bin/sepolicy-inject -s su_daemon -f device -c file -t su_device -P sepolicy
	"$scriptdir"/bin/sepolicy-inject -s su_daemon -f device -c dir -t su_device -P sepolicy
	allow su_device tmpfs filesystem "associate"

	#Transition from untrusted_app to su_client
	#TODO: other contexts want access to su?
	allowSuClient shell
	allowSuClient untrusted_app
	allowSuClient su

	#HTC Debug context requires SU
	"$scriptdir/bin/sepolicy-inject" -e -s ssd_tool -P sepolicy && allowSuClient ssd_tool

	#Allow init to execute su daemon/transition
	allow init su_daemon process "transition"
	noaudit init su_daemon process "rlimitinh siginh noatsecure"
	suDaemonRights

	allowLog su
	suRights su

	suL0 su
	suL1 su
	suL3 su

	#Need to set su_device/su as trusted to be accessible from other categories
	"$scriptdir"/bin/sepolicy-inject -a mlstrustedobject -s su_device -P sepolicy
	"$scriptdir"/bin/sepolicy-inject -a mlstrustedsubject -s su_daemon -P sepolicy
	"$scriptdir"/bin/sepolicy-inject -a mlstrustedsubject -s su -P sepolicy

	if [ "$1" == "power" -o "$1" == "eng" ];then
		suL6 su
		suL8 su
		suL9 su
	fi

	if [ "$1" == "eng" ];then
		"$scriptdir"/bin/sepolicy-inject -Z su -P sepolicy
	fi
fi

#Disable recovery overwrite
sed -i '/flash_recovery/a \    disabled' init.rc

sed -i '/on init/a \    chmod 0755 /sbin' init.rc
echo -e 'service su /sbin/su --daemon\n\tclass main\n\tseclabel u:r:su_daemon:s0\n' >> init.rc
addFile init.rc

VERSIONED=1
