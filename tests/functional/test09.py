#!/usr/bin/python

# Check that configure requests for changing stacking order are supported.

#* Test steps
#  * show an application window
#  * show another application window
#  * configure topmost application below the other
#  * configure bottommost application above the other
#  * configure bottommost application to the top (sibling None)
#* Post-conditions
#  * stacking order is changed according to the configure requests

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
  elif re.search(' viewable mdecorator', l.strip()):
    deco_win = win_re.match(l.strip()).group()

if home_win == 0:
  print 'FAIL: desktop not found'
  sys.exit(1)

if deco_win == 0:
  print 'FAIL: decorator window not found'
  sys.exit(1)

ret = 0
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

# create two application windows
fd = os.popen('windowctl n')
app1 = fd.readline().strip()
time.sleep(1)

check_order([deco_win, app1, home_win], 'mapped app1 in right order')

fd = os.popen('windowctl n')
app2 = fd.readline().strip()
time.sleep(2)

check_order([deco_win, app2, app1, home_win], 'mapped both apps in right order')

# stack top application below the other
os.popen('windowctl L %s %s' % (app2, app1))
time.sleep(1)

check_order([deco_win, app1, app2, home_win], 'app2 stacked below app1')

# stack bottom application above the other
os.popen('windowctl V %s %s' % (app2, app1))
time.sleep(1)

check_order([deco_win, app2, app1, home_win], 'app2 configured above app1')

# stack bottom application to top
os.popen('windowctl V %s None' % app1)
time.sleep(1)

check_order([deco_win, app1, app2, home_win], 'app1 stacked on top')

# stack app1 to bottom
os.popen('windowctl L %s None' % app1)
time.sleep(1)

check_order([deco_win, app2, home_win, app1], 'app1 stacked to bottom')

# stack app2 to bottom
os.popen('windowctl L %s None' % app2)
time.sleep(1)

check_order([home_win, app1, app2, deco_win], 'app2 stacked to bottom')

# create two override-redirect windows
fd = os.popen('windowctl o')
or1 = fd.readline().strip()
time.sleep(1)

check_order([or1, home_win, app1, app2, deco_win], 'or1 mapped correctly')

fd = os.popen('windowctl o')
or2 = fd.readline().strip()
time.sleep(1)

check_order([or2, or1, home_win, app1, app2, deco_win], 'or2 mapped correctly')

# stack lower OR window to top
os.popen('windowctl V %s None' % or1)
time.sleep(1)

check_order([or1, or2, home_win, app1, app2, deco_win], 'or1 configured to top')

# stack topmost OR window to bottom
os.popen('windowctl L %s None' % or1)
time.sleep(1)

check_order([or2, home_win, app1, app2, deco_win, or1], 'or1 configured to bottom')

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
