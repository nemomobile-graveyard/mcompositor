TEMPLATE = subdirs
SUBDIRS  = src mcompositor mdecorator libdecorator translations
SUBDIRS += tests appinterface_test

libdecorator.subdir = decorators/libdecorator
mdecorator.subdir = decorators/mdecorator
mdecorator.depends = libdecorator
src.depends = libdecorator
mcompositor.depends = src
appinterface_test.subdir = tests/appinterface
appinterface_test.depends = libdecorator

QMAKE_CLEAN += configure-stamp build-stamp
QMAKE_DISTCLEAN += configure-stamp build-stamp

check.target = check
check.CONFIG = recursive
QMAKE_EXTRA_TARGETS += check
