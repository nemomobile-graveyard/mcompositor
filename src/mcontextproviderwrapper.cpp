#include "mcontextproviderwrapper_p.h"
#include "mcontextproviderwrapper.h"

#include "mwindowpropertycache.h"

#include <contextprovider/ContextProvider>
#include <QDBusConnection>

MContextProviderWrapperPrivate::MContextProviderWrapperPrivate() :
    service(QDBusConnection::SessionBus, "org.maemo.mcompositor.context"),
    currentWindowAngleProperty(service, "/Screen/CurrentWindow/OrientationAngle"),
    desktopAngleProperty(service, "/Screen/Desktop/OrientationAngle")
{
    currentWindowAngleProperty.setValue(QVariant(QVariant::Int));
    desktopAngleProperty.setValue(defaultDesktopAngle);
}

MContextProviderWrapper::~MContextProviderWrapper()
{
    // keep QScopedPointer happy
}

MContextProviderWrapper::MContextProviderWrapper(unsigned defaultDesktopAngle):
    d_ptr(new MContextProviderWrapperPrivate)
{
    Q_D(MContextProviderWrapper);
    d->q_ptr = this;
    d->defaultDesktopAngle = defaultDesktopAngle;
}

void MContextProviderWrapper::updateCurrentWindowOrienationAngle(MWindowPropertyCache *pc)
{
    Q_D(MContextProviderWrapper);
    if(pc != 0)
        d->currentWindowAngleProperty.setValue(pc->orientationAngle());
    else
        d->currentWindowAngleProperty.setValue(QVariant(QVariant::Int));
}

void MContextProviderWrapper::updateDesktopOrientationAngle(MWindowPropertyCache *pc)
{
    Q_D(MContextProviderWrapper);
    if(pc != 0)
        d->desktopAngleProperty.setValue(pc->orientationAngle());
    else
        d->desktopAngleProperty.setValue(d->defaultDesktopAngle);
}
