typedef unsigned short int sa_family_t;
//Linux includes
#define _LINUX_TIME_H
#define _GNU_SOURCE
#define MNT_DETACH 2
#define XATTR_NAME_SELINUX "security.selinux"
#define DEFAULT_CONTEXT "u:object_r:rootfs:s0"
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
#include <dirent.h>
#include <sys/types.h>
#include <sys/xattr.h>

//WARNING: Calling this will change our current namespace
//We don't care because we don't want to run from here anyway
int disableSu(int pid) {
	char buffer[512];
	char *p, *p2;
	int ret = 0;
	snprintf(buffer, sizeof(buffer), "/proc/%d/ns/mnt", pid);
	int fd = open(buffer, O_RDONLY);
	if(fd == -1) return 2;
	int res = syscall(SYS_setns, fd, 0);
	close(fd);
	if(res == -1) return 3;

	// unmount the tmpfs from /sbin
	res = umount2("/sbin", MNT_DETACH);
	if(res == -1) ret |= 4;

	// unmount anything under /system/
	snprintf(buffer, sizeof(buffer), "/proc/%d/mounts", pid);
	FILE *mf = fopen(buffer, "r");
	if(mf == NULL) return ret | 8;
	while(fgets(buffer, sizeof(buffer), mf) != NULL) {
		if((p = strchr(buffer, ' ')) == NULL) continue;
		p++;
		if((p2 = strchr(p, ' ')) == NULL) continue;
		*p2 = 0;
		if(p2-p>8 && strncmp(p, "/system/", 8)==0) {
			res = umount2(p, MNT_DETACH);
			if(res == -1) ret |= 16;
		}
	}
	fclose(mf);

	return ret;
}

int makeSbinTmpfs() {
	DIR *sd;
	struct dirent *dir;
	struct stat sb, sb2;
	char sfile[256];
	char dfile[256];
	char buffer[256];
	int sfd, dfd, res;
	size_t bytes, copied;

	// check that /sbin is NOT already mounted
	stat("/", &sb);
	stat("/sbin", &sb2);
	if(sb.st_dev != sb2.st_dev) return 0;

	strcpy(sfile, "/sbin/");
	strcpy(dfile, "/dev/sbin.tmp/");
	mkdir("/dev/sbin.tmp", 0700);
	if(mount("tmpfs", "/dev/sbin.tmp", "tmpfs", 0, "size=64m,nr_inodes=8192,mode=755,uid=0,gid=0") == -1) return 1;

	res = getxattr("/sbin", XATTR_NAME_SELINUX, buffer, sizeof(buffer) - 1);
	if(res != -1) {
		buffer[res] = 0;
		res = setxattr("/dev/sbin.tmp", XATTR_NAME_SELINUX, buffer, strlen(buffer) + 1, 0);
	}
	if(res == -1) {
		setxattr("/dev/sbin.tmp", XATTR_NAME_SELINUX, DEFAULT_CONTEXT, strlen(DEFAULT_CONTEXT) + 1, 0);
	}

	if((sd = opendir("/sbin")) == NULL) {
		umount2("/dev/sbin.tmp", MNT_DETACH);
		return 2;
	}
	while((dir = readdir(sd)) != NULL) {
		// copy files (with symlink support), copy mode/owner + SELinux context
		strncpy(sfile+6, dir->d_name, sizeof(sfile)-6);
		strncpy(dfile+14, dir->d_name, sizeof(dfile)-14);
		switch(dir->d_type) {
		case DT_REG:
			sfd = open(sfile, O_RDONLY);
			if(sfd == -1 || fstat(sfd, &sb) == -1) {
				umount2("/dev/sbin.tmp", MNT_DETACH);
				return 3;
			}
			if((dfd = open(dfile, O_WRONLY | O_CREAT | O_EXCL, sb.st_mode)) == -1) {
				umount2("/dev/sbin.tmp", MNT_DETACH);
				return 3;
			}
			copied = 0;
			while((bytes = sendfile(dfd, sfd, NULL, (size_t) sb.st_size)) > 0) copied += bytes;
			if(copied != (size_t) sb.st_size) {
				umount2("/dev/sbin.tmp", MNT_DETACH);
				return 3;
			}
			fchown(dfd, sb.st_uid, sb.st_gid);
			fchmod(dfd, sb.st_mode);
			res = fgetxattr(sfd, XATTR_NAME_SELINUX, buffer, sizeof(buffer) - 1);
			if(res != -1) {
				buffer[res] = 0;
				res = fsetxattr(dfd, XATTR_NAME_SELINUX, buffer, strlen(buffer) + 1, 0);
			}
			if(res == -1) {
				fsetxattr(dfd, XATTR_NAME_SELINUX, DEFAULT_CONTEXT, strlen(DEFAULT_CONTEXT) + 1, 0);
			}
			close(sfd);
			close(dfd);
			break;
		case DT_LNK:
			if((res = readlink(sfile, buffer, sizeof(buffer)-1)) == -1) {
				umount2("/dev/sbin.tmp", MNT_DETACH);
				return 3;
			}
			buffer[res] = 0;
			if(symlink(buffer, dfile) == -1) {
				umount2("/dev/sbin.tmp", MNT_DETACH);
				return 3;
			}
			if(lstat(sfile, &sb) == 0) lchown(dfile, sb.st_uid, sb.st_gid);
			res = lgetxattr(sfile, XATTR_NAME_SELINUX, buffer, sizeof(buffer) - 1);
			if(res != -1) {
				buffer[res] = 0;
				res = lsetxattr(dfile, XATTR_NAME_SELINUX, buffer, strlen(buffer) + 1, 0);
			}
			if(res == -1) {
				lsetxattr(dfile, XATTR_NAME_SELINUX, DEFAULT_CONTEXT, strlen(DEFAULT_CONTEXT) + 1, 0);
			}
			break;
		}
	}
	closedir(sd);
	mount("tmpfs", "/dev/sbin.tmp", "tmpfs", MS_REMOUNT | MS_RDONLY, "size=64m,nr_inodes=8192,mode=755,uid=0,gid=0");
	if(mount("/dev/sbin.tmp", "/sbin", NULL, MS_BIND, NULL) == -1) {
		umount2("/dev/sbin.tmp", MNT_DETACH);
		return 4;
	}
	umount2("/dev/sbin.tmp", MNT_DETACH);
	rmdir("/dev/sbin.tmp");

	return 0;
}

int main(int argc, char **argv, char **envp) {
	// rise priority to get advantage in race conditions
	nice(-15);
	if(makeSbinTmpfs() != 0) {
		usleep(200 * 1000);
		return 1;
	}

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

			kill(pid, SIGSTOP);
			printf("Disabling for PID = %d, UID = %d\n", pid, uid);
			disableSu(pid);
			kill(pid, SIGCONT);
		}
	}

	return 0;
}
