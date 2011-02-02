# Definitions
SUITENAME=mcompositor-functional-tests

# Stupid qmake wants to link everything.
QMAKE_LINK = @: IGNORE THIS LINE

# The init script will be part of mcompositor-utils.
initScript.files = mcompositor-test-init.py
initScript.path = /usr/bin
PRE_TARGETDEPS += $${initScript.files}
INSTALLS += initScript

# Install all test scripts.
scripts.files += test*.py
scripts.path = /usr/share/meegotouch/testscripts/$$SUITENAME
INSTALLS += scripts

# Simple and easy way to build tests.xml.
metadata.target = tests.xml
metadata.input = setandsuite.testdata \$(patsubst %.py,%.testdata,\$(wildcard $${scripts.files}))
metadata.output = $${metadata.target}
metadata.depends = createTestXml $${metadata.input}
metadata.commands = ./createTestXml $${metadata.output} $${scripts.path} $${metadata.input}
QMAKE_EXTRA_COMPILERS += metadata
QMAKE_EXTRA_TARGETS += metadata
PRE_TARGETDEPS += $${metadata.output}

# Make make clean work.
clean.depends += compiler_metadata_clean
QMAKE_EXTRA_TARGETS += clean

# If we added metadata to INSTALLS it would regenerate tests.xml every
# time you install.
mdinst.depends = $${metadata.output}
mdinst.files = $${metadata.output}
mdinst.path = /usr/share/$$SUITENAME
mdinst.CONFIG = no_check_exist
INSTALLS += mdinst
