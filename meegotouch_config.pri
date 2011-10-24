# Load more defines from the dui_defines.
load(meegotouch_defines)

# Add global libdui includes
INCLUDEPATH += $$M_INSTALL_HEADERS

# Check for testability features, should they be compiled in or not?
!isEqual(TESTABILITY,"off") {
    DEFINES += WINDOW_DEBUG
}

# Check for remote control interface
isEqual(REMOTE_CONTROL,"on") {
    DEFINES += REMOTE_CONTROL
}

# Compositor components only
VERSION = 1.1.3
