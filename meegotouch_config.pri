# Load more defines from the dui_defines.
load(meegotouch_defines)

# Add global libdui includes
INCLUDEPATH += $$M_INSTALL_HEADERS

# Check for testability features, should they be compiled in or not?
!isEqual(TESTABILITY,"off") {
    DEFINES += WINDOW_DEBUG
}

# Compositor components only
VERSION = 0.7.9
