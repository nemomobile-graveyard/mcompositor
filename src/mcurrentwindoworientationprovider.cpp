#include "mcurrentwindoworientationprovider_p.h"
#include "mcurrentwindoworientationprovider.h"

#include "mwindowpropertycache.h"

#include <contextprovider/ContextProvider>
#include <QDBusConnection>

MCurrentWindowOrientationProviderPrivate::MCurrentWindowOrientationProviderPrivate() :
    service(QDBusConnection::SessionBus, "org.maemo.mcompositor.context"),
    property(service, "/Screen/CurrentWindow/OrientationAngle")
{
}

MCurrentWindowOrientationProvider::~MCurrentWindowOrientationProvider()
{
    // keep QScopedPointer happy
}

MCurrentWindowOrientationProvider::MCurrentWindowOrientationProvider(unsigned int defaultAngle):
    d_ptr(new MCurrentWindowOrientationProviderPrivate)
{
    Q_D(MCurrentWindowOrientationProvider);
    d->q_ptr = this;
    d->defaultAngle =defaultAngle;
    d->property.setValue(defaultAngle);
}

void MCurrentWindowOrientationProvider::update(MWindowPropertyCache *pc)
{
    Q_D(MCurrentWindowOrientationProvider);
    if(pc != 0)
        d->property.setValue(pc->orientationAngle());
    else
        d->property.setValue(d->defaultAngle);
}
