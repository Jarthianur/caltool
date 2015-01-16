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

#include <libinput.h>
#include <libudev.h>
#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>

#include "touch.h"
#include "matrix.h"


extern int events;
extern struct udev *udev;
extern const char *seat;
extern int got_sample;
extern int xres;
extern int yres;
extern int verbose;
extern FILE *fp_log;


/*
* Calibration algorithm:
*
* The equation we want to apply at event time where x' and y' are the
* calibrated co-ordinates.
*
* x' = Ax + By + C
* y' = Dx + Ey + F
*
* For example "zero calibration" would be A=1.0 B=0.0 C=0.0, D=0.0, E=1.0,
* and F=0.0.
*
* With 6 unknowns we need 6 equations to find the constants:
*
* x1' = Ax1 + By1 + C
* y1' = Dx1 + Ey1 + F
* ...
* x3' = Ax3 + By3 + C
* y3' = Dx3 + Ey3 + F
*
* In matrix form:
*
* x1' x1 y1 1 A
* x2' = x2 y2 1 x B
* x3' x3 y3 1 C
*
* So making the matrix M we can find the constants with:
*
* A x1'
* B = M^-1 x x2'
* C x3'
*
* (and similarly for D, E and F)
*
* For the calibration the desired values x, y are the same values at which
* we've drawn at.
*
*/

void
finish_calibration (struct calibrator *calibrator, struct weston_matrix *cal_matrix)
{
	struct weston_matrix m;
	struct weston_matrix inverse;
	struct weston_vector x_calib, y_calib;
	int i;
	
	/*
	* x1 y1 1 0
	* x2 y2 1 0
	* x3 y3 1 0
	* 0 0 0 1
	*/
	
	// write touched coordinates in Matrix M
	memset(&m, 0, sizeof(m));
	for (i = 0; i < (int)ARRAY_LENGTH(test_ratios); i++) {
		m.d[i] = calibrator->tests[i].clicked_x;
		m.d[i + 4] = calibrator->tests[i].clicked_y;
		m.d[i + 8] = 1;
	}
	m.d[15] = 1;
	
	// calculate M^-1
	weston_matrix_invert(&inverse, &m);
	
	/*printf("Matrix M\n");
	for (i = 0; i < (int)ARRAY_LENGTH(test_ratios)+1; i++) {
		printf("%f \t%f \t%f \t%f\n", m.d[i], m.d[i+4], m.d[i+8], m.d[i+12]);
	}
	
	printf("Matrix M^1\n");
	for (i = 0; i < (int)ARRAY_LENGTH(test_ratios); i++) {
		printf("%f \t%f \t%f \t%f\n", inverse.d[i], inverse.d[i+4], inverse.d[i+8], inverse.d[i+12]);
	}*/
	
	memset(&x_calib, 0, sizeof(x_calib));
	memset(&y_calib, 0, sizeof(y_calib));
	
	for (i = 0; i < (int)ARRAY_LENGTH(test_ratios); i++) {
		x_calib.f[i] = calibrator->tests[i].drawn_x;
		y_calib.f[i] = calibrator->tests[i].drawn_y;
	}

	/* Multiples into the vector */
	weston_matrix_transform(&inverse, &x_calib);
	weston_matrix_transform(&inverse, &y_calib);
	fprintf (fp_log,"Calibration values: %f %f %f %f %f %f\n",
		x_calib.f[0], x_calib.f[1], x_calib.f[2],
		y_calib.f[0], y_calib.f[1], y_calib.f[2]);
	
	
	// save calibration values in matrix	
	cal_matrix->d[0] = x_calib.f[0];
	cal_matrix->d[4] = x_calib.f[1];
	cal_matrix->d[8] = (x_calib.f[2]/xres);
	cal_matrix->d[12] = 0;
	
	cal_matrix->d[1] = y_calib.f[0];
	cal_matrix->d[5] = y_calib.f[1];
	cal_matrix->d[9] = (y_calib.f[2]/yres);
	cal_matrix->d[13] = 0;
	
	cal_matrix->d[2] = 0;
	cal_matrix->d[6] = 0;
	cal_matrix->d[10] = 1;
	cal_matrix->d[14] = 0;
	
	cal_matrix->d[15] = 1;
	
}

void 
rotate_calibration_matrix(struct weston_matrix *cal_matrix, int rotation)
{
	struct weston_matrix rot_matrix;
	
	memset(&rot_matrix, 0, sizeof(rot_matrix));
	
	if (rotation == 90)
	{
		// define rotation matrix
		rot_matrix.d[1] = 1;
		rot_matrix.d[4] = -1;
		rot_matrix.d[8] = 1;
		rot_matrix.d[10] = 1;
		rot_matrix.d[16] = 1;
		
		// multiply matrix
		weston_matrix_multiply(cal_matrix, &rot_matrix);
	}
}

void 
get_touch_coordinates(struct libinput_event *ev, struct calibrator *calibrator)
{
	struct libinput_event_touch *t = libinput_event_get_touch_event(ev);
	double x,x_raw;
	double y,y_raw;
	
	// get current screen coordinates
	x = libinput_event_touch_get_x_transformed(t, xres);
	y = libinput_event_touch_get_y_transformed(t, yres);
	
	x_raw = libinput_event_touch_get_x(t);
	y_raw = libinput_event_touch_get_y(t);
	
	// write to current test ratio
	calibrator->tests[calibrator->current_test].clicked_x = (int) x;
	calibrator->tests[calibrator->current_test].clicked_y = (int) y;
	
	fprintf(fp_log,"Iteration: %d Clicked X,Y: %f (%f), %f (%f)    Drawn X,Y: %f, %f\n",calibrator->current_test, x,x_raw,y,y_raw, calibrator->tests[calibrator->current_test].drawn_x,calibrator->tests[calibrator->current_test].drawn_y);
}

int 
handle_events(struct libinput *li, struct calibrator *calibrator)
{
	int rc = -1;
	struct libinput_event *ev;

	libinput_dispatch(li);
	while ((ev = libinput_get_event(li))) {
		//print_event_header(ev);

		switch (libinput_event_get_type(ev)) {
		case LIBINPUT_EVENT_NONE:
			abort();
		case LIBINPUT_EVENT_DEVICE_ADDED:
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			//print_device_notify(ev);
			break;
		case LIBINPUT_EVENT_KEYBOARD_KEY:
			//print_key_event(ev);
			break;
		case LIBINPUT_EVENT_POINTER_MOTION:
			//print_motion_event(ev);
			break;
		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
			//print_absmotion_event(ev);
			break;
		case LIBINPUT_EVENT_POINTER_BUTTON:
			//print_button_event(ev);
			break;
		case LIBINPUT_EVENT_POINTER_AXIS:
			//print_axis_event(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_DOWN:
			get_touch_coordinates(ev, calibrator);
			got_sample=1;
			break;
		case LIBINPUT_EVENT_TOUCH_MOTION:
			//print_touch_event_with_coords(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_UP:
			//print_touch_event_without_coords(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_CANCEL:
			//print_touch_event_without_coords(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_FRAME:
			//print_touch_event_without_coords(ev);
			break;
		}

		libinput_event_destroy(ev);
		libinput_dispatch(li);
		rc = 0;
	}
	return rc;
}

int
open_restricted(const char *path, int flags, void *user_data)
{
	int fd = open(path, flags);
	return fd < 0 ? -errno : fd;
}

void
close_restricted(int fd, void *user_data)
{
	close(fd);
}

const struct libinput_interface interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

int
open_udev(struct libinput **li)
{
	udev = udev_new();
	if (!udev) {
		fprintf(stderr, "Failed to initialize udev\n");
		return 1;
	}

	*li = libinput_udev_create_context(&interface, NULL, udev);
	if (!*li) {
		fprintf(stderr, "Failed to initialize context from udev\n");
		return 1;
	}

	if (libinput_udev_assign_seat(*li, seat)) {
		fprintf(stderr, "Failed to set seat\n");
		libinput_unref(*li);
		return 1;
	}

	return 0;
}