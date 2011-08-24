TEMPLATE = subdirs
SUBDIRS += ut_stacking ut_anim ut_lockscreen ut_closeapp ut_compositing

tests_xml.files = tests.xml
tests_xml.path = /usr/share/mcompositor-unit-tests
INSTALLS += tests_xml
