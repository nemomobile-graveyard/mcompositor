#!/bin/sh -e
#
# Create a little override-redirected window and move it around until
# it has been everywhere on the screen, and in each step verify that
# the screen looks different by taking screenshots.  If it has not
# changed mcompositor must have painted it wrong.

# The number of steps the test should be completed.
steps=$1;
[ $# -gt 0 ] || steps=5;

# Get the screen size, the area to be traversed.
set -- `windowctl D`;
scrw=$1;
scrh=$2;

# The size of the window we're moving around.
winw=$((scrw * 15 / ($steps*10)));
winh=$((scrh * 15 / ($steps*10)));

# Take note of the original $notifs setting and restore it when we exit.
notifs="/desktop/meego/notifications/previews_enabled";
enabled=`gconftool-2 -g "$notifs"`;
trap '
	rm -f "$shot0" "$shot1" "$shot2";
	gconftool -s -t bool "$notifs" "$enabled";
' EXIT;
mcompositor-test-init.py;

# Take a screenshot of the unaltered scene.
shot0="/var/tmp/mctest-0.png";
shot1="/var/tmp/mctest-1.png";
shot2="/var/tmp/mctest-2.png";
windowctl -shot "$shot0";

# Run
windowctl o | (
	trap "pkill windowctl" EXIT;
	read win;

	# Move $win systematically around and verify that the scene looks
	# different every step.
	for x in `seq 0 $((scrw / $steps)) $((scrw-1))`;
	do
		for y in `seq 0 $((scrh / $steps)) $((scrh-1))`;
		do
			geo="${winw}x${winh}+$x+$y";
			windowctl g $win $x $y $winw $winh;
			windowctl -fill HotPink$((1+RANDOM%4)) $win;
			sleep 0.1;
			windowctl -shot "$shot2";

			if cmp -s "$shot2" "$shot0";
			then
				echo "window didn't appear at $geo" >&2;
				exit 1;
			elif [ "$shot1" != "" ] && cmp -s "$shot2" "$shot1";
			then
				echo "window didn't move to $geo" >&2;
				exit 1;
			fi

			tmp="$shot1";
			shot1="$shot2";
			shot2="$tmp";
		done
	done
);

# End of test26.sh
