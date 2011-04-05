TEMPLATE = subdirs
SUBDIRS = windowctl windowstack focus-tracker functional manual-splash
# appinterface depends on libdecorator and is built by the toplevel Makefile
# unit tests depend on libmcompositor and are built likewise

include(shell.pri)

testsXml.files = tests.xml
testsXml.path = /usr/share/mcompositor-tests
INSTALLS += testsXml
