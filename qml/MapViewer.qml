// MapViewer.qml
import QtQuick
import QtLocation
import QtPositioning

Item {
    anchors.fill: parent

    Map {
        id: map
        anchors.fill: parent

        center: QtPositioning.coordinate(50.807, 8.770)
        zoomLevel: 14

        MapUrlTileLayer {
            id: tiles
            baseUrl: "https://tile.openstreetmap.org/{z}/{x}/{y}.png"
            minimumZoomLevel: 0
            maximumZoomLevel: 19
            tileSize: 256
            format: "png"
        }
    }
}
