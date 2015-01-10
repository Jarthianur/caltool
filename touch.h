#include <libinput.h>
#include "matrix.h"

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])

/* Our points for the calibration must be not be on a line */
static const struct {
	float x_ratio, y_ratio;
	} test_ratios[] = {
	{ 0.20, 0.40 },
	{ 0.80, 0.60 },
	{ 0.40, 0.80 }
};

struct calibrator {
	struct tests {
		double drawn_x, drawn_y;
		double clicked_x, clicked_y;
	} tests[ARRAY_LENGTH(test_ratios)];
	int current_test;
};

void print_touch_event_with_coords(struct libinput_event *);
int handle_events(struct libinput *, struct calibrator *);
int open_restricted(const char *, int, void *);
void close_restricted(int , void *);
int open_udev(struct libinput **);
void finish_calibration (struct calibrator *, struct weston_matrix *);