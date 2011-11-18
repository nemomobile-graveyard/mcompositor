#!/bin/bash

DOMAIN="Application Framework"
FEATURE="MCompositor"
TYPE="Unit"
LEVEL="Component"

UT_TESTCASES=""
# FT_TESTCASES=""

for T in `cd unit; ls -d ?t_*`; do
  TEMPLATE="<case name=\"$T\" description=\"$T\" requirement=\"\" timeout=\"300\">
        <step expected_result=\"0\">/usr/lib/mcompositor-unit-tests/$T</step>
      </case>
      "

  if [ -n "`echo $T | egrep '^u'`" ]; then
    UT_TESTCASES="${UT_TESTCASES}${TEMPLATE}"
  fi
done

TESTSUITE_TEMPLATE="<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>
<testdefinition version=\"0.1\">
  <suite name=\"mcompositor-tests\" domain=\"$DOMAIN\" type=\"$TYPE\" level=\"$LEVEL\">
    <set name=\"unit_tests\" description=\"Unit Tests\" feature=\"$FEATURE\">

      $UT_TESTCASES

      <environments>
        <scratchbox>false</scratchbox>
        <hardware>true</hardware>    
      </environments> 
    </set>
  </suite>
</testdefinition>"

echo "$TESTSUITE_TEMPLATE"

## 
##    <set name=\"functional_tests\" description=\"Functional Tests\" feature=\
##
##      $FT_TESTCASES
##
##      <environments>
##        <scratchbox>false</scratchbox>
##        <hardware>true</hardware>    
##      </environments> 
##
##    </set>
## 
