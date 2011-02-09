#!/usr/bin/python

# Check configure requests for unmapped windows are supported.

#* Test steps
#  * create an unmapped application window 1
#  * check that window 1 is at the top
#  * configure window 1 to bottom
#  * check that window 1 is at the bottom
#  * map window 1
#  * check that window 1 is at the bottom
#  * map an application window 2
#  * check that window 2 appears on top
#  * unmap the application window 2
#  * check that window 2 is still stacked on top of the home window
#  * configure window 2 to the bottom
#  * check that window 2 is stacked in the bottom
#  * map window 2
#  * check that mapped window 2 is stacked in the bottom
#  * unmap window 2
#  * check that unmapped window 2 is stacked in the bottom
#  * map another application window 3
#  * check that the order of unmapped windows is unchanged
#  * configure the window 2 below the mapped window 3
#  * map the configured window 2
#* Post-conditions
#  * check that window 2 is stacked below window 3

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

# create unmapped application window
fd = os.popen('windowctl eunk')
app1 = fd.readline().strip()
time.sleep(1)

ret = 0
check_order([app1, home_win], 'unmapped app 1 at the top')

# stack it to bottom -- this configuration is remembered when mapped
os.popen('windowctl L %s None' % app1)
time.sleep(1)

check_order([home_win, app1], 'unmapped app 1 in bottom')

# map it
os.popen('windowctl M %s' % app1)
time.sleep(1)

check_order([home_win, app1], 'mapped app 1 configured in bottom')

# map new application window
fd = os.popen('windowctl enk')
app2 = fd.readline().strip()
time.sleep(1)

check_order([app2, home_win, app1], 'mapped app 2 appears on top')

# unmap it
os.popen('windowctl U %s' % app2)
time.sleep(1)

check_order([app2, home_win, app1], 'unmapped app 2 remains on top')

# stack it to bottom -- this configuration is remembered when mapped again
os.popen('windowctl L %s None' % app2)
time.sleep(1)

check_order([home_win, app1, app2], 'unmapped app 2 configured to bottom')

# map it
os.popen('windowctl M %s' % app2)
time.sleep(1)

check_order([home_win, app1, app2], 'mapped app 2 configured to bottom')

# unmap window 2
os.popen('windowctl U %s' % app2)
time.sleep(1)

check_order([home_win, app1, app2], 'app 2 still at bottom after unmapping')

# map application window 3
fd = os.popen('windowctl nk')
app3 = fd.readline().strip()
time.sleep(1)

check_order([app3, home_win, app1, app2], 'order of unmapped windows is unchanged')

# configure unmapped window 2 below the mapped window 3
os.popen('windowctl L %s %s' % (app2, app3))
time.sleep(1)

check_order([app3, app2, home_win, app1], 'unmapped app 2 configured below app 3')

# map the configured window 2
os.popen('windowctl M %s' % app2)
time.sleep(1)

check_order([app3, app2, home_win, app1], 'mapped app 2 configured below app 3')

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
