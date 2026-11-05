#ifndef USBWORKER_H
#define USBWORKER_H

#include <QObject>
#include <QByteArray>
#include "fx3communicator.h"

class USBWorker : public QObject
{
    Q_OBJECT
public:
    explicit USBWorker(QObject *parent = nullptr);
public slots:
    void connectDevice();
    void disconnectDevice();
    void sendData(const QByteArray &data);
    void testCommunication();
    void stop();
signals:
    void connected(bool success, const QString &info);
    void disconnected();
    void dataSent(int bytes);
    void dataReceived(const QByteArray &data);
    void testFinished(bool success);
    void error(const QString &error);
    void finished();
private:
    FX3Communicator *communicator;
    QMutex mutex;  // 添加互斥锁
};

#endif
