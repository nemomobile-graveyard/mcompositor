#ifndef MCURRENTWINDOWORIENTATIONPROVIDER_P_H
#define MCURRENTWINDOWORIENTATIONPROVIDER_P_H

#include <contextprovider/ContextProvider>

class MCurrentWindowOrientationProvider;

class MCurrentWindowOrientationProviderPrivate
{
public:
    MCurrentWindowOrientationProviderPrivate();

    ContextProvider::Service service;
    ContextProvider::Property property;

    MCurrentWindowOrientationProvider* q_ptr;
    unsigned int defaultAngle;
};

#endif // MCURRENTWINDOWORIENTATIONPROVIDER_P_H
