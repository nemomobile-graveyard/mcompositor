#include "mcurrentwindoworientationprovider_p.h"
#include "mcurrentwindoworientationprovider.h"

#include "mcompositemanager_p.h"
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

MCurrentWindowOrientationProvider::MCurrentWindowOrientationProvider(QObject *parent) :
    QObject(parent),
    d_ptr(new MCurrentWindowOrientationProviderPrivate)
{
    Q_D(MCurrentWindowOrientationProvider);
    d->q_ptr = this;

    d->mc_priv = qobject_cast<MCompositeManagerPrivate*>(parent);
    connect(d->mc_priv, SIGNAL(currentAppChanged(Window)),
            this, SLOT(update()));
}

MCurrentWindowOrientationProvider::~MCurrentWindowOrientationProvider()
{
    delete d_ptr;
}

void MCurrentWindowOrientationProvider::update()
{
    Q_D(MCurrentWindowOrientationProvider);
    unsigned int currentWindow = d->mc_priv->current_app;
    if(!currentWindow)
        return;

    MWindowPropertyCache* pc = d->mc_priv->getPropertyCache(currentWindow);
    if (d->currentWindowOrientationAngle != pc->orientationAngle()) {
        d->currentWindowOrientationAngle = pc->orientationAngle();
        d->property.setValue(d->currentWindowOrientationAngle);
    }
}
