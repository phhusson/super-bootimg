#!/system/bin/sh

#self = $scr

. "$(dirname "$scr")"/su-communication.sh
. "$(dirname "$scr")"/rights.sh

cp "$scriptdir"/bin/su-$DST_ARCH sbin/su
addFile sbin/su
chmod 0755 sbin/su

selinuxmode="user"
noverity=0
nocrypt=0
while [ "$#" -ge 1 ];do
	case $1 in
		eng|power|user)
			selinuxmode="$1"
			;;

		noverity)
			noverity=1
			;;
		verity)
			noverity=0
			;;

		nocrypt_all)
			nocrypt=2
			;;
		nocrypt)
			nocrypt=1
			;;
		crypt)
			nocrypt=0
			;;
	esac
	shift
done

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

	if [ "$selinuxmode" == "power" -o "$selinuxmode" == "eng" ];then
		suL6 su
		suL8 su
		suL9 su
	fi

	if [ "$selinuxmode" == "eng" ];then
		"$scriptdir"/bin/sepolicy-inject -Z su -P sepolicy
	fi
fi

#Check if user wants to edit fstab
if [ "$nocrypt" -ne 0 -o "$noverity" -ne 0 ];then
	for i in fstab*;do
		cp $i ${i}.orig
		if [ "$nocrypt" == 1 ];then
			sed -i 's;\(/data.*\),encryptable=.*;\1;g' $i
			sed -i 's;\(/data.*\),forceencrypt=.*;\1;g' $i
		elif [ "$nocrypt" == 2 ];then
			sed -i 's;,encryptable=.*;;g' $i
			sed -i 's;,forceencrypt=.*;;g' $i
		fi
		[ "$noverity" == 1 ] && sed -i 's;,verify;;g' $i
		addFile $i
	done
fi

#Samsung specific
#Prevent system from loading policy
if "$scriptdir/bin/sepolicy-inject" -e -s knox_system_app -P sepolicy;then
	"$scriptdir/bin/sepolicy-inject" --not -s init -t kernel -c security -p load_policy -P sepolicy
	for i in policyloader_app system_server system_app installd init ueventd runas drsd debuggerd vold zygote auditd servicemanager itsonbs commonplatformappdomain;do
		"$scriptdir/bin/sepolicy-inject" --not -s "$i" -t security_spota_file -c dir -p read,write -P sepolicy
		"$scriptdir/bin/sepolicy-inject" --not -s "$i" -t security_spota_file -c file -p read,write -P sepolicy
	done
fi

#Disable recovery overwrite
sed -i '/flash_recovery/a \    disabled' init.rc

sed -i '/on init/a \    chmod 0755 /sbin' init.rc
echo -e 'service su /sbin/su --daemon\n\tclass main\n\tseclabel u:r:su_daemon:s0\n' >> init.rc
addFile init.rc

VERSIONED=1
