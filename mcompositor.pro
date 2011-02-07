include(shared.pri)

TEMPLATE = subdirs
CONFIG+=ordered

addSubDirs(decorators)
addSubDirs(src, decorators)
addSubDirs(mcompositor, decorators src)
addSubDirs(tests)
addSubDirs(translations)


QMAKE_CLEAN += \ 
	configure-stamp \
	build-stamp \

QMAKE_DISTCLEAN += \
    configure-stamp \
    build-stamp \

check.target = check
check.CONFIG = recursive
QMAKE_EXTRA_TARGETS += check
