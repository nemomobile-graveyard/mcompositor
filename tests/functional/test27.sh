#!/bin/sh
#
# Test that restarting mc doesn't change window states
# and doesn't switch away from the desktop.

TOP="_MEEGOTOUCH_CURRENT_APP_WINDOW";
get_topmost_window()
{
	xprop -root -notype -f "$TOP" 32x ' $0' "$TOP";
}

assert()
{
	what="$1";
	actual="$2";
	expected="$3";
	if [ "$actual" != "$expected" ];
	then
		echo "$what" should be "$expected" but it is "$actual" >&2;
		exit 1;
	fi
}

assert_topmost_window()
{
	win="$1";

	set -- `get_topmost_window`;
	assert "root:$TOP" "$2" "$win";
}

assert_window_state()
{
	state="$1"; shift;

	prop="WM_STATE";
	for win in "$@";
	do
		set -- `xprop -id "$win" -notype -format "$prop" 32i \
			' ?$0=0(Withdrawn)?$0=1(Normal)?$0=3(Iconic)' "$prop"`;
		assert "$win:$prop" "$2" "$state";
	done
}

assert_window_type()
{
	wintype="$1"; shift;

	prop="_NET_WM_WINDOW_TYPE";
	for win in "$@";
	do
		set -- `xprop -id "$win" -notype -f "$prop" 32a ' $0' "$prop"`;
		assert "$win:$prop" "$2" "$wintype";
	done
}

restart_mc()
{
	mrc="/tmp/mrc";
	if [ -p "$mrc" ];
	then
		echo "restart" > "$mrc";
	else
		pkill mcompositor;
	fi
}

patience() { sleep 1.5; }

# Init
# Take note of the original $notifs setting and restore it when we exit.
notifs="/desktop/meego/notifications/previews_enabled";
enabled=`gconftool-2 -g "$notifs"`;
trap 'gconftool -s -t bool "$notifs" "$enabled"' EXIT;
mcompositor-test-init.py;

# Test
(
	# Create two application windows.
	windowctl kn & patience;
	windowctl kn & patience;
) | (
        trap "pkill windowctl" EXIT;
	read win1; read win2; patience;

	# Verify that the latter window is the topmost
	# and that it's in Normal state.
	assert_topmost_window "$win2";
	assert_window_state Normal "$win2";

	# Go to the desktop and verify that the windows are Iconic.
	windowctl O "$win2"; patience;
	assert_window_state Iconic "$win1" "$win2";
	set -- `get_topmost_window`;
	assert_window_type _NET_WM_WINDOW_TYPE_DESKTOP "$2";
	home="$2";

	# Restart mc and verify that the desktop is still on top,
	# and that the windows are still Iconic.
	restart_mc; patience;
	assert_topmost_window "$home";
	assert_window_state Iconic "$win1" "$win2";                                  

	# Top the firstly created window and verify that the other
	# is still Iconic.
	windowctl A "$win1"; patience;
	assert_topmost_window "$win1";
	assert_window_state Normal "$win1";
	assert_window_state Iconic "$win2";

	# Restart mc and verify that the top window and the window states
	# didn't change.
	restart_mc; patience;
	assert_topmost_window "$win1";                                               
	assert_window_state Normal "$win1";                                          
	assert_window_state Iconic "$win2";       

	echo ok;
)

# End of test27.sh
