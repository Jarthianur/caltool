#!/bin/bash

# copy zero cal to udev
cp touchscreen.rules.template /etc/udev/rules.d/touchscreen.rules

#udpate rules to zero calibration
udevadm control --reload-rules
udevadm trigger

# call caltool
./caltool -r portrait

#copy new values to udev
cp touchscreen.rules /etc/udev/rules.d/

#udpate rules to new calibration
udevadm control --reload-rules
udevadm trigger