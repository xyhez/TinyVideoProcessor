#include "syncmanager.h"

#include <QMutexLocker>

syncmanager::syncmanager(QObject *parent)
    : QObject{parent}
{

}

syncmanager::~syncmanager() {

}

int syncmanager::refreshClock(double clock) {
    QMutexLocker locker(&mutex);
    mainclock = clock;

    return 0;
}

double syncmanager::getClock() const {
    QMutexLocker locker(&mutex);
    return mainclock;
}

int syncmanager::Forword() {
    QMutexLocker locker(&mutex);
    mainclock += 3000;

    return 0;
}
