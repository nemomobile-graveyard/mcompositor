#!/usr/bin/python

# Check that selective compositing works for _MEEGO_LOW_POWER_MODE windows.

#* Test steps
#  * ensure the display is on
#  * show ARGB application window
#  * check that the window is composited
#  * set _MEEGO_LOW_POWER_MODE=1 on the window
#  * check that the window is not composited
#  * turn the display off
#  * check that the window is not composited
#  * set _MEEGO_LOW_POWER_MODE=0 on the window
#  * check that the window is composited
#  * set _MEEGO_LOW_POWER_MODE=1 on the window
#* Post-conditions
#  * check that the window is not composited

import os, re, sys, time

if os.system('mcompositor-test-init.py'):
  sys.exit(1)

# enable debug code in mcompositor
os.popen('kill -SIGUSR1 `pidof mcompositor`')

fd = os.popen('windowctl akn')
win = fd.readline().strip()
fd = os.popen('windowctl E %s 10' % win)  # raise it above touch screen lock
time.sleep(2)

ret = 0
def check_redir(app_win, str, story):
  global ret
  fd = os.popen('xprop -id %s' % app_win)
  s = fd.read(5000)
  found = 0
  for l in s.splitlines():
    if re.search(str, l.strip()):
      found = 1
      break
  if found == 0:
    print 'FAIL "%s": test app is not' % story, str
    ret = 1

# check that test app is redirected
check_redir(win, 'WINDOW_COMPOSITED', 'composited when mapped')

os.popen("xprop -id %s -f _MEEGO_LOW_POWER_MODE 32c -set _MEEGO_LOW_POWER_MODE 1" % win)
time.sleep(1)

# check that test app is not redirected
check_redir(win, 'WINDOW_DIRECT', 'display on & _MEEGO_LOW_POWER_MODE=1')

# turn the display off
os.popen('/sbin/mcetool --blank-screen')
time.sleep(1)

# check that test app is not redirected
check_redir(win, 'WINDOW_DIRECT', 'display off & _MEEGO_LOW_POWER_MODE=1')

os.popen("xprop -id %s -f _MEEGO_LOW_POWER_MODE 32c -set _MEEGO_LOW_POWER_MODE 0" % win)
time.sleep(1)

# check that test app is redirected
check_redir(win, 'WINDOW_COMPOSITED', 'display off & _MEEGO_LOW_POWER_MODE set 0')

os.popen("xprop -id %s -f _MEEGO_LOW_POWER_MODE 32c -set _MEEGO_LOW_POWER_MODE 1" % win)
time.sleep(1)

# check that test app is not redirected
check_redir(win, 'WINDOW_DIRECT', 'display off & _MEEGO_LOW_POWER_MODE set 1')

# cleanup
os.popen('pkill windowctl')

# disable debug code in mcompositor
os.popen('kill -SIGUSR1 `pidof mcompositor`')
# turn the display back on
os.popen('/sbin/mcetool --unblank-screen --set-tklock-mode=unlocked')
time.sleep(1)

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
