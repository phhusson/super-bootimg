#!/system/bin/sh
#In this file lies the real permissions of a process running in su

r_file_perms="getattr open read ioctl lock"
x_file_perms="getattr execute execute_no_trans"
rx_file_perms="$r_file_perms $x_file_perms"
w_file_perms="open append write"
#Here lies everything we know DON'T want
#Add them as noaudits
knownForbidden() {
	noaudit $1 dalvikcache_data_file dir "write add_name remove_name"
	noaudit $1 dalvikcache_data_file file "write append create unlink"
}

#Enable the app to write to logs
allowLog() {
	#logdw_socket not in Android 4.4
	allow $1 logdw_socket sock_file "write" || true
	#logd not in Android 4.4
	if allow $1 logd unix_dgram_socket "sendto";then
		allow logd $1 dir "search"
		allow logd $1 file "read open getattr"
	fi
	allow $1 $1 dir "search read"
	allow $1 $1 "unix_dgram_socket" "create connect write"
	allow $1 $1 "lnk_file" "read"
	allow $1 $1 file "read"
	allow $1 toolbox_exec file "read" || true
	allow $1 devpts chr_file "read write open"
}

#Rights to be added for services/apps to talk (back) to su
suBackL0() {
	[ "$ANDROID" -ge 20 ] && allow system_server $1 binder "call transfer"

	#ES Explorer opens a sokcet
	allow untrusted_app su unix_stream_socket "$rw_socket_perms connectto"

	#Any domain is allowed to send su "sigchld"
	#TODO: Have sepolicy-inject handle that
	#allow "=domain" su process "sigchld"
	allow "surfaceflinger" "su" "process" "sigchld"
}

suBackL6() {
	#Used by CF.lumen (restarts surfaceflinger, and communicates with it)
	#TODO: Add a rule to enforce surfaceflinger doesn't have dac_override
	allowFSRWX surfaceflinger "app_data_file"
	"$scriptdir"/bin/sepolicy-inject -a mlstrustedsubject -s surfaceflinger -P sepolicy
}

suBind() {
	#Allow to override /system/xbin/su
	allow su_daemon su_exec "file" "mounton read"

	#We will create files in /dev/su/, they will be marked as su_device
	allowFSRWX su_daemon su_device
	allow su_daemon su_device "file" "relabelfrom"
	allow su_daemon system_file "file" "relabelto"
}

#This is the vital minimum for su to open a uid 0 shell
suRights() {
	#Communications with su_daemon
	allow $1 "su_daemon" fd "use"
	allow $1 "su_daemon" process "sigchld"
	allow $1 "su_daemon" "unix_stream_socket" "read write"

	#Admit su_daemon is meant to be god.
	allow su_daemon su_daemon "capability" "sys_admin"

	allow servicemanager $1 "dir" "search read"
	allow servicemanager $1 "file" "open read"
	allow servicemanager $1 "process" "getattr"
	allow servicemanager $1 "binder" "transfer"
	[ "$ANDROID" -ge 20 ] && allow system_server su binder "call"

	allow $1 "shell_exec zygote_exec dalvikcache_data_file rootfs system_file" file "$rx_file_perms entrypoint"
	allow $1 "dalvikcache_data_file rootfs system_file" lnk_file "read getattr"
	allow $1 "dalvikcache_data_file rootfs system_file" dir "$r_dir_perms"
	#toolbox_exec is Android 6.0, was "system_file" before
	[ "$ANDROID" -ge 23 ] && allow $1 "toolbox_exec" file "$rx_file_perms entrypoint"
	allow $1 "devpts" chr_file "getattr ioctl"
	[ "$ANDROID" -ge 20 ] && allow $1 "system_server servicemanager" "binder" "call transfer"
	[ "$ANDROID" -ge 23 ] && allow $1 activity_service service_manager "find"
	#untrusted_app_devpts not in Android 4.4
	if [ "$ANDROID" -ge 20 ];then
		allow $1 untrusted_app_devpts chr_file "read write open getattr ioctl"
	else
		allow $1 devpts chr_file "read write open getattr ioctl"
	fi

	#Give full access to itself
	allow $1 $1 "file" "$rwx_file_perms"
	allow $1 $1 "unix_stream_socket" "$create_stream_socket_perms"
	allow $1 $1 "process" "sigchld setpgid setsched fork signal execmem getsched"
	allow $1 $1 "fifo_file" "$rw_file_perms"
}

suReadLogs() {
	#dmesg
	allow $1 kernel system "syslog_read syslog_mod"
	allow $1 $1 capability2 "syslog"

	#logcat
	#logdr_socket and logd not in Android 4.4
	if [ "$ANDROID" -ge 20 ];then
		allow $1 logdr_socket sock_file "write"
		allow $1 logd unix_stream_socket "connectto $rw_socket_perms"
	fi
	if [ "$ANDROID" -ge 22 ];then
		allow $1 logcat_exec file "getattr execute"
	fi
}

suToApps() {
	allow $1 untrusted_app fifo_file "ioctl getattr"
	allow $1 app_data_file dir "search getattr"
	allow $1 app_data_file file "getattr execute read open execute_no_trans"
}

#Refer/comment to super-bootimg's issue #4
suFirewall() {
	suToApps $1

	allow $1 $1 unix_stream_socket "$create_stream_socket_perms"
	allow $1 $1 rawip_socket "$create_socket_perms"
	allow $1 $1 udp_socket "$create_socket_perms"
	allow $1 $1 tcp_socket "$create_socket_perms"
	allow $1 $1 capability "net_raw net_admin"
	allow $1 $1 netlink_route_socket "nlmsg_write"
}

suMiscL0() {
	#In for untrusted_app in AOSP b/23476772
	[ "$ANDROID" -ge 20 ] && allow $1 servicemanager service_manager list
	allow $1 $1 capability "sys_nice"
}

suServicesL1() {
	if [ "$ANDROID" -ge 23 ];then
		allow $1 =service_manager_type-gatekeeper_service service_manager find
	elif [ "$ANDROID" -ge 20 ];then
		allow $1 =service_manager_type service_manager find
	fi
}

suMiscL1() {
	#Access to /data/local/tmp/
	allowFSRWX $1 shell_data_file

	#Access to /sdcard & friends

	if [ "$ANDROID" -ge 20 ];then
		#Those are AndroidM specific
		[ "$ANDROID" -ge 23 ] && allowFSR $1 "storage_file mnt_user_file"

		#fuse context is >= 5.0
		[ "$ANDROID" -ge 20 ] && allowFSR $1 "fuse"
	fi

	#strace self
	allow $1 $1 process "ptrace"
}

suNetworkL0() {
	"$scriptdir"/bin/sepolicy-inject -a netdomain -s su -P sepolicy
	"$scriptdir"/bin/sepolicy-inject -a bluetoothdomain -s su -P sepolicy
}

suNetworkL1() {
	allow $1 $1 netlink_route_socket "create setopt bind getattr write nlmsg_read read"
	[ "$ANDROID" -ge 20 ] && allowFSR su net_data_file
	true
}

suMiscL8() {
	#Allow to mount --bind to a file in /system/
	allow $1 system_file file "mounton"
	allow $1 $1 capability "sys_admin"
}

suMiscL9() {
	#Remounting /system RW
	allow $1 labeledfs filesystem "remount unmount"
	#Remounting / RW
	allow $1 rootfs filesystem remount

	allowFSRW $1 block_device
	allow $1 block_device blk_file "$rw_file_perms"

	allow $1 $1 capability "sys_admin"
}

suL0() {
	suBackL0 $1

	suMiscL0 $1
	suReadLogs $1
	suNetworkL0 $1
}

suL1() {
	suMiscL1 $1
	suServicesL1 $1
	suNetworkL1 $1
}

suL3() {
	suFirewall $1

	#Only su_daemon can bind, don't specify domain argument
	suBind
}

suL6() {
	suBackL6 $1
}

suL8() {
	suMiscL8 $1
}

suL9() {
	suMiscL9 $1

	allowFSRW su_daemon su_daemon
	allowFSRW su_daemon system_data_file
	allow su_daemon "labeledfs" filesystem "associate"
	allow su_daemon su_daemon process setfscreate
	allow su_daemon tmpfs filesystem associate
	allow su_daemon su_daemon file relabelfrom
	allow su_daemon system_file file mounton
}
