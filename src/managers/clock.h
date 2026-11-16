#ifndef CLOCK_H
#define CLOCK_H

#include <QObject>
#include <QString>
#include <QTimer>

class SettingsManager;

// The bar's clock text, in the user's format ("" while showClock is off).
// `time` is readable (a client subscribing gets it at once); timeChanged
// fires only when the text changes, so once a minute without seconds.
class Clock : public QObject
{
    Q_OBJECT
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "read")
    // Deprecated names (src/service/metadescribe.h): the camelCase one beside each stays
    Q_CLASSINFO("octave.deprecated.update_time", "use updateTime; goes in protocol 5")
    Q_PROPERTY(QString time READ time NOTIFY timeChanged)

public:
    explicit Clock(SettingsManager *settingsManager, QObject *parent = nullptr);

    QString time() const { return m_lastText; }

signals:
    void timeChanged(const QString &timeStr);

public slots:
    void update_time();

    // camelCase names for the snake_case methods above (protocol 3 keeps both;
    // the old names are marked octave.deprecated and go in protocol 5)
    void updateTime() { update_time(); }

private:
    SettingsManager *m_settingsManager;
    QTimer m_timer;
    QString m_lastText;   // last emitted text; emit only on change
};

#endif // CLOCK_H
