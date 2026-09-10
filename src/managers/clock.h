#ifndef CLOCK_H
#define CLOCK_H

#include <QObject>
#include <QString>
#include <QTimer>

class SettingsManager;

class Clock : public QObject
{
    Q_OBJECT

public:
    explicit Clock(SettingsManager *settingsManager, QObject *parent = nullptr);

signals:
    void timeChanged(const QString &timeStr);

public slots:
    void update_time();

private:
    SettingsManager *m_settingsManager;
    QTimer m_timer;
    QString m_lastText;   // last emitted text; emit only on change
};

#endif // CLOCK_H
