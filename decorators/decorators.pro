TEMPLATE = subdirs
include(../shared.pri)
CONFIG+=ordered


addSubDirs(libdecorator)
addSubDirs(mdecorator, libdecorator)


check.target = check 
check.CONFIG = recursive
QMAKE_EXTRA_TARGETS += check
