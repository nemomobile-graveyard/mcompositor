#!/usr/bin/python

# Check that fullscreen apps during call are decorated.

#* Test steps
#  * simulate an ongoing call
#  * show a fullscreen application window
#  * the decorator is on top of it
#  * show a fullscreen application window that paints its statusbar
#* Post-conditions
#  * the decorator is not on top of it

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
import subprocess
ctx = subprocess.Popen(("context-provide",
			"org.freedesktop.ContextKit.Commander",
			"string", "Phone.Call", "active"),
	stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=file("/dev/null"))
while not [ ("Service started" in line) if (line != "")
	    else context_provide_already_running()
	    for line in [ctx.stdout.readline()]][0]: pass
# service started and will be automatically shut down when we exit

# create a fullscreen application window
fd = os.popen('windowctl fn')
app_win = fd.readline().strip()
time.sleep(2)

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

# create a fullscreen application window that paints its statusbar
app2 = os.popen('windowctl fn').readline().strip()
subprocess.call(["xprop", "-id", str(app2), "-f",
                 "_MEEGOTOUCH_MSTATUSBAR_GEOMETRY", "32cccc",
                 "-set", "_MEEGOTOUCH_MSTATUSBAR_GEOMETRY", "0,0,854,36"])
time.sleep(1)

fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % deco_win, l.strip()):
    print 'FAIL: decorator is above the application'
    print 'Failed stack:\n', s
    ret = 1
    break
  elif re.search("%s " % app2, l.strip()):
    print deco_win, 'found'
    break

# cleanup
os.popen('pkill windowctl')

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
