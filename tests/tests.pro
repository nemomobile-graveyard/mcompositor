TEMPLATE = subdirs
SUBDIRS = windowctl windowstack focus-tracker functional unit
# appinterface depends on libdecorator and is built by the toplevel Makefile

include(shell.pri)

testsXml.files = tests.xml
testsXml.path = /usr/share/mcompositor-tests
INSTALLS += testsXml
