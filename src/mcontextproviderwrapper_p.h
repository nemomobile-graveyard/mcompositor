#ifndef MCONTEXTPROVIDERWRAPPER_P_H
#define MCONTEXTPROVIDERWRAPPER_P_H

#include <contextprovider/ContextProvider>

class MContextProviderWrapper;

class MContextProviderWrapperPrivate
{
public:
    MContextProviderWrapperPrivate();

    ContextProvider::Service service;
    ContextProvider::Property currentWindowAngleProperty;
    ContextProvider::Property desktopAngleProperty;

    unsigned defaultDesktopAngle;

    MContextProviderWrapper* q_ptr;
};

#endif // MCONTEXTPROVIDERWRAPPER_P_H
