#ifndef MCURRENTWINDOWORIENTATIONPROVIDER_P_H
#define MCURRENTWINDOWORIENTATIONPROVIDER_P_H

#include <contextprovider/ContextProvider>

class MCurrentWindowOrientationProvider;
class MCompositeManagerPrivate;

class MCurrentWindowOrientationProviderPrivate
{
public:
    MCurrentWindowOrientationProviderPrivate();

    ContextProvider::Service service;
    ContextProvider::Property property;
    unsigned currentWindowOrientationAngle;

    MCompositeManagerPrivate* mc_priv;

    MCurrentWindowOrientationProvider* q_ptr;
};

#endif // MCURRENTWINDOWORIENTATIONPROVIDER_P_H
