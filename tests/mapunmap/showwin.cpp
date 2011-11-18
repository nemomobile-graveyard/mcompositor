#include <QtGui>
#include <sys/time.h> 
 #include <sys/resource.h>


#include <iostream>

class Shower: public QObject
{
    Q_OBJECT
public:
    Shower(QWidget* showthis)
        :QObject(),
         show(false),
         w(showthis)
    {}

public slots:
    void showwindow()
    {
        if (show) {
            w->show();
            w->activateWindow();
            show = false;
        } else {
            w->hide();
            show = true;
        }
        static int cycle = 0;
        qDebug() << cycle;
        if (cycle++ == 100)
            exit(0);
    }

private:
    bool show;
    QWidget* w;

};

int main(int argc, char **argv)
{
    QApplication::setGraphicsSystem("raster");
    QApplication a(argc, argv);
    
    QWidget w(0, Qt::FramelessWindowHint);
    Shower s(&w);
    
    QTimer t;
    QObject::connect(&t, SIGNAL(timeout()), &s, SLOT(showwindow()));
    
    w.resize(a.desktop()->screenGeometry().size());
    
    w.show();
    t.start(1000);
    
    a.exec();
}

#include "showwin.moc"
