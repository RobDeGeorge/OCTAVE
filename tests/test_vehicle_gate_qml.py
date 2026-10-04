"""Vehicle support gate — QML side, backend-agnostic.

Loads the real OBDParameterModel singleton and DashboardRenderer against a
fake `obdManager` that exposes the same gate API both backends implement
(vehicleSupportedParameters / vehicleScanComplete / setParameterDemand), then
checks what the renderer shows before a scan, after a scan, and after the
vehicle disconnects.
"""
import os

from PySide6.QtCore import Property, QObject, QUrl, Signal, Slot
from PySide6.QtQml import QQmlComponent, QQmlEngine

FRONTEND = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "frontend"))

QML = """
import QtQuick
import "%(fe)s" as App
import "%(fe)s/dashboards" as Dash

Item {
    id: root
    width: 1200; height: 600
    property var model: App.OBDParameterModel
    Dash.DashboardRenderer {
        objectName: "renderer"
        anchors.fill: parent
        spec: ({ gridColumns: 12, gridRows: 6, cells: [
            { type: "DigitalReadout", paramId: "RPM",              col: 0, row: 0, colSpan: 6, rowSpan: 3 },
            { type: "DigitalReadout", paramId: "BOOST_PRESSURE_A", col: 6, row: 0, colSpan: 6, rowSpan: 3 },
            { type: "DigitalReadout", paramId: "PITCH",            col: 0, row: 3, colSpan: 6, rowSpan: 3 }
        ] })
    }
    function supported(id) { return App.OBDParameterModel.isSupported(id) }
    // Visual-tree lookup (Repeater delegates aren't QObject children, and
    // PySide can't recast QML items to QQuickItem reliably).
    function _find(item, name) {
        if (item.objectName === name) return item
        for (var i = 0; i < item.children.length; i++) {
            var hit = _find(item.children[i], name)
            if (hit) return hit
        }
        return null
    }
    function placeholderVisible(i) {
        var it = _find(root, "dashUnsupported_" + i)
        return it ? it.visible : null
    }
    function vehicleIds() { return App.OBDParameterModel.vehicleParameters.map(function(p) { return p.id }) }
}
"""


class FakeObd(QObject):
    vehicleSupportedParametersChanged = Signal()

    def __init__(self):
        super().__init__()
        self._supported, self._complete = [], False
        self.demand = {}

    def _get_supported(self):
        return self._supported

    def _get_complete(self):
        return self._complete

    vehicleSupportedParameters = Property(list, _get_supported, notify=vehicleSupportedParametersChanged)
    vehicleScanComplete = Property(bool, _get_complete, notify=vehicleSupportedParametersChanged)

    @Slot(str, list)
    def setParameterDemand(self, consumer, ids):
        self.demand[consumer] = list(ids)

    def scan(self, names, complete=True):
        self._supported, self._complete = list(names), complete
        self.vehicleSupportedParametersChanged.emit()


def _placeholder_visible(call, index):
    v = call("placeholderVisible", index)
    assert v is not None, f"placeholder {index} missing"
    return v


def test_vehicle_gate(qapp):
    engine = QQmlEngine()
    engine.addImportPath(FRONTEND)
    obd = FakeObd()
    engine.rootContext().setContextProperty("obdManager", obd)
    comp = QQmlComponent(engine)
    comp.setData((QML % {"fe": QUrl.fromLocalFile(FRONTEND).toString()}).encode(), QUrl())
    root = comp.create()
    assert root is not None, comp.errorString()
    for _ in range(5):
        qapp.processEvents()

    def call(name, *args):
        from PySide6.QtCore import QMetaObject, Q_RETURN_ARG, Q_ARG, Qt
        from PySide6.QtQml import QJSValue
        if args:
            v = QMetaObject.invokeMethod(root, name, Qt.DirectConnection,
                                         Q_RETURN_ARG("QVariant"), Q_ARG("QVariant", args[0]))
        else:
            v = QMetaObject.invokeMethod(root, name, Qt.DirectConnection, Q_RETURN_ARG("QVariant"))
        return v.toVariant() if isinstance(v, QJSValue) else v

    # No vehicle: nothing gated, Parameter Cards list empty.
    assert not any(_placeholder_visible(call, i) for i in range(3))
    assert call("vehicleIds") == []
    assert call("supported", "PITCH") is True       # sensors never gated
    assert call("supported", "RPM") is False        # unknown until scanned

    # Scanned vehicle supports RPM + SPEED only.
    obd.scan(["RPM", "SPEED"])
    qapp.processEvents()
    assert _placeholder_visible(call, 0) is False   # RPM supported
    assert _placeholder_visible(call, 1) is True    # BOOST not reported → placeholder
    assert _placeholder_visible(call, 2) is False   # PITCH is a sensor
    assert call("vehicleIds") == ["SPEED", "RPM"]   # allParameters order, OBD only

    # Disconnect: gate lifts, list empties again.
    obd.scan([], complete=False)
    qapp.processEvents()
    assert not any(_placeholder_visible(call, i) for i in range(3))
    assert call("vehicleIds") == []
    root.deleteLater()
