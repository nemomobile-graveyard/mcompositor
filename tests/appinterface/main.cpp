#include <QtGui>

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
    }

public slots:

    void onAddMenuAction()
    {
        QAction* act = new QAction("MenuAction " +QString::number(menuActionCounter++),menuBar());
        act->setObjectName(act->text());
        connect(act,SIGNAL(triggered(bool)),SLOT(onTriggered(bool)));
        connect(act,SIGNAL(toggled(bool)),SLOT(onToggled(bool)));
        menuBar()->addAction(act);
    }

    void onAddToolBarAction()
    {
        QAction* act = new QAction("ToolBarAction " +QString::number(toolBarActionCounter++),toolBar);
        act->setObjectName(act->text());
        connect(act,SIGNAL(triggered(bool)),SLOT(onTriggered(bool)));
        connect(act,SIGNAL(toggled(bool)),SLOT(onToggled(bool)));
        toolBar->addAction(act);
    }

    void onRemoveMenuAction()
    {
        if (!menuBar()->actions().isEmpty()) {
            QAction* act = menuBar()->actions().last();
            menuBar()->removeAction(act);
            delete act;
            menuActionCounter--;
        }
    }

    void onRemoveToolBarAction()
    {
        if (!toolBar->actions().isEmpty()) {
            QAction* act = toolBar->actions().last();
            toolBar->removeAction(act);
            delete act;
            toolBarActionCounter--;
        }
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
};

#include <main.moc>

int main (int argc, char** argv)
{
    QApplication app(argc, argv);

    MainWindow m;
    m.show();

    return app.exec();
}
