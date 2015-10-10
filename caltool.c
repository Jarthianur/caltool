/*  
	caltool - Touch screen calibration tool for XCSoar Glide Computer - http://www.openvario.org/
    Copyright (C) 2014  The openvario project
    A detailed list of copyright holders can be found in the file "AUTHORS" 

    This program is free software; you can redistribute it and/or 
    modify it under the terms of the GNU General Public License 
    as published by the Free Software Foundation; either version 3
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, see <http://www.gnu.org/licenses/>.
*/


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
#include <linux/fb.h>

#include "fbutils.h"
#include "touch.h"
#include "matrix.h"
#include "cmdline_parser.h"

#include <libinput.h>
#include <libudev.h>
#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>

int events=0;
int got_sample = 0;

FILE* fp_log = NULL;
FILE* cal_file = NULL;

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

int main(int argc, char **argv)
{
	// libinput
	struct libinput *li;
	struct calibrator calibrator;
	struct weston_matrix cal_matrix;
	
	// general use
	unsigned int i;
	char buf[10];
	int nread;
	int rotation=0;
	int use_calfile=0;
	
	FILE* fp_template = NULL;
	FILE* fp_udev = NULL;
	FILE* fp_cal = NULL;
	
	char cal_file[255]="";
	char udev_template[] = "touchscreen.rules.template";
	char udev_rule[] = "touchscreen.rules";
	
	// initialize matrix
	memset(&cal_matrix, 0, sizeof(cal_matrix));
	cal_matrix.d[15] = 1;
	
	// open file for logging
	fp_log=fopen("caltool.log","w");
	if (fp_log == NULL)
	{
		printf("Error opening logfile !!\n");
		return 1;
	}

	// get commandline options
	cmdline_parser(argc, argv, &rotation, &use_calfile, cal_file);
	
	fprintf(fp_log,"Using cal file: %s\n", cal_file);
	
	if ( *cal_file == '\n')
	{
		printf("Error no cal file specified !!\n");
		return 1;
	}
	
	// check if we use an external cal file
	if (use_calfile == 1)
	{
		fprintf(fp_log,"Getting cal from file\n");
		fp_cal = fopen(cal_file,"r");
		fread(&cal_matrix, sizeof(struct weston_matrix), 1, fp_cal);
		
		// rotate matrix
		fprintf(fp_log,"Rotation is: %d\n", rotation);
		
		// rotate matrix if necessary
		rotate_calibration_matrix(&cal_matrix, rotation);
		
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
			fprintf(fp_udev,", ENV{LIBINPUT_CALIBRATION_MATRIX}=");
			fprintf(fp_udev,"\"%f %f %f %f %f %f\"", 
				cal_matrix.d[0], cal_matrix.d[4], cal_matrix.d[8],
				cal_matrix.d[1], cal_matrix.d[5], cal_matrix.d[9]);
		}

		// close files
		fclose(fp_udev);
		fclose(fp_template);
		
	}
	else
	{
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
		
		
		//log parameter
		fprintf(fp_log,"rotation: %d degree\n", rotation);
		
		// print user guideance
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
		fp_cal = fopen(cal_file,"w");
		fwrite(&cal_matrix, sizeof(struct weston_matrix), 1, fp_cal);
		//fprintf(fd,"%f %f %f %f %f %f\n", x_calib.f[0], x_calib.f[1], (x_calib.f[2]/xres), y_calib.f[0], y_calib.f[1], (y_calib.f[2]/yres));
		fclose(fp_cal);
					
		// close udev
		libinput_unref(li);
		if (udev)
			udev_unref(udev);
			
		// close framebuffer
		close_framebuffer();
	}
	// close logfile
	fclose(fp_log);
	return 0;
}