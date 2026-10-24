/*
 * Gate 0 test init for the Echo Dot's 4.9 kernel. It shows its progress on
 * the LED ring, saves what the kernel found to /cache/k49-test and reboots
 * into recovery, so TWRP can read the results.
 *
 * Yellow: init runs. Green: results saved. Red: /cache was not found.
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/klog.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <linux/reboot.h>

#define LED_FRAME "/sys/bus/i2c/devices/0-003f/frame"
#define OUT "/cache/k49-test"

static void ring(const char *rgb)
{
	char frame[12 * 6 + 1];
	int fd, i;

	for (i = 0; i < 12; i++)
		memcpy(frame + i * 6, rgb, 6);
	frame[72] = '\0';
	fd = open(LED_FRAME, O_WRONLY);
	if (fd >= 0) {
		write(fd, frame, 72);
		close(fd);
	}
}

static void copy(const char *from, const char *to)
{
	char buf[4096];
	int in = open(from, O_RDONLY), out;
	ssize_t n;

	if (in < 0)
		return;
	out = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out >= 0) {
		while ((n = read(in, buf, sizeof(buf))) > 0)
			write(out, buf, n);
		close(out);
	}
	close(in);
}

static void list(const char *dir, const char *to)
{
	DIR *d = opendir(dir);
	FILE *f = fopen(to, "w");
	struct dirent *e;

	if (d && f)
		while ((e = readdir(d)))
			if (e->d_name[0] != '.')
				fprintf(f, "%s\n", e->d_name);
	if (d)
		closedir(d);
	if (f)
		fclose(f);
}

/* Create /dev/cache for the partition the GPT names cache. */
static int find_cache(void)
{
	char path[256], line[128];
	int major = -1, minor = -1, found = 0;
	struct dirent *e;
	DIR *d = opendir("/sys/class/block");
	FILE *f;

	if (!d)
		return -1;
	while ((e = readdir(d)) && !found) {
		snprintf(path, sizeof(path), "/sys/class/block/%s/uevent", e->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		major = minor = -1;
		while (fgets(line, sizeof(line), f)) {
			sscanf(line, "MAJOR=%d", &major);
			sscanf(line, "MINOR=%d", &minor);
			if (!strcmp(line, "PARTNAME=cache\n"))
				found = 1;
		}
		fclose(f);
	}
	closedir(d);
	if (!found)
		return -1;
	return mknod("/dev/cache", S_IFBLK | 0600, makedev(major, minor));
}

int main(void)
{
	static char log[1 << 20];
	int i, n, fd;

	mkdir("/dev", 0755);
	mkdir("/proc", 0755);
	mkdir("/sys", 0755);
	mkdir("/cache", 0755);
	mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
	mount("proc", "/proc", "proc", 0, NULL);
	mount("sysfs", "/sys", "sysfs", 0, NULL);
	ring("202000");

	/* The eMMC and the LED ring may still be probing. */
	for (i = 0; i < 30 && find_cache() < 0; i++)
		sleep(1);
	if (mount("/dev/cache", "/cache", "ext4", 0, NULL)) {
		ring("200000");
		sleep(30);
		syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
			LINUX_REBOOT_CMD_RESTART2, "recovery");
		return 1;
	}
	mkdir(OUT, 0755);

	/* Give the drivers time to probe before saving the log. */
	sleep(10);
	n = klogctl(10 /* SYSLOG_ACTION_SIZE_BUFFER */, NULL, 0);
	n = klogctl(3 /* SYSLOG_ACTION_READ_ALL */, log,
		    n > 0 && n < (int)sizeof(log) ? n : (int)sizeof(log));
	fd = open(OUT "/dmesg.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0 && n > 0) {
		write(fd, log, n);
		close(fd);
	}
	copy("/proc/cmdline", OUT "/cmdline.txt");
	copy("/proc/meminfo", OUT "/meminfo.txt");
	copy("/proc/iomem", OUT "/iomem.txt");
	copy("/proc/asound/cards", OUT "/asound-cards.txt");
	copy("/proc/asound/pcm", OUT "/asound-pcm.txt");
	copy("/proc/interrupts", OUT "/interrupts.txt");
	copy("/proc/slabinfo", OUT "/slabinfo.txt");
	list("/sys/bus/i2c/devices", OUT "/i2c-devices.txt");
	list("/sys/class/leds", OUT "/leds.txt");
	list("/sys/class/input", OUT "/input.txt");
	sync();
	umount("/cache");
	sync();

	ring("002000");
	sleep(20);
	syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		LINUX_REBOOT_CMD_RESTART2, "recovery");
	return 0;
}
