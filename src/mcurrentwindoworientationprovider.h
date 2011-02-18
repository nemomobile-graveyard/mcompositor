#ifndef MCURRENTWINDOWORIENTATIONPROVIDER_H
#define MCURRENTWINDOWORIENTATIONPROVIDER_H

#include <QObject>

//typedef int Window;
class MCurrentWindowOrientationProviderPrivate;

class MCurrentWindowOrientationProvider : public QObject
{
    Q_OBJECT
public:
    explicit MCurrentWindowOrientationProvider(QObject *parent = 0);
    ~MCurrentWindowOrientationProvider();
signals:

public slots:
    void update();

private:
    Q_DECLARE_PRIVATE(MCurrentWindowOrientationProvider)
    MCurrentWindowOrientationProviderPrivate* d_ptr;
};

#endif // MCURRENTWINDOWORIENTATIONPROVIDER_H
