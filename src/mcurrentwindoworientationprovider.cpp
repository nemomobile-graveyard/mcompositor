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

MCurrentWindowOrientationProvider::MCurrentWindowOrientationProvider():
    d_ptr(new MCurrentWindowOrientationProviderPrivate)
{
    Q_D(MCurrentWindowOrientationProvider);
    d->q_ptr = this;
}

void MCurrentWindowOrientationProvider::update(MWindowPropertyCache *pc)
{
    Q_D(MCurrentWindowOrientationProvider);
    d->property.setValue(pc->orientationAngle());
}
