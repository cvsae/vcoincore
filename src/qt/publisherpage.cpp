#include "publisherpage.h"
#include "ui_publisherpage.h"

#include "bitcoinunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "walletmodel.h"

#include <QFile>
#include <QString>
#include <QDir>
#include <QTextStream>
#include <QWebEngineView>

class QWebEngineView;

#define DECORATION_SIZE 64
#define NUM_ITEMS 3

PublisherPage::PublisherPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PublisherPage),
    clientModel(0),
    walletModel(0)
{
    ui->setupUi(this);

}

PublisherPage::~PublisherPage()
{
    delete ui;
}

void PublisherPage::setClientModel(PlatformStyle *platformStyle, ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
    }
}

void PublisherPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;

}

void PublisherPage::show()
{
    QWebEngineView *view = new QWebEngineView;
    QDir dir;
    QString cwd = dir.currentPath();
    QString html = "qrc:///pbt/index.html";
    QString fpath = "/src/qt/res/pbt/index.html";
    QString res = cwd + fpath;
    QFile file(res);
    // QFile file("file:////res/pbt/index.html");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        html = QString("<html><body><p>Cannot read file in ") + res + QString("</p></body></html>");
    } else {
        html = QTextStream(&file).readAll();
    };
    view->setHtml(html, QUrl());
    view->show();
}
