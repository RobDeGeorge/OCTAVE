#ifndef VEHICLEMANAGER_H
#define VEHICLEMANAGER_H

// The vehicle the app shows: the 3D view (frontend/VehicleView.qml), the
// Home card and the "No vehicle connected" picture. The built-in one is the
// rigged Jeep TJ (frontend/assets/jeep_tj, id kBuiltInId). Anyone can add
// their own model: each is a folder <config>/vehicles/<id>/ holding
//
//   vehicle.json   {"name": "...", "model": "model.glb", "yaw": 0, "tilt": 0}
//   model.glb      the model (glTF binary; a .gltf with its files beside it
//                  works for a folder made by hand)
//   preview.png    its picture, rendered by the app the first time the
//                  model is shown (Settings > Device > Vehicle)
//
// A model is data only: it is shown whole, turning with the IMU like the
// Jeep, with no moving parts (those come from a rig, and only the built-in
// vehicle has one). `yaw` and `tilt` (degrees) stand a model the right way
// up when its file has other ideas about which way is forward or up; the
// view scales and centres it by itself.
//
// importModel() copies one .glb into a new folder. A folder dropped in by
// hand is listed too, when its manifest names a model file that is inside
// the folder. The file ACTIVE under <config>/vehicles/ holds the active id
// (absent = the built-in vehicle).
//
// Service object `vehicleManager` (experimental).

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class VehicleManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("octave.experimental", "*")
    Q_CLASSINFO("octave.scope", "settings")
    Q_CLASSINFO("octave.scope.files", "importModel")
    Q_CLASSINFO("octave.scope.read", "picturePath")
    // [{id, name, builtIn, rigged, active, pictureUrl}], the built-in first
    Q_PROPERTY(QVariantList vehicles READ vehicles NOTIFY vehiclesChanged)
    // The vehicle shown; kBuiltInId when none is chosen or the chosen one is gone
    Q_PROPERTY(QString activeId READ activeId WRITE select NOTIFY activeChanged)
    // {id, name, builtIn, rigged, modelUrl, pictureUrl, yaw, tilt}. modelUrl
    // is "" for the built-in rig; pictureUrl is "" until a picture exists
    // (the built-in vehicle's picture ships with the frontend)
    Q_PROPERTY(QVariantMap active READ active NOTIFY activeChanged)
    // False where a model can't be picked from a file (the browser build)
    Q_PROPERTY(bool canImport READ canImport CONSTANT)
    // Why the last import / remove / select failed ("" after a success)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    static constexpr qint64 kMaxModelBytes = 256LL * 1024 * 1024;
    static constexpr int kMaxVehicles = 32;
    static inline const QString kBuiltInId = QStringLiteral("jeep-tj");

    explicit VehicleManager(QObject *parent = nullptr);

    // <config>/vehicles (AppCore); made if missing
    void setRoot(const QString &dir);
    QString root() const { return m_root; }

    QVariantList vehicles() const;
    QString activeId() const;
    QVariantMap active() const;
    bool canImport() const;
    QString lastError() const { return m_lastError; }

    // Copy a .glb in as a new vehicle and make it the active one. `path` is
    // a path or the URL a file picker gives; `name` defaults to the file's
    // name. The new id, or "" (lastError says why)
    Q_INVOKABLE QString importModel(const QString &path, const QString &name = QString());
    // Delete an added vehicle; the built-in one can't be removed
    Q_INVOKABLE bool remove(const QString &id);
    // "" or kBuiltInId = the built-in vehicle
    Q_INVOKABLE bool select(const QString &id);
    Q_INVOKABLE bool rename(const QString &id, const QString &name);
    // Degrees about the up axis (yaw) and the side axis (tilt)
    Q_INVOKABLE bool setOrientation(const QString &id, double yaw, double tilt);
    // Where the app saves the picture it renders of an added vehicle ("" for
    // the built-in one or an unknown id), and the call that says it did
    Q_INVOKABLE QString picturePath(const QString &id) const;
    Q_INVOKABLE void pictureSaved(const QString &id);

signals:
    void vehiclesChanged();
    void activeChanged();
    void lastErrorChanged();

private:
    struct Entry {
        QString id;
        QString name;
        QString model;   // file name inside the folder
        double yaw = 0;
        double tilt = 0;
    };
    QList<Entry> installed() const;
    bool find(const QString &id, Entry *out) const;
    bool writeManifest(const Entry &e) const;
    QString dirOf(const QString &id) const;
    QString pictureUrlOf(const QString &id) const;
    QVariantMap describe(const Entry &e) const;
    bool fail(const QString &why);
    void ok();

    QString m_root;
    QString m_lastError;
};

#endif // VEHICLEMANAGER_H
