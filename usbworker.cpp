#include "usbworker.h"

USBWorker::USBWorker(QObject *parent) : QObject(parent), communicator(new FX3Communicator(this))
{
    connect(communicator, &FX3Communicator::dataRead, this, &USBWorker::dataReceived);
    connect(communicator, &FX3Communicator::dataWritten, this, &USBWorker::dataSent);
    connect(communicator, &FX3Communicator::errorOccurred, this, &USBWorker::error);
}

void USBWorker::connectDevice()
{
    const bool success = communicator->init();
    emit connected(success, success ? communicator->getDeviceInfo() : "连接失败");
}

void USBWorker::disconnectDevice()
{
    communicator->close();
    emit disconnected();
}

void USBWorker::sendData(const QByteArray &data)
{
    QMutexLocker locker(&mutex);
        if (communicator && communicator->isConnected()) {
            communicator->writeData(data);
        }
}
void USBWorker::testCommunication() { emit testFinished(communicator->testCommunication()); }

void USBWorker::stop()
{
    communicator->close();
    emit finished();
}
