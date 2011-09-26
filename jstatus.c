/*
 * Copyright (c) 2011, Julien Laffaye <jlaffaye@FreeBSD.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/dkstat.h>
#include <sys/soundcard.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <xosd.h>

#define DZEN_CMD "dzen2 -x 1400 -y 1061"
#define SYSCTL_TEMP "hw.acpi.thermal.tz1.temperature"
#define SYSCTL_BAT_STATE "hw.acpi.battery.state"
#define SYSCTL_BAT_LIFE "hw.acpi.battery.life"
#define SYSCTL_BAT_TIME "hw.acpi.battery.time"
#define SYSCTL_CPU_TIME "kern.cp_time"

#define XBM_PATH "/usr/home/jlaffaye/.dzen/"
#define ICON_POWER_AC XBM_PATH "power-ac.xbm"
#define ICON_POWER_BAT XBM_PATH "power-bat2.xbm"
#define ICON_VOLUME_HIGH XBM_PATH "vol-hi.xbm"
#define ICON_VOLUME_MUTE XBM_PATH "vol-mute.xbm"
#define ICON_TEMP XBM_PATH "temp.xbm"
#define ICON_LOAD XBM_PATH "load.xbm"
#define DOT "^p(5)^c(5)^p(5)"

static void
print_icon(FILE *fp, const char *path)
{
	fprintf(fp, "^i(%s) ", path);
}

static void
print_dot(FILE *fp)
{
	fprintf(fp, " %s ", DOT);
}


static void
show_message(const char *msg)
{
	static xosd *osd;

	if (osd == NULL) {
		osd = xosd_create(1);

		xosd_set_align(osd, XOSD_right);
		xosd_set_pos(osd, XOSD_bottom);
		xosd_set_vertical_offset(osd, 20);

		xosd_set_font(osd, "-adobe-helvetica-bold-r-normal-*-*-240-*-*-p-*-iso8859-1"); 
		xosd_set_shadow_offset(osd, 4);

		xosd_set_timeout(osd, 4);
	}

	xosd_display(osd, 0, XOSD_string, msg);
}

#define STATE_AC 0
#define STATE_CHARGING 6
#define STATE_DISCHARGING 1
static void
print_bat(FILE *fp)
{
	static int prev_state;
	size_t sz = sizeof(int);
	int state, life, remaining;
	int hours, minutes;

	sysctlbyname(SYSCTL_BAT_STATE, &state, &sz, NULL, 0);

	if (state == STATE_DISCHARGING || state == STATE_CHARGING) {
		sysctlbyname(SYSCTL_BAT_LIFE, &life, &sz, NULL, 0);
	} else {
		life = 100;
	}

	if (state == STATE_DISCHARGING && prev_state != STATE_DISCHARGING) {
		sysctlbyname(SYSCTL_BAT_TIME, &remaining, &sz, NULL, 0);
		hours = remaining / 60;
		minutes = remaining % 60;
		show_message("ON BATTERY");
	}

	if (life < 10) {
		show_message("PLUG AC!");
	}

	if (state == STATE_AC || state == STATE_CHARGING) {
		print_icon(fp, ICON_POWER_AC);
	} else if (state == STATE_DISCHARGING) {
		print_icon(fp, ICON_POWER_BAT);
	} else {
		fprintf(fp, "[?] ");
	}

	fprintf(fp, "%d%%", life);
	prev_state = state;
}

#define TZ_ZEROC 2732
#define TZ_KELVTOC(x) (((x) - TZ_ZEROC) / 10), abs(((x) - TZ_ZEROC) % 10)
static void
print_temp(FILE *fp)
{
	static int prev_temp;
	int temp;
	size_t sz = sizeof(temp);

	sysctlbyname(SYSCTL_TEMP, &temp, &sz, NULL, 0);
	temp = TZ_KELVTOC(temp);

	if (temp >= 80 && prev_temp < 80) {
		show_message("High temperature");
	}

	print_icon(fp, ICON_TEMP);
	fprintf(fp, "%d C", temp);
	prev_temp = temp;
}

static void
print_cpu_usage(FILE *fp)
{
	static int prev_total;
	static int prev_idle;

	int curr_user = 0, curr_nice = 0, curr_system = 0, curr_idle = 0, curr_total;
	int diff_idle, diff_total, diff_usage;

	long cp_time[CPUSTATES];
	size_t size = sizeof(cp_time);

	sysctlbyname(SYSCTL_CPU_TIME, &cp_time, &size, NULL, 0);

	curr_user = cp_time[CP_USER];
	curr_nice = cp_time[CP_NICE];
	curr_system = cp_time[CP_SYS];
	curr_idle = cp_time[CP_IDLE];
	curr_total = curr_user + curr_nice + curr_system + curr_idle;
	diff_idle  = curr_idle - prev_idle;
	diff_total = curr_total - prev_total;
	diff_usage = (1000 * (diff_total - diff_idle) / diff_total + 5) / 10;
	prev_total = curr_total;
	prev_idle  = curr_idle;

	print_icon(fp, ICON_LOAD);
	fprintf(fp, "%02d%%", diff_usage);
}

static void
print_volume(FILE *fp)
{
	static int fd;
	int vol;

	if (fd == 0) {
		if ((fd = open("/dev/mixer", O_RDWR)) < 0)
			err(1, "open(/dev/mixer)");
	}

	if (ioctl(fd, MIXER_READ(0), &vol) == -1)
		err(1, "ioctl(mixer)");

	vol &= 0x7f;

	if (vol > 0)
		print_icon(fp, ICON_VOLUME_HIGH);
	else
		print_icon(fp, ICON_VOLUME_MUTE);
	fprintf(fp, "%d%%", vol);
}

static void
print_date(FILE *fp)
{
	char buf[128];
	time_t t;
	struct tm *tm;

	t = time(NULL);
	tm = localtime(&t);
	strftime(buf, sizeof(buf), "%d-%m-%Y %k:%M", tm);

	fprintf(fp, "%s", buf);
}

int
main(void)
{
	FILE *fp;

	fp = popen(DZEN_CMD, "w");
	if (fp == NULL)
		err(1, "popen(%s)", DZEN_CMD);

	for (;;) {
		print_bat(fp);
		print_dot(fp);
		print_temp(fp);
		print_dot(fp);
		print_cpu_usage(fp);
		print_dot(fp);
		print_volume(fp);
		print_dot(fp);
		print_date(fp);

		fprintf(fp, "\n");
		fflush(fp);
		sleep(1);
	}

	return 0;
}
