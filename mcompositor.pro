TEMPLATE = subdirs
SUBDIRS  = src mcompositor mdecorator libdecorator translations
SUBDIRS += tests appinterface_test unittests

NODOCS:{
  message("Not building the documentation for this package")
} else {
  SUBDIRS += doc
}

libdecorator.subdir = decorators/libdecorator
mdecorator.subdir = decorators/mdecorator
mdecorator.depends = libdecorator
src.depends = libdecorator
mcompositor.depends = src
appinterface_test.subdir = tests/appinterface
appinterface_test.depends = libdecorator
unittests.subdir = tests/unit
unittests.depends = libdecorator src

QMAKE_CLEAN += configure-stamp build-stamp
QMAKE_DISTCLEAN += configure-stamp build-stamp

check.target = check
check.CONFIG = recursive
QMAKE_EXTRA_TARGETS += check
