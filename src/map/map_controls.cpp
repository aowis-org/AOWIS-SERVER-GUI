#include "map_controls.h"

MapControls::MapControls(QWidget *parent)
    : QWidget(parent)
{
    m_buttonZoomOut = new QPushButton("-", this);
    m_buttonZoomIn = new QPushButton("+", this);
    m_labelZoom = new QLabel("Zoom: -", this);
    m_comboProvider = new QComboBox(this);
    
    m_comboProvider->addItem("ArcGIS Satellite", QVariant::fromValue(int(MapProvider::ArcGISSat)));
    m_comboProvider->addItem("OpenTopoMap", QVariant::fromValue(int(MapProvider::OpenTopoMap)));
    m_comboProvider->addItem("OpenStreetMap", QVariant::fromValue(int(MapProvider::OpenStreetMap)));
    
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_buttonZoomOut);
    layout->addWidget(m_buttonZoomIn);
    layout->addWidget(m_labelZoom);
    layout->addWidget(m_comboProvider);
    layout->addStretch();
    
    connect(m_buttonZoomIn, &QPushButton::clicked,
            this, &MapControls::zoomInRequested);
    
    connect(m_buttonZoomOut, &QPushButton::clicked,
            this, &MapControls::zoomOutRequested);
    
    connect(m_comboProvider, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                const auto provider = MapProvider(m_comboProvider->itemData(index).toInt());
                emit providerChanged(provider);
            });
}

void MapControls::setZoom(int zoom)
{
    m_labelZoom->setText(QString("Zoom: %1").arg(zoom));
}

void MapControls::setProvider(MapProvider provider)
{
    const int wanted = int(provider);
    
    for (int i = 0; i < m_comboProvider->count(); ++i)
    {
        if (m_comboProvider->itemData(i).toInt() == wanted)
        {
            m_comboProvider->setCurrentIndex(i);
            return;
        }
    }
}
