#!/usr/bin/python

# Check that a transient dialog is raised with the application it is
# transient for.

#* Test steps
#  * show an application window
#  * create and show a dialog window that is transient for the application
#  * check stacking order
#  * iconify the application window
#  * check stacking order
#  * activate the application window
#  * check that the transient is above the application window
#  * iconify the application window
#  * check stacking order
#  * activate the transient window
#  * check that the transient is above the application window
#  * create and show a dialog window that is transient for the previous dialog
#  * check stacking order
#  * iconify the application window
#  * check stacking order
#  * activate the application window
#  * check that both transients are above the application window and in order
#  * swap the transiencies of the dialogs
#* Post-conditions
#  * check that both transients are above the application window and in order

import os, re, sys, time

if os.system('mcompositor-test-init.py'):
  sys.exit(1)

fd = os.popen('windowstack m')
s = fd.read(5000)
win_re = re.compile('^0x[0-9a-f]+')
home_win = 0
for l in s.splitlines():
  if re.search(' DESKTOP viewable ', l.strip()):
    home_win = win_re.match(l.strip()).group()

if home_win == 0:
  print 'FAIL: desktop window not found'
  sys.exit(1)

def check_order(list, test):
  global ret
  print 'Test:', test
  fd = os.popen('windowstack m')
  s = fd.read(10000)
  i = 0
  for l in s.splitlines():
    if re.search('%s ' % list[i], l.strip()):
      print list[i], 'found'
      i += 1
      if i >= len(list):
        break
      continue
    else:
      # no match, check that no other element matches either
      for j in range(i, len(list)):
        if re.search('%s ' % list[j], l.strip()):
          print 'FAIL: stacking order is wrong in "%s" test' % test
          print 'Failed stack:\n', s
          ret = 1
          return
  if i < len(list):
    print 'FAIL: windows missing from the stack in "%s" test' % test
    print 'Failed stack:\n', s
    ret = 1

# create application and transient dialog windows
fd = os.popen('windowctl kn')
app = fd.readline().strip()
time.sleep(1)
fd = os.popen("windowctl kd %s" % app)
dialog1 = fd.readline().strip()
time.sleep(1)

ret = 0
check_order([dialog1, app, home_win], 'app and dialog appeared correctly')

# iconify the application
os.popen("windowctl O %s" % app)
time.sleep(2)

check_order([home_win, dialog1, app], 'app and dialog iconified correctly 1')

# activate the application (this should raise the dialog too)
os.popen("windowctl A %s" % app)
time.sleep(1)

check_order([dialog1, app, home_win], 'app and dialog raised correctly 1')

# iconify the application
os.popen("windowctl O %s" % app)
time.sleep(2)

check_order([home_win, dialog1, app], 'app and dialog iconified correctly 2')

# activate the transient
os.popen("windowctl A %s" % dialog1)
time.sleep(1)

check_order([dialog1, app, home_win], 'app and dialog raised correctly 2')

# create a dialog that is transient to the first dialog
fd = os.popen("windowctl kd %s" % dialog1)
dialog2 = fd.readline().strip()
time.sleep(1)

check_order([dialog2, dialog1, app, home_win], 'dialog2 appeared correctly')

# iconify the application
os.popen("windowctl O %s" % app)
time.sleep(2)

check_order([home_win, dialog2, dialog1, app], 'app and both dialogs iconified')

# activate the application (this should raise the dialogs too)
os.popen("windowctl A %s" % app)
time.sleep(1)

check_order([dialog2, dialog1, app, home_win], 'app and both dialogs raised')

# get the root window and the current stacking
r = re.compile("Window id: (0x[0-9a-fA-F]*)")
root = [ m.group(1) for m in [ r.search(l) for l in os.popen("xwininfo -root") ]
	if m is not None ][0]
rnwmclist = re.compile('^_NET_CLIENT_LIST_STACKING:')
stacking = filter(rnwmclist.match, os.popen("xprop -id %s -notype" % root))[0]

# swap the transiencies of the dialogs
# (this introduces a temporary transiency loop)
os.popen("windowctl t %s %s" % (dialog1, dialog2))
os.popen("windowctl t %s %s" % (dialog2, app))

# wait until the wm has restacked
prev_stacking = stacking
while stacking == prev_stacking:
	time.sleep(0.5)
	prev_stacking = stacking
	stacking = filter(rnwmclist.match,
		os.popen("xprop -id %s -notype" % root))[0]

check_order([dialog1, dialog2, app, home_win], 'dialogs swapped correctly')

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
