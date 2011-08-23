#!/usr/bin/python

# Some tests for the splash screen.

#* Test steps
#  * show an unmapped application window
#  * show a splash screen for it
#  * check that the splash screen appeared
#  * map the application window
#  * check that the splash screen disappeared
#  * show a new splash screen with a bogus PID
#  * check that the splash screen appeared
#  * map a new application window
#  * check that the app window is stacked above the splash screen
#  * wait for the splash timeout
#* Post-conditions
#  * check that the splash screen disappeared

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

# create unmapped application window
fd = os.popen('windowctl eukn')
app1 = fd.readline().strip()
time.sleep(1)

# show a splash screen for it
pid = os.popen('pidof windowctl').readline().strip()
splash = '/usr/share/mcompositor-functional-tests/splash.jpg'
os.popen('manual-splash %s ignored %s %s ""' % (pid, splash, splash))
time.sleep(2)

ret = 0
check_order(['\'MSplashScreen\'', home_win], 'splash appeared correctly')

# map the application window
os.popen('windowctl M %s' % app1)
time.sleep(1)

# check that splash screen disappeared
check_order([app1, home_win], 'splash disappeared', '\'MSplashScreen\'')

# show a new splash screen with a bogus PID
os.popen('manual-splash %s ignored %s %s ""' % (0, splash, splash))
time.sleep(2)

check_order(['\'MSplashScreen\'', app1, home_win], 'second splash appeared')

# show a new application window
app2 = os.popen('windowctl kn').readline().strip()
time.sleep(1)

check_order([app2, '\'MSplashScreen\'', app1, home_win], 'app2 appeared correctly')

# wait for the splash timeout (2s already waited)
time.sleep(28)

check_order([app2, app1, home_win], 'splash disappeared on timeout',
            '\'MSplashScreen\'')

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
