#!/usr/bin/python

# Check that window stack stays static while rotating the screen.

#* Test steps
#  * create and show TYPE_INPUT window
#  * create and show application window
#  * create and show dialog window
#  * rotate screen step-by-step and check that window stack says the same
#* Post-conditions
#  * None

import os, re, sys, time

if os.system('mcompositor-test-init.py'):
  sys.exit(1)

def rotate_screen(top_edge):
  print 'rotate_screen:', top_edge
  os.spawnlp(os.P_NOWAIT, "/usr/bin/windowctl", "windowctl", "R", top_edge)
  time.sleep(1)

def print_stack_array(a):
  i = 0
  while i < len(a):
    if a[i] == 'root':
      print a[i:i+5]
      break
    print a[i:i+4]
    i += 4

# create input, app, and dialog windows
fd = os.popen('windowctl i')
input_win = fd.readline().strip()
time.sleep(1)
fd = os.popen('windowctl n')
old_win = fd.readline().strip()
time.sleep(1)
fd = os.popen('windowctl d')
new_win = fd.readline().strip()
time.sleep(2)

orig_stack = []
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if l.split()[0] == 'root':
    print 'root found'
    orig_stack += l.strip().split()
    break
  if l.split()[1] != 'no-TYPE':
    orig_stack += l.strip().split()[0:4]

# rotate 90 degrees and check the stack
ret = 0
for arg in ['l', 'b', 'r', 't']:
  rotate_screen(arg)
  new_stack = []
  fd = os.popen('windowstack m')
  s = fd.read(5000)
  for l in s.splitlines():
    if l.split()[0] == 'root':
      new_stack += l.strip().split()
      break
    if l.split()[1] != 'no-TYPE':
      new_stack += l.strip().split()[0:4]
  if orig_stack != new_stack:
    print 'Failed stack:'
    print_stack_array(new_stack)
    print 'Original stack:'
    print_stack_array(orig_stack)
    ret = 1
    break
  edge2angle = {'l' : '270', 'b' : '180', 'r' : '90', 't' : '0'}
  # set _MEEGOTOUCH_ORIENTATION_ANGLE on curren app window
  f_cw = os.popen('xprop -root _MEEGOTOUCH_CURRENT_APP_WINDOW')
  o_cw = f_cw.read()
  cw = o_cw.split('#')
  print cw[1].strip() 
  os.popen("xprop -id %s -f _MEEGOTOUCH_ORIENTATION_ANGLE 32c "
           "-set _MEEGOTOUCH_ORIENTATION_ANGLE %s"
           % (cw[1].strip(), edge2angle[arg]))  
  # check if /Screen/CurrentWindow/OrientationAngle was set
  time.sleep(1)
  f_ca = os.popen("qdbus org.maemo.mcompositor.context "
                  "/Screen/CurrentWindow/OrientationAngle "
                  "org.maemo.contextkit.Property.Get")
  o_ca = f_ca.read()
  if o_ca.splitlines()[0] == edge2angle[arg]:
    print 'Value as expected: ' + edge2angle[arg]
    time.sleep(9) # sleep the rotation (windowctl R) away
    continue
  ret = 1
  print '/Screen/CurrentWindow/OrientationAngle does not match expected value'
  print 'Current value: ' + clout.splitlines()[0]
  print 'Expected value: ' + edge2angle[arg]
  break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
