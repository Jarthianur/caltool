
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
//#include <linux/vt.h>
//#include <linux/kd.h>
#include <linux/fb.h>

#include "fbutils.h"
#include "touch.h"
#include "matrix.h"

#include <libinput.h>
#include <libudev.h>
#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>

int events=0;
int got_sample = 0;

FILE* fp_log = NULL;

int verbose = 1;
const char *seat = "seat0";
static int palette [] =
{
	0x000000, 0xffe080, 0xffffff, 0xe0c0a0
};
#define NR_COLORS (sizeof (palette) / sizeof (palette [0]))

struct udev *udev;
extern int xres;
extern int yres;

void
sample_cal_values(struct libinput *li, struct calibrator *calibrator)
{
	struct pollfd fds[2];
	sigset_t mask;
	int32_t drawn_x, drawn_y;
	
	fds[0].fd = libinput_get_fd(li);
	fds[0].events = POLLIN;
	fds[0].revents = 0;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);

	fds[1].fd = signalfd(-1, &mask, SFD_NONBLOCK);
	fds[1].events = POLLIN;
	fds[1].revents = 0;

	if (fds[1].fd == -1 ||
	    sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
		fprintf(stderr, "Failed to set up signal handling (%s)\n",
				strerror(errno));
	}

	/* Handle already-pending device added events */
	if (handle_events(li, calibrator))
		fprintf(stderr, "Expected device added events on startup but got none. "
				"Maybe you don't have the right permissions?\n");
	
	// reset test to 0
	calibrator->current_test = 0;
	
	// got samples values defined in test_ratios
	while(calibrator->current_test < ARRAY_LENGTH(test_ratios))
	{
		// Calculate x,y coordinates for cross
		drawn_x = test_ratios[calibrator->current_test].x_ratio * xres;
		drawn_y = test_ratios[calibrator->current_test].y_ratio * yres;
		
		// save values for later calculations
		calibrator->tests[calibrator->current_test].drawn_x = drawn_x;
		calibrator->tests[calibrator->current_test].drawn_y = drawn_y;
		
		// draw cross on actual position
		put_cross(drawn_x, drawn_y, 2 | XORMODE);
		
		// reset wait for touch event
		got_sample = 0;
		
		// wait for touch event
		while (poll(fds, 2, -1) > -1) {
			if (fds[1].revents)
				break;
			if (got_sample > 0)
				break;
				
			handle_events(li, calibrator);
		
		}
		// clear cross on actual position
		put_cross(drawn_x, drawn_y, 2 | XORMODE);
		
		// next test set
		calibrator->current_test++;
	}
	
	close(fds[1].fd);
}

int main()
{
	// libinput
	struct libinput *li;
	struct calibrator calibrator;
	struct weston_matrix cal_matrix;
	
	// general use
	unsigned int i;
	char buf[10];
	int nread;
	
	FILE* fd = NULL;
	FILE* fp_template = NULL;
	FILE* fp_udev = NULL;
	
	char cal_file[] = "touch.cal";
	char udev_template[] = "touchscreen.rules.template";
	char udev_rule[] = "touchscreen.rules";
	
	// initialize matrix
	memset(&cal_matrix, 0, sizeof(cal_matrix));
	
	// open file for logging
	fp_log=fopen("caltool.log","w");
	if (fp_log == NULL)
	{
		printf("Error opening logfile !!\n");
		return 1;
	}	
	fprintf(fp_log, "Start calibration ...\n");
	
	// open framebuffer
	if (open_framebuffer()) {
		close_framebuffer();
		exit(1);
	}
	
	for (i = 0; i < NR_COLORS; i++)
		setcolor (i, palette [i]);
	
	//log screen size 
	fprintf(fp_log,"detected Resolution: x=%d y=%d\n", xres, yres);
	
	put_string_center (xres / 2, yres / 4, "Touch Calibration Tool", 1);
	put_string_center (xres / 2, yres / 4 + 20, "Touch crosshair to calibrate", 2);
	
	//open libinput device
	if (open_udev(&li))
			return 1;

	// sample values for calibration
	sample_cal_values(li, &calibrator);
			
	// calculate calibration values
	finish_calibration(&calibrator, &cal_matrix);
	
	// write calibration values to file
	/*fd = fopen(cal_file,"w");
	fprintf(fd,"%f %f %f %f %f %f\n", x_calib.f[0], x_calib.f[1], (x_calib.f[2]/xres), y_calib.f[0], y_calib.f[1], (y_calib.f[2]/yres));
	fclose(fd);*/
	
	// open udev files
	fp_template = fopen(udev_template, "r");
	if (fp_template == NULL) {
		fprintf(fp_log, "Error opening template file !!\n");
	}
	
	fp_udev = fopen(udev_rule, "w");
	if (fp_udev == NULL) {
		fprintf(fp_log, "Error opening udev output file !!\n");
	}
	
	// combine template and calibration data
	if ((fp_template != NULL) && (fp_udev != NULL))
	{
		// copy template to output file
		while((nread = fread(buf, 1, sizeof(buf), fp_template)) > 0) {
			fwrite(buf, 1, nread, fp_udev);
		}
		
		// 
		fseek(fp_udev, -1, SEEK_CUR);
		
		// add calibration data
		fprintf(fp_udev,"ENV{LIBINPUT_CALIBRATION_MATRIX}=");
		fprintf(fp_udev,"\"%f %f %f %f %f %f\"", 
			cal_matrix.d[0], cal_matrix.d[4], cal_matrix.d[8],
			cal_matrix.d[1], cal_matrix.d[5], cal_matrix.d[9]);
	}

	// close files
	fclose(fp_udev);
	fclose(fp_template);
				
	// close udev
	libinput_unref(li);
	if (udev)
		udev_unref(udev);
		
	// close framebuffer
	close_framebuffer();

	// close logfile
	fclose(fp_log);
	return 0;
}