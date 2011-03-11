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
    Q_DECLARE_PRIVATE(MCurrentWindowOrientationProvider)
    MCurrentWindowOrientationProviderPrivate* d_ptr;
};

#endif // MCURRENTWINDOWORIENTATIONPROVIDER_H
