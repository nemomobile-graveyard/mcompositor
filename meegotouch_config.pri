M_INSTALL_BIN=/usr/bin
M_INSTALL_HEADERS=/usr/include/meegotouch

# Check for testability features, should they be compiled in or not?
!isEqual(TESTABILITY,"off") {
    DEFINES += WINDOW_DEBUG
}

# Check for remote control interface
isEqual(REMOTE_CONTROL,"on") {
    DEFINES += REMOTE_CONTROL
}

# Compositor components only
VERSION = 1.3.0
