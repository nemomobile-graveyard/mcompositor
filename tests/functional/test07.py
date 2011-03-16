#!/usr/bin/python

# Check that TYPE_NOTIFICATION window is stacked above applications,
# dialogs and input windows.

#* Test steps
#  * create and show window on Meego level 6
#  * create and show notification window
#  * create and show application window
#  * create and show dialog window
#  * create and show TYPE_INPUT window
#  * create and show window on Meego level 5
#* Post-conditions
#  * Notification window is stacked above application, dialog and input
#    window, but below the Meego level 6 window

import os, re, sys, time

if os.system('mcompositor-test-init.py'):
  sys.exit(1)

# create notification, app, dialog, and input windows
fd = os.popen('windowctl E 6')
level6 = fd.readline().strip()
time.sleep(1)
fd = os.popen('windowctl b')
note_win = fd.readline().strip()
time.sleep(1)
fd = os.popen('windowctl n')
old_win = fd.readline().strip()
time.sleep(1)
fd = os.popen('windowctl d')
new_win = fd.readline().strip()
time.sleep(1)
fd = os.popen('windowctl i')
input_win = fd.readline().strip()
time.sleep(1)
fd = os.popen('windowctl E 5')
level5 = fd.readline().strip()
time.sleep(2)

ret = level6_found = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % level6, l.strip()):
    print level6, 'found'
    level6_found = 1
  elif re.search("%s " % note_win, l.strip()) and level6_found:
    print note_win, 'found'
    break
  elif re.search("%s " % old_win, l.strip()):
    print 'FAIL: app is stacked above notification'
    print 'Failed stack:\n', s
    ret = 1
    break
  elif re.search("%s " % new_win, l.strip()):
    print 'FAIL: dialog is stacked above notification'
    print 'Failed stack:\n', s
    ret = 1
    break
  elif re.search("%s " % input_win, l.strip()):
    print 'FAIL: input is stacked above notification'
    print 'Failed stack:\n', s
    ret = 1
    break
  elif re.search("%s " % level5, l.strip()):
    print 'FAIL: notification is stacked below Meego level 5'
    print 'Failed stack:\n', s
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
