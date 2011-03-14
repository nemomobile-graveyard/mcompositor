#ifndef MCURRENTWINDOWORIENTATIONPROVIDER_H
#define MCURRENTWINDOWORIENTATIONPROVIDER_H

#include <QObject>

class MWindowPropertyCache;
class MCurrentWindowOrientationProviderPrivate;

class MCurrentWindowOrientationProvider
{
public:
    MCurrentWindowOrientationProvider();
    ~MCurrentWindowOrientationProvider();

public:
    void update(MWindowPropertyCache *pc);

private:
    Q_DISABLE_COPY(MCurrentWindowOrientationProvider);
    Q_DECLARE_PRIVATE(MCurrentWindowOrientationProvider)
    QScopedPointer<MCurrentWindowOrientationProviderPrivate> d_ptr;
};

#endif // MCURRENTWINDOWORIENTATIONPROVIDER_H
