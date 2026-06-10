#include "main_window.h"
#include <QPushButton>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    QWidget *widget_central = new QWidget(this);
    this->setCentralWidget(widget_central);
    widget_central->setLayout(this->layout);
    
    this->label_image_a = new QLabel();
    this->label_image_b = new QLabel();
    
    this->line_server_status = new QLineEdit();
    this->line_server_status->setDisabled(true);
    
    this->layout->addWidget(label_image_a, 0, 0);
    this->layout->addWidget(label_image_b, 0, 1);
    this->layout->addWidget(this->line_server_status, 1, 0, 1, 2);
    
    this->setMinimumHeight(500);
    this->setMinimumWidth(550);
    
    this->menu = new MenuBar;
    setMenuBar(this->menu);
    
    checkAPIServer();
    
    getOpenStreetMapTile(this->label_image_a, "/osm/2/3/0.png");
    getOpenStreetMapTile(this->label_image_b, "/osm/2/3/0.png");
}

void MainWindow::checkAPIServer()
{
    // Testing the HTTP connection to AOWIS-Map-Server
    
    RESTClient *rest = new RESTClient("http://aowis-server-map.localhost:80", this);
    connect(rest, &RESTClient::requestFinished, this, [this, rest](const QByteArray &data)
    {
        this->line_server_status->setText(data);
        
        rest->deleteLater();
    });
    connect(rest, &RESTClient::requestError, this, [this, rest](const QString &err)
    {
        this->line_server_status->setText("REST ERROR: " + err);
        
        rest->deleteLater();
    });
    rest->get("/status");
}

void MainWindow::getOpenStreetMapTile(QLabel *label, QString endpoint)
{
    // Testing GET OSM Tile from AOWIS-Map-Server
    
    RESTClient *rest = new RESTClient("http://aowis-server-map.localhost:80", this);
    connect(rest, &RESTClient::requestFinished, this, [this, rest, label](const QByteArray &data)
    {
        
        QPixmap pix;
        pix.loadFromData(data);
        
        label->setPixmap(pix);
        
        rest->deleteLater();
    });
    connect(rest, &RESTClient::requestError, this, [this, rest](const QString &err)
    {
        qDebug() << "fail: " << err;
        
        rest->deleteLater();
    });
    rest->get(endpoint);
}
