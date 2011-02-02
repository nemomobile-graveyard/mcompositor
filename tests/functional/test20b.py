#!/usr/bin/python

# Check that fullscreen apps during callare decorated.

#* Test steps
#  * simulate an ongoing call
#  * show a fullscreen application window
#* Post-conditions
#  * the decorator is on top of it

import os, re, sys, time

if os.system('mcompositor-test-init.py'):
  sys.exit(1)

fd = os.popen('windowstack m')
s = fd.read(5000)
win_re = re.compile('^0x[0-9a-f]+')
deco_win = 0
for l in s.splitlines():
  if re.search(' viewable mdecorator', l.strip()):
    deco_win = win_re.match(l.strip()).group()

if deco_win == 0:
  print 'FAIL: decorator window not found'
  sys.exit(1)

# simulate a phone call
fd = os.popen('windowctl P')
time.sleep(1)

# create a fullscreen application window
fd = os.popen('windowctl fn')
app_win = fd.readline().strip()
time.sleep(1)

ret = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % deco_win, l.strip()):
    print deco_win, 'found'
    break
  elif re.search("%s " % app_win, l.strip()):
    print 'FAIL: decorator is below the application'
    print 'Failed stack:\n', s
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
os.popen('pkill context-provide')

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
