#!/system/bin/sh

#allowSuClient <scontext>
allowSuClient() {
	#All domain-s already have read access to rootfs
	allow $1 rootfs file "execute_no_trans execute" #TODO: Why do I need execute?!? (on MTK 5.1, kernel 3.10)
	[ "$ANDROID" -ge 24 ] && allowFSR $1 rootfs
	allow $1 su_daemon unix_stream_socket "connectto getopt"

	allow $1 su_device dir "search read"
	allow $1 su_device sock_file "read write"
	allow su_daemon $1 "fd" "use"

	allow su_daemon $1 fifo_file "read write getattr ioctl"

	#Read /proc/callerpid/cmdline in from_init, drop?
	#Requiring sys_ptrace sucks
	allow su_daemon "$1" "dir" "search"
	allow su_daemon "$1" "file" "read open"
	allow su_daemon "$1" "lnk_file" "read"
	allow su_daemon su_daemon "capability" "sys_ptrace"

	#TODO: Split in for su/su_sensitive/su_cts
	allow su "$1" "fd" "use"
	allow su "$1" "fifo_file" "read write"
}

suDaemonTo() {
	allow su_daemon $1 "process" "transition"
	noaudit su_daemon $1 "process" "siginh rlimitinh noatsecure"
}

suDaemonRights() {
	allow su_daemon rootfs file "entrypoint"

	allow su_daemon su_daemon "dir" "search read"
	allow su_daemon su_daemon "file" "read write open"
	allow su_daemon su_daemon "lnk_file" "read"
	allow su_daemon su_daemon "unix_dgram_socket" "create connect write"
	allow su_daemon su_daemon "unix_stream_socket" "$create_stream_socket_perms"

	allow su_daemon devpts chr_file "read write open getattr"
	#untrusted_app_devpts not in Android 4.4
	allow su_daemon untrusted_app_devpts chr_file "read write open getattr" || true

	allow su_daemon su_daemon "capability" "setuid setgid"

	#Access to /data/data/me.phh.superuser/xxx
	allow su_daemon app_data_file "dir" "getattr search write add_name"
	allow su_daemon app_data_file "file" "getattr read open lock"

	#FIXME: This shouldn't exist
	#dac_override can be fixed by having pts_slave's fd forwarded over socket
	#Instead of forwarding the name
	allow su_daemon su_daemon "capability" "dac_override"

	allow su_daemon su_daemon "process" "fork sigchld"

	#toolbox needed for log
	allow su_daemon toolbox_exec "file" "execute read open execute_no_trans" || true

	#Create /dev/me.phh.superuser. Could be done by init
	allow su_daemon device "dir" "write add_name"
	allow su_daemon su_device "dir" "create setattr remove_name add_name"
	allow su_daemon su_device "sock_file" "create unlink"

	#Allow su daemon to start su apk
	allow su_daemon zygote_exec "file" "execute read open execute_no_trans"

	#Send request to APK
	allow su_daemon su_device dir "search write add_name"

	#Allow su_daemon to switch to su or su_sensitive
	allow su_daemon su_daemon "process" "setexec"

	#Allow su_daemon to execute a shell (every commands are supposed to go through a shell)
	allow su_daemon shell_exec file "execute read open"

	allow su_daemon su_daemon "capability" "chown"

	suDaemonTo su
}
