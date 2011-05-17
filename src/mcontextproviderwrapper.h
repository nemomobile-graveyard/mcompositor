#ifndef MCONTEXTPROVIDERWRAPPER_H
#define MCONTEXTPROVIDERWRAPPER_H

#include <QObject>

class MWindowPropertyCache;
class MContextProviderWrapperPrivate;

class MContextProviderWrapper
{
public:
    MContextProviderWrapper(unsigned defaultDesktopAngle = 270);
    ~MContextProviderWrapper();

public:
    void updateCurrentWindowOrienationAngle(MWindowPropertyCache *pc);
    void updateDesktopOrientationAngle(MWindowPropertyCache *pc);

private:
    Q_DISABLE_COPY(MContextProviderWrapper);
    Q_DECLARE_PRIVATE(MContextProviderWrapper)
    QScopedPointer<MContextProviderWrapperPrivate> d_ptr;
};

#endif // MCONTEXTPROVIDERWRAPPER_H
