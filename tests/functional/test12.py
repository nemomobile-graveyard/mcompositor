#!/usr/bin/python

# Check that selective compositing works in most important cases.

#* Test steps
#  * start test app
#  * check that test app is not composited
#  * show an input window transient for the test app
#  * check that test app is composited but the input window is not
#  * unmap the input window
#  * check that test app is not composited
#  * map a new input window and set the transiency after mapping
#  * check that test app is composited but the input window is not
#  * remove transiency and lower the input window
#  * check that test app is not composited
#  * set the input window transient again
#  * check that test app is composited but the input window is not
#  * show a normal (non-fullscreen) application window
#  * check that application is composited (due to the decorator)
#  * iconify the application by raising desktop
#  * check that desktop is not composited
#  * show a meegotouch-looking window
#  * try to close it before the animation ends
#  * check that desktop is not composited
#  * show a non-decorated RGBA application window
#  * check that compositing is on (because the topmost window is RGBA)
#  * set _MEEGOTOUCH_OPAQUE_WINDOW=1 on it
#  * check that compositing is off
#  * set _MEEGOTOUCH_OPAQUE_WINDOW=0 on it
#  * check that compositing is on (because the topmost window is RGBA)
#  * unmap it, set _MEEGOTOUCH_OPAQUE_WINDOW=1, then re-map
#* Post-conditions
#  * check that compositing is off

import os, re, sys, time

if os.system('mcompositor-test-init.py'):
  sys.exit(1)

# enable debug code in mcompositor
os.popen('kill -SIGUSR1 `pidof mcompositor`')

fd = os.popen('windowctl kn')
app_win = fd.readline().strip()
time.sleep(2)

fd = os.popen('windowstack m')
s = fd.read(5000)
win_re = re.compile('^0x[0-9a-f]+')
home_win = 0
for l in s.splitlines():
  if re.search(' DESKTOP viewable ', l.strip()):
    home_win = win_re.match(l.strip()).group()

if home_win == 0:
  print 'FAIL: desktop not found'
  sys.exit(1)

ret = 0
def check_redir(app_win, str, story):
  global ret
  print 'Test:', story
  fd = os.popen('xprop -id %s' % app_win)
  s = fd.read(5000)
  found = 0
  for l in s.splitlines():
    if re.search(str, l.strip()):
      found = 1
      break
  if found == 0:
    print 'FAIL "%s": test app is not' % story, str
    fd = os.popen('windowstack m')
    print 'stack:\n', fd.read(5000)
    ret = 1

# check that test app is not redirected
check_redir(app_win, 'WINDOW_DIRECT', 'test app is not redirected')

# map an input window that is transient to the test app window
input_win = os.popen('windowctl i %s' % app_win).readline().strip()
time.sleep(1)

# check that the test app is not redirected but not the input window
check_redir(app_win, 'WINDOW_COMPOSITED', 'test app with VKB is redirected')
check_redir(input_win, 'WINDOW_DIRECT', 'input window is not redirected')

# unmap the input window
os.popen('windowctl U %s' % input_win)
time.sleep(1)

# check that test app is not redirected
check_redir(app_win, 'WINDOW_DIRECT', 'test app is not redirected 2')

# map a new input window and set the transiency after mapping
input_win = os.popen('windowctl i').readline().strip()
time.sleep(1)
os.popen('windowctl t %s %s' % (input_win, app_win))
time.sleep(1)

# check that the test app is not redirected but not the input window
check_redir(app_win, 'WINDOW_COMPOSITED', 'test app with VKB is redirected 2')
check_redir(input_win, 'WINDOW_DIRECT', 'input window is not redirected 2')

# remove transiency and lower the input window
os.popen('windowctl T %s' % input_win)
os.popen('windowctl L %s None' % input_win)
time.sleep(1)

# check that test app is not redirected
check_redir(app_win, 'WINDOW_DIRECT', 'test app is not redirected 3')

# set the input window transient again
os.popen('windowctl t %s %s' % (input_win, app_win))
time.sleep(1)

# check that the test app is not redirected but not the input window
check_redir(app_win, 'WINDOW_COMPOSITED', 'test app with VKB is redirected 3')
check_redir(input_win, 'WINDOW_DIRECT', 'input window is not redirected 3')

# map new decorated application window
fd = os.popen('windowctl n')
old_win = fd.readline().strip()
time.sleep(2)

# check that the application is redirected
check_redir(old_win, 'WINDOW_COMPOSITED', 'decorated app is redirected')

# raise home
os.popen('windowctl A %s' % home_win)
time.sleep(2)

# check that duihome is not redirected
check_redir(home_win, 'WINDOW_DIRECT', 'desktop is not redirected')

# map an LMT-looking application window and quickly kill it
fd = os.popen('windowctl nk')
old_win = fd.readline().strip()
pidfd = os.popen('pidof windowctl')
pid = pidfd.readline().split()[0]
time.sleep(0.1)
os.popen('kill %s' % pid)
time.sleep(1)

# check that desktop is not redirected
check_redir(home_win, 'WINDOW_DIRECT',
            'desktop is not redirected after aborted animation')

# map new non-decorated application with alpha
fd = os.popen('windowctl eakn')
new_win = fd.readline().strip()
time.sleep(2)

# check that new_win is redirected
check_redir(new_win, 'WINDOW_COMPOSITED', 'RGBA app is redirected')

# set _MEEGOTOUCH_OPAQUE_WINDOW=1
os.popen("xprop -id %s -f _MEEGOTOUCH_OPAQUE_WINDOW 32c "
         "-set _MEEGOTOUCH_OPAQUE_WINDOW 1" % new_win)
time.sleep(1)

# check that new_win is not redirected
check_redir(new_win, 'WINDOW_DIRECT',
            'RGBA app is not redirected when _MEEGOTOUCH_OPAQUE_WINDOW=1')

# set _MEEGOTOUCH_OPAQUE_WINDOW=0
os.popen("xprop -id %s -f _MEEGOTOUCH_OPAQUE_WINDOW 32c "
         "-set _MEEGOTOUCH_OPAQUE_WINDOW 0" % new_win)
time.sleep(1)

# check that new_win is redirected
check_redir(new_win, 'WINDOW_COMPOSITED',
            'RGBA app is redirected when _MEEGOTOUCH_OPAQUE_WINDOW=0')

# unmap it, set _MEEGOTOUCH_OPAQUE_WINDOW=1, then re-map
os.popen('windowctl U %s' % new_win)
os.popen("xprop -id %s -f _MEEGOTOUCH_OPAQUE_WINDOW 32c "
         "-set _MEEGOTOUCH_OPAQUE_WINDOW 1" % new_win)
os.popen('windowctl M %s' % new_win)
time.sleep(2)

# check that new_win is not redirected
check_redir(new_win, 'WINDOW_DIRECT',
            'RGBA app is not redirected when _MEEGOTOUCH_OPAQUE_WINDOW=1 2')

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

# disable debug code in mcompositor
os.popen('kill -SIGUSR1 `pidof mcompositor`')

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
