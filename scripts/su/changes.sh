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
keeprecovery=0
hidesu=0
permissivesysinit=0
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
		keeprecovery)
			keeprecovery=1
			;;
		hidesu)
			hidesu=1
			;;
		permissivesysinit)
			permissivesysinit=1
			;;
	esac
	shift
done

if [ -f "sepolicy" -a -z "$UNSUPPORTED_SELINUX" ];then
	#Create domains if they don't exist
	"$scriptdir"/bin/sepolicy-inject"$SEPOLICY" -z su -P sepolicy
	"$scriptdir"/bin/sepolicy-inject"$SEPOLICY" -z su_device -P sepolicy
	"$scriptdir"/bin/sepolicy-inject"$SEPOLICY" -z su_daemon -P sepolicy

	#Autotransition su's socket to su_device
	"$scriptdir"/bin/sepolicy-inject"$SEPOLICY" -s su_daemon -f device -c file -t su_device -P sepolicy
	"$scriptdir"/bin/sepolicy-inject"$SEPOLICY" -s su_daemon -f device -c dir -t su_device -P sepolicy
	allow su_device tmpfs filesystem "associate"

	#Transition from untrusted_app to su_client
	#TODO: other contexts want access to su?
	allowSuClient shell
	allowSuClient untrusted_app
	allowSuClient platform_app
	allowSuClient system_app
	allowSuClient su
	[ "$ANDROID" -ge 24 ] && allowSuClient priv_app

	#HTC Debug context requires SU
	"$scriptdir/bin/sepolicy-inject$SEPOLICY" -e -s ssd_tool -P sepolicy && allowSuClient ssd_tool

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
	"$scriptdir"/bin/sepolicy-inject"$SEPOLICY" -a mlstrustedobject -s su_device -P sepolicy
	"$scriptdir"/bin/sepolicy-inject"$SEPOLICY" -a mlstrustedsubject -s su_daemon -P sepolicy
	"$scriptdir"/bin/sepolicy-inject"$SEPOLICY" -a mlstrustedsubject -s su -P sepolicy

	if [ "$selinuxmode" == "power" -o "$selinuxmode" == "eng" ];then
		suL6 su
		suL8 su
		suL9 su
	fi

	if [ "$selinuxmode" == "eng" ];then
		"$scriptdir"/bin/sepolicy-inject"$SEPOLICY" -Z su -P sepolicy
	fi

	if [ "$permissivesysinit" == 1 ];then
		"$scriptdir"/bin/sepolicy-inject"$SEPOLICY" -Z sysinit -P sepolicy
	fi
fi

#Check if user wants to edit fstab
if [ "$nocrypt" -ne 0 -o "$noverity" -ne 0 ];then
	for i in fstab*;do
		cp $i ${i}.orig
		if [ "$nocrypt" == 1 ];then
			sed -i 's;\(/data.*\),encryptable=.*;\1;g' $i
			sed -i 's;\(/data.*\),forceencrypt=.*;\1;g' $i
			sed -i 's;\(/data.*\),forcefdeorfbe=.*;\1;g' $i
		elif [ "$nocrypt" == 2 ];then
			sed -i 's;,encryptable=.*;;g' $i
			sed -i 's;,forceencrypt=.*;;g' $i
			sed -i 's;,forcefdeorfbe=.*;;g' $i
		fi
		if [ "$noverity" == 1 ];then
			sed -i 's;,\{0,1\}verify\(=[^,]*\)\{0,1\};;g' $i
			sed -i 's;\bro\b;rw;g' $i
		fi
		addFile $i
	done
fi

#Samsung specific
#Prevent system from loading policy
if "$scriptdir/bin/sepolicy-inject" -e -s knox_system_app -P sepolicy;then
	"$scriptdir/bin/sepolicy-inject" --not -s init -t kernel -c security -p load_policy -P sepolicy
	for i in policyloader_app system_server system_app installd init ueventd runas drsd debuggerd vold zygote auditd servicemanager itsonbs commonplatformappdomain;do
		"$scriptdir/bin/sepolicy-inject" --not -s "$i" -t security_spota_file -c dir -p read,write -P sepolicy || true
		"$scriptdir/bin/sepolicy-inject" --not -s "$i" -t security_spota_file -c file -p read,write -P sepolicy || true
	done

	"$scriptdir/bin/sepolicy-inject" --auto -s su -P sepolicy
fi

if [ "$UNSUPPORTED_SELINUX" ];then
	#Disable SELinux the hard way
	sed -i -E 's;%s/enforce;/xxenforce;g' init
	sed -i -E 's;/sys/fs/selinux/checkreqprot;/dev_fs_selinux_checkreqprot;g' init
	sed -i -E 's;Initializing SELinux ;Initializing FFFinux ;g' init
	addFile init
	echo -n 1 > xxenforce
	addFile xxenforce
fi

#Disable recovery overwrite
if [ "$keeprecovery" == 0 ];then
	sed -i '/^service flash_recovery/a \    disabled' init.rc
fi

if [ "$hidesu" == 1 ];then
	sed -i '/on init/a \    chmod 0750 /sbin' init.rc
else
	sed -i '/on init/a \    chmod 0755 /sbin' init.rc
fi

echo -e 'service su /sbin/su --daemon\n\tclass main' >> init.rc
if [ -z "$UNSUPPORTED_SELINUX" ];then
	echo -e '\tseclabel u:r:su_daemon:s0' >> init.rc
else
	echo -e '\tseclabel u:r:kernel:s0' >> init.rc
fi
echo -e '\n' >> init.rc

if [ "$hidesu" == 1 ];then
	cp $scriptdir/bin/hidesu sbin/hidesu
	addFile sbin/hidesu
	chmod 0750 sbin/hidesu

	allow init su process transition
	allow rootfs tmpfs filesystem "associate"

	echo -e 'service hidesu /sbin/hidesu\n\tclass main' >> init.rc
	echo -e '\tseclabel u:r:su:s0' >> init.rc
	echo -e '\n' >> init.rc
fi
addFile init.rc

if [ -f init.superuser.rc ];then
	echo > init.superuser.rc
	addFile init.superuser.rc
fi

VERSIONED=1
