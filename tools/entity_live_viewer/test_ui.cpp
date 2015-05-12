
#include <QApplication>
#include <QtGui>
#include <QMainWindow>
#include <QWidget>

//run directly
#include "ui_entity_live_viewer_form.h"

// run through another class
#include "viewer_ui.h"

int main( int argc, char* argv[] )
{

    QApplication app(argc, argv);

    // turn through another class
    ViewerUI testUI;
    testUI.show();
    testUI.setlabeltext("bla");

    app.exec();
}
