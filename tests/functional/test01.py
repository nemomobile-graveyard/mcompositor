#!/usr/bin/python

# Check that we can't raise app or system dialog over a system-modal dialog.

#* Test steps
#  * show an application window
#  * show a modal, non-transient (i.e. system-modal) dialog
#  * check that the dialog is stacked above the application
#  * activate application in background
#  * check that the dialog is stacked above the application
#  * show a system dialog
#  * check that the system-modal is stacked above the app and system dialog
#  * show a non-modal dialog that is transient to the system-modal dialog
#  * check that the transient dialog is stacked above the system-modal dialog
#  * show a window with Meego level 1
#  * check the stacking order
#  * create few unmapped windows
#  * show a system-modal dialog
#* Post-conditions
#  * check that the system-modal is stacked below the Meego level 1 window

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
  print 'FAIL: desktop not found'
  sys.exit(1)

def check_order(list, test):
  global ret
  print 'Test:', test
  fd = os.popen('windowstack')
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

fd = os.popen('windowctl kn')
app = fd.readline().strip()
time.sleep(2)

# show system-modal dialog
fd = os.popen('windowctl mdk')
sys_modal = fd.readline().strip()
time.sleep(1)

ret = 0
check_order([sys_modal, app, home_win], 'system-modal appears on top')

# check that we can't raise app over the dialog
os.popen("windowctl A %s" % app)
time.sleep(1)

check_order([sys_modal, app, home_win], 'cannot raise app over system-modal')

# show system dialog
fd = os.popen('windowctl dk')
dialog = fd.readline().strip()
time.sleep(1)

check_order([sys_modal, dialog, app, home_win], 'system dialog stacked correctly')

# show a non-modal dialog that is transient to the system-modal dialog
fd = os.popen('windowctl dk %s' % sys_modal)
transient = fd.readline().strip()
time.sleep(1)

check_order([transient, sys_modal, dialog, app, home_win],
            'transient dialog stacked correctly')

# show Meego level 1 window
fd = os.popen('windowctl E 1')
meego1 = fd.readline().strip()
time.sleep(1)

check_order([meego1, transient, sys_modal, dialog, app, home_win],
            'Meego 1 level window stacked correctly')

# create few unmapped windows
for i in range(10):
  id = os.popen('windowctl enk').readline().strip()
  os.popen('windowctl U %s' % id)
time.sleep(1)

# show a system-modal
fd = os.popen('windowctl mdk')
sys_modal2 = fd.readline().strip()
time.sleep(1)

check_order([meego1, sys_modal2, transient, sys_modal, dialog, app, home_win],
            'system-modal stacked below the Meego 1 level')

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
