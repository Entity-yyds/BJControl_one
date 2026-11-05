#include "usbmanager.h"
#include <QMetaObject>

UsbManager::UsbManager(QObject *parent)
    : QObject(parent)           // ← 这里要调用父类构造函数
    , m_connected(false)
    , workerThread(new QThread(this))
    , worker(new USBWorker())
{
    worker->moveToThread(workerThread);
    connect(workerThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &USBWorker::connected, this, &UsbManager::handleConnected);
    connect(worker, &USBWorker::disconnected, this, &UsbManager::handleDisconnected);
    connect(worker, &USBWorker::dataReceived, this, &UsbManager::dataReceived);
    connect(worker, &USBWorker::dataSent, this, &UsbManager::handleDataSent);
    connect(worker, &USBWorker::error, this, &UsbManager::errorOccurred);
    connect(worker, &USBWorker::testFinished, this, &UsbManager::testFinished);
    workerThread->start();
}

UsbManager::~UsbManager()
{
    if (workerThread->isRunning())
        disconnectDevice();
    workerThread->quit();
    workerThread->wait();
}

bool UsbManager::connectDevice(const QString &portName)
{
    Q_UNUSED(portName);
    return QMetaObject::invokeMethod(worker, "connectDevice", Qt::QueuedConnection);    // 通过 QMetaObject::invokeMethod 发到 worker 线程
}

void UsbManager::disconnectDevice()
{
    QMetaObject::invokeMethod(worker, "disconnectDevice", Qt::BlockingQueuedConnection);
}

bool UsbManager::isConnected() const
{
    return m_connected;
}

bool UsbManager::sendCommand(uint8_t cmd, const QByteArray &data)
{
    if (!m_connected) return false;
    QByteArray packet(1, static_cast<char>(cmd));
    packet.append(data);
    return sendData(packet);
}

bool UsbManager::sendData(const QByteArray &data)
{
    if (!m_connected) return false;
    return QMetaObject::invokeMethod(worker, "sendData", Qt::QueuedConnection,
                                     Q_ARG(QByteArray, data));
}

QStringList UsbManager::getAvailableDevices() const
{
    QStringList devices;
    devices << "FX3 (VID 0x04b4, PID 0x00f1)";
    return devices;
}

QString UsbManager::getDeviceInfo() const
{
    return m_deviceInfo;
}

void UsbManager::testCommunication() { QMetaObject::invokeMethod(worker, "testCommunication", Qt::QueuedConnection); }
void UsbManager::handleDeviceReadyRead() {}
void UsbManager::handleConnected(bool success, const QString &info) { m_connected = success; m_deviceInfo = info; emit statusChanged(success); }
void UsbManager::handleDisconnected() { m_connected = false; emit statusChanged(false); }
void UsbManager::handleDataSent(int) {}
