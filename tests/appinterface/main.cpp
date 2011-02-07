#include <QtGui>
#include <QDBusConnection>
#include <mdecorator_dbus_interface.h>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = NULL)
        : QMainWindow(parent)
        , menuActionCounter(0)
        , toolBarActionCounter(0)
        , addMenuAction(new QPushButton("add Menu Action"))
        , addToolBarAction(new QPushButton("add ToolBar Action"))
        , removeMenuAction(new QPushButton("remove Menu Action"))
        , removeToolBarAction(new QPushButton("remove ToolBar Action"))
        , interface(new MDecoratorInterface("com.nokia.MDecorator", "/MDecorator", QDBusConnection::sessionBus(),this))
        , isStyled(false)
    {
        setCentralWidget(new QWidget());

        QGridLayout* grid = new QGridLayout(centralWidget());
        grid->addWidget(addMenuAction,0,0);
        grid->addWidget(removeMenuAction,0,1);
        grid->addWidget(addToolBarAction,1,0);
        grid->addWidget(removeToolBarAction,1,1);

        connect(addMenuAction,SIGNAL(clicked()),SLOT(onAddMenuAction()));
        connect(addToolBarAction,SIGNAL(clicked()),SLOT(onAddToolBarAction()));
        connect(removeMenuAction,SIGNAL(clicked()),SLOT(onRemoveMenuAction()));
        connect(removeToolBarAction,SIGNAL(clicked()),SLOT(onRemoveToolBarAction()));

        toolBar = addToolBar("ToolBar");

        if(style()->inherits("QtMaemo6Style")) {
            isStyled = true;
        } else {
            qDBusRegisterMetaType<MDecoratorIPCAction>();
            qDBusRegisterMetaType<MDecoratorIPCActionList>();
            connect(interface, SIGNAL(triggered(QString,bool)), SLOT(onTriggeredDBus(QString,bool)));
            connect(interface, SIGNAL(toggled(QString,bool)), SLOT(onToggledDBus(QString,bool)));
        }
    }

public slots:

    void onAddMenuAction()
    {
        QAction* act = new QAction("MenuAction " +QString::number(menuActionCounter++),menuBar());
        act->setObjectName(act->text());
        connect(act,SIGNAL(triggered(bool)),SLOT(onTriggered(bool)));
        connect(act,SIGNAL(toggled(bool)),SLOT(onToggled(bool)));
        menuBar()->addAction(act);
        if (!isStyled)
            updateDecorator();
    }

    void onAddToolBarAction()
    {
        QAction* act = new QAction("ToolBarAction " +QString::number(toolBarActionCounter++),toolBar);
        act->setObjectName(act->text());
        act->setIcon(QIcon("/usr/share/themes/blanco/meegotouch/icons/icon-m-content-videos.png"));
        QIcon icon("/usr/share/themes/blanco/meegotouch/icons/icon-m-content-videos.png");

        connect(act,SIGNAL(triggered(bool)),SLOT(onTriggered(bool)));
        connect(act,SIGNAL(toggled(bool)),SLOT(onToggled(bool)));
        toolBar->addAction(act);
        if (!isStyled)
            updateDecorator();
    }

    void onRemoveMenuAction()
    {
        if (!menuBar()->actions().isEmpty()) {
            QAction* act = menuBar()->actions().last();
            menuBar()->removeAction(act);
            delete act;
            menuActionCounter--;
            if (!isStyled)
                updateDecorator();
        }
    }

    void onRemoveToolBarAction()
    {
        if (!toolBar->actions().isEmpty()) {
            QAction* act = toolBar->actions().last();
            toolBar->removeAction(act);
            delete act;
            toolBarActionCounter--;
            if (!isStyled)
                updateDecorator();
        }
    }

    void updateDecorator()
    {
        QList<MDecoratorIPCAction> list;
        foreach(QAction*act ,menuBar()->actions())
        {
            MDecoratorIPCAction iact(*act, MDecoratorIPCAction::MenuAction);
            list.append(iact);
        }
        foreach(QAction*act ,toolBar->actions())
        {
            MDecoratorIPCAction iact(*act, MDecoratorIPCAction::ToolBarAction);
            list.append(iact);
        }
        interface->setActions(list, winId());
    }

    void onTriggeredDBus(const QString& uuid, bool b)
    {
        qCritical()<<"Action with UUID"<<uuid<<"triggered. value:"<<b;
    }

    void onToggledDBus(const QString& uuid, bool b)
    {
        qCritical()<<"Action with UUID"<<uuid<<"toggled. value:"<<b;
    }

    void onTriggered(bool val)
    {
        if(!sender())
            return;
        qCritical()<<sender()<<"triggered with value:"<<val;
    }

    void onToggled(bool val)
    {
        if(!sender())
            return;
        qCritical()<<sender()<<"toggled with value:"<<val;
    }

private:
    int menuActionCounter;
    int toolBarActionCounter;
    QPushButton* addMenuAction;
    QPushButton* addToolBarAction;
    QPushButton* removeMenuAction;
    QPushButton* removeToolBarAction;
    QToolBar* toolBar;
    MDecoratorInterface* interface;
    bool isStyled;
};

int main (int argc, char** argv)
{
    QApplication app(argc, argv);

    MainWindow m;
    m.show();

    return app.exec();
}

#include "main.moc"
