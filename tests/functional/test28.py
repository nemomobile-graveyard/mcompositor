#!/usr/bin/python

# Verify some aspects of PropertyNotify handling.

#* Test steps
#  * create a window
#  * set _MEEGOTOUCH_CANNOT_MINIMIZE=1 onto it
#  * check that it cannot be minimized
#  * delete the _MEEGOTOUCH_CANNOT_MINIMIZE property
#  * check that it can be minimized
#  * create a window
#  * set _MEEGOTOUCH_CANNOT_MINIMIZE=1 onto it
#  * check that it cannot be minimized
#  * set _MEEGOTOUCH_CANNOT_MINIMIZE=0 onto it
#* Post-conditions
#  * check that it can be minimized

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

ret = 0
def check_order(list, test, no_match = 'NO_MATCH'):
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
    elif re.search('%s ' % no_match, l.strip()):
      print 'FAIL: "%s" matched in "%s" test' % (no_match, test)
      print 'Failed stack:\n', s
      ret = 1
      return
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

# create an application window
fd = os.popen('windowctl kn')
app1 = fd.readline().strip()
time.sleep(2)

check_order([app1, home_win], 'application appeared normally')

# set _MEEGOTOUCH_CANNOT_MINIMIZE=1
os.popen("xprop -id %s -f _MEEGOTOUCH_CANNOT_MINIMIZE 32c "
         "-set _MEEGOTOUCH_CANNOT_MINIMIZE 1" % app1)
time.sleep(1)

# try to iconify it
os.popen('windowctl O %s' % app1)
time.sleep(2)

# check that it failed
check_order([app1, home_win], 'application could not be minimized')

# remove _MEEGOTOUCH_CANNOT_MINIMIZE
os.popen("xprop -id %s -remove _MEEGOTOUCH_CANNOT_MINIMIZE" % app1)
time.sleep(1)

# iconify it
os.popen('windowctl O %s' % app1)
time.sleep(2)

# check that it succeeded
check_order([home_win, app1], 'application could be minimized')

# create an application window
fd = os.popen('windowctl kn')
app2 = fd.readline().strip()
time.sleep(2)

check_order([app2, home_win, app1], 'app2 appeared normally')

# set _MEEGOTOUCH_CANNOT_MINIMIZE=1
os.popen("xprop -id %s -f _MEEGOTOUCH_CANNOT_MINIMIZE 32c "
         "-set _MEEGOTOUCH_CANNOT_MINIMIZE 1" % app2)
time.sleep(1)

# try to iconify it
os.popen('windowctl O %s' % app2)
time.sleep(2)

# check that it failed
check_order([app2, home_win, app1], 'app2 could not be minimized')

# set _MEEGOTOUCH_CANNOT_MINIMIZE=0
os.popen("xprop -id %s -f _MEEGOTOUCH_CANNOT_MINIMIZE 32c "
         "-set _MEEGOTOUCH_CANNOT_MINIMIZE 0" % app2)
time.sleep(1)

# iconify it
os.popen('windowctl O %s' % app2)
time.sleep(2)

# check that it succeeded
check_order([home_win, app1, app2], 'app2 could be minimized')

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
