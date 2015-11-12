#!/system/bin/sh
#In this file lies the real permissions of a process running in su

r_file_perms="getattr open read ioctl lock"
x_file_perms="getattr execute execute_no_trans"
rx_file_perms="$r_file_perms $x_file_perms"
w_file_perms="open append write"
#Here lies everything we know DON'T want
#Add them as noaudits
function knownForbidden() {
	noaudit $1 dalvikcache_data_file dir "write add_name remove_name"
	noaudit $1 dalvikcache_data_file file "write append create unlink"
}

#Enable the app to write to logs
function allowLog() {
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
function suBack() {
	allow system_server $1 binder "call transfer"

	#ES Explorer opens a sokcet
	allow untrusted_app su unix_stream_socket "$rw_socket_perms connectto"
}

#This is the vital minimum for su to open a uid 0 shell
function suRights() {
	#Communications with su_daemon
	allow $1 "su_daemon" fd "use"
	allow $1 "su_daemon" process "sigchld"
	allow $1 "su_daemon" "unix_stream_socket" "read write"

	allow servicemanager $1 "dir" "search read"
	allow servicemanager $1 "file" "open read"
	allow servicemanager $1 "process" "getattr"
	allow servicemanager $1 "binder" "transfer"
	allow system_server su binder "call"

	allow $1 "shell_exec zygote_exec dalvikcache_data_file rootfs system_file" file "$rx_file_perms entrypoint"
	#toolbox_exec is Android 6.0, was "system_file" before
	allow $1 "toolbox_exec" file "$rx_file_perms entrypoint" || true
	allow $1 "devpts" chr_file "getattr ioctl"
	allow $1 "system_server servicemanager" "binder" "call transfer"
	allow $1 activity_service service_manager "find" || true
	#untrusted_app_devpts not in Android 4.4
	allow $1 untrusted_app_devpts chr_file "read write open getattr ioctl" || true

	#Give full access to itself
	allow $1 $1 "file" "$rwx_file_perms"
	allow $1 $1 "unix_stream_socket" "$create_stream_socket_perms"
	allow $1 $1 "process" "sigchld setpgid setsched fork signal"
	allow $1 $1 "fifo_file" "$rw_file_perms"
}

function suReadLogs() {
	#dmesg
	allow $1 kernel system "syslog_read syslog_mod"
	allow $1 $1 capability2 "syslog"

	#logcat
	#logdr_socket and logd not in Android 4.4
	if allow $1 logdr_socket sock_file "write";then
		allow $1 logd unix_stream_socket "connectto $rw_socket_perms"
	fi
}

function suToApps() {
	allow $1 untrusted_app fifo_file "ioctl getattr"
	allow $1 app_data_file dir "search getattr"
	allow $1 app_data_file file "getattr execute read open execute_no_trans"
}

#Refer/comment to super-bootimg's issue #4
function suFirewall() {
	suToApps $1

	allow $1 $1 unix_stream_socket "$create_stream_socket_perms"
	allow $1 $1 rawip_socket "$create_socket_perms"
	allow $1 $1 udp_socket "$create_socket_perms"
	allow $1 $1 tcp_socket "$create_socket_perms"
	allow $1 $1 capability "net_raw net_admin"
}

function suMiscL0() {
	allow $1 $1 capability "sys_nice"
}

function suServicesL1() {
	allow $1 servicemanager service_manager list
	allow $1 =service_manager_type-gatekeeper_service service_manager find
}

function suMiscL1() {
	#Access to /data/local/tmp/
	allowFSRWX $1 shell_data_file

	#Access to /sdcard & friends

	#Those are AndroidM specific
	allowFSR $1 "storage_file mnt_user_file" || true
	#fuse context is >= 5.0
	allowFSR $1 "fuse" || true
}

function suMiscL8() {
	#Allow to mount --bind to a file in /system/
	allow $1 system_file file "mounton"
	allow $1 $1 capability "sys_admin"
}

function suMiscL9() {
	#Remounting /system RW
	allow $1 labeledfs filesystem "remount unmount"
	allowFSRW $1 block_device
	allow $1 block_device blk_file "$rw_file_perms"

	allow $1 $1 capability "sys_admin"
}

function suL0() {
	suBack $1

	suMiscL0 $1
	suReadLogs $1
}

function suL1() {
	suMiscL1 $1
	suServicesL1 $1
}

function suL8() {
	#L8 for the moment because of suToApps/dac_override
	suFirewall $1
	suMiscL8 $1
}

function suL9() {
	suMiscL9 $1
}
