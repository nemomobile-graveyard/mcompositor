#!/usr/bin/python

# Some tests for Meego stacking layer support.

#* Test steps
#  * show an application window
#  * check correct stacking
#  * show an application window transient to the first one
#  * check correct stacking
#  * show an application window transient to the first transient
#  * check correct stacking
#  * show an application window transient to the second transient
#  * check correct stacking
#  * show an application window
#  * check correct stacking
#  * show a system-modal dialog
#  * check correct stacking
#  * set the first non-transient application window to Meego level 1
#  * check correct stacking
#  * for N in [1, 3, 5, 7, 9] do the following:
#  *   set the second non-transient application window to Meego level N
#  *   check correct stacking
#  *   set the first non-transient application window to Meego level N+1
#* Post-conditions
#  *   check correct stacking

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

# create application window
fd = os.popen('windowctl kn')
app1 = fd.readline().strip()
time.sleep(1)

ret = 0
check_order([app1, home_win], 'app1 appeared correctly')

# create transient application windows
fd = os.popen('windowctl kn %s' % app1)
trans1 = fd.readline().strip()
time.sleep(1)

check_order([trans1, app1, home_win], 'trans1 appeared correctly')

fd = os.popen('windowctl kn %s' % trans1)
trans2 = fd.readline().strip()
time.sleep(1)

check_order([trans2, trans1, app1, home_win], 'trans2 appeared correctly')

fd = os.popen('windowctl kn %s' % trans2)
trans3 = fd.readline().strip()
time.sleep(1)

check_order([trans3, trans2, trans1, app1, home_win],
            'trans3 appeared correctly')

# create application window
fd = os.popen('windowctl kn')
app2 = fd.readline().strip()
time.sleep(1)

check_order([app2, trans3, trans2, trans1, app1, home_win],
            'app2 appeared correctly')

# create system-modal dialog
fd = os.popen('windowctl mdk')
dialog = fd.readline().strip()
time.sleep(1)

check_order([dialog, app2, trans3, trans2, trans1, app1, home_win],
            'dialog appeared correctly')

# set the non-transient application windows to Meego level 1
os.popen('windowctl E %s 1' % app1)
time.sleep(1)

check_order([trans3, trans2, trans1, app1, dialog, app2, home_win],
            'app1 on Meego 1 level correctly')

for level in range(1, 11):
  if level % 2 != 0:
    # levels 1, 3, 5, 7, 9
    os.popen('windowctl E %s %s' % (app2, level))
    time.sleep(1)
    check_order([app2, trans3, trans2, trans1, app1, dialog, home_win],
                'app2 on Meego %s level correctly' % level)
  else:
    # levels 2, 4, 6, 8, 10
    os.popen('windowctl E %s %s' % (app1, level))
    time.sleep(1)
    check_order([trans3, trans2, trans1, app1, app2, dialog, home_win],
                'app1 on Meego %s level correctly' % level)

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
