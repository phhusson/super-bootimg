typedef unsigned short int sa_family_t;
//Linux includes
#define _LINUX_TIME_H
#define _GNU_SOURCE
#define MNT_DETACH 2
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>
#include <linux/netlink.h>
#include <linux/fs.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <sys/glibc-syscalls.h>
#include <asm/unistd.h>
#include <strings.h>

//WARNING: Calling this will change our current namespace
//We don't care because we don't want to run from here anyway
int disableSu(int pid) {
	char path[64];
	char buffer[512];
	char *p, *p2;
	int ret = 0;
	snprintf(path, 64, "/proc/%d/ns/mnt", pid);
	int fd = open(path, O_RDONLY);
	if(fd == -1) return 2;
	int res = syscall(SYS_setns, fd, 0);
	if(res == -1) return 3;

	// unmount the tmpfs from /sbin
	res = umount("/sbin", MNT_DETACH);
	if(res == -1) ret |= 4;

	// unmount anything under /system/
	snprintf(path, 64, "/proc/%d/mounts", pid);
	FILE *mf = fopen(path, "r");
	if(mf == NULL) return ret | 8;
	while(fgets(buffer, sizeof(buffer), mf) != NULL) {
		if((p = strchr(buffer, ' ')) == NULL) continue;
		p++;
		if((p2 = strchr(p, ' ')) == NULL) continue;
		*p2 = 0;
		if(strncmp(p, "/system/", 8)==0 && p2-p>8) {
			res = umount2(p, MNT_DETACH);
			if(res == -1) ret |= 16;
		}
	}
	fclose(mf);

	return ret;
}

int main(int argc, char **argv, char **envp) {
	FILE *p = popen("while true;do logcat -b events -v raw -s am_proc_start;sleep 1;done", "r");
	while(!feof(p)) {
		//Format of am_proc_start is (as of Android 5.1 and 6.0)
		//UserID, pid, unix uid, processName, hostingType, hostingName
		char buffer[512];
		fgets(buffer, sizeof(buffer), p);

		{
			char *pos = buffer;
			while(1) {
				pos = strchr(pos, ',');
				if(pos == NULL)
					break;
				pos[0] = ' ';
			}
		}

		int user, pid, uid;
		char processName[256], hostingType[16], hostingName[256];
		int ret = sscanf(buffer, "[%d %d %d %256s %16s %256s]",
				&user, &pid, &uid,
				processName, hostingType, hostingName);


		if(ret != 6) {
			printf("sscanf returned %d on '%s'\n", ret, buffer);
			continue;
		}
		if(
				strcmp(processName, "com.google.android.gms.unstable") == 0 ||

				strcmp(processName, "com.att.tv") == 0 ||
				strcmp(processName, "com.bskyb.skygo") == 0 ||

				strcmp(processName, "com.starfinanz.smob.android.sbanking") == 0 ||
				strcmp(processName, "com.starfinanz.smob.android.sfinanzstatus") == 0 ||
				strcmp(processName, "com.starfinanz.smob.android.sfinanzstatus.tablet") == 0 ||

				strcmp(processName, "com.airwatch.androidagent") == 0
				) {

			printf("Disabling for PID = %d, UID = %d\n", pid, uid);
			disableSu(pid);
		}
	}

	return 0;
}
