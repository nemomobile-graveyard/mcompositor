#include "mcurrentwindoworientationprovider_p.h"
#include "mcurrentwindoworientationprovider.h"

#include "mwindowpropertycache.h"

#include <contextprovider/ContextProvider>
#include <QDBusConnection>

MCurrentWindowOrientationProviderPrivate::MCurrentWindowOrientationProviderPrivate() :
        service(QDBusConnection::SessionBus, "org.maemo.mcompositor.context"),
        property(service, "/Screen/CurrentWindow/OrientationAngle"),
        currentWindowOrientationAngle(0)
{
    property.setValue(currentWindowOrientationAngle);
}

MCurrentWindowOrientationProvider::MCurrentWindowOrientationProvider():
    d_ptr(new MCurrentWindowOrientationProviderPrivate)
{
    Q_D(MCurrentWindowOrientationProvider);
    d->q_ptr = this;
}

MCurrentWindowOrientationProvider::~MCurrentWindowOrientationProvider()
{
    delete d_ptr;
}

void MCurrentWindowOrientationProvider::update(MWindowPropertyCache *pc)
{
    Q_D(MCurrentWindowOrientationProvider);

    if (d->currentWindowOrientationAngle != pc->orientationAngle()) {
        d->currentWindowOrientationAngle = pc->orientationAngle();
        d->property.setValue(d->currentWindowOrientationAngle);
    }
}
