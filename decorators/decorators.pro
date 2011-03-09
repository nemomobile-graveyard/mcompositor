TEMPLATE = subdirs
CONFIG += ordered
SUBDIRS += libdecorator mdecorator

check.target = check 
check.CONFIG = recursive
QMAKE_EXTRA_TARGETS += check
