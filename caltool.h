struct calibrator {
	struct tests {
		int32_t drawn_x, drawn_y;
		int32_t clicked_x, clicked_y;
	} tests[ARRAY_LENGTH(test_ratios)];
	int current_test;
};