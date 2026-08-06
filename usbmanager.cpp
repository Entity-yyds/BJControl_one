#include "usbmanager.h"
#include "mainwindow.h"
#include <QDebug>
#include <QTimer>
#include <QDateTime>

UsbManager::UsbManager(QObject *parent)
    : QObject(parent)           // ← 这里要调用父类构造函数
    , m_connected(false)
    , simulateTimer(nullptr)
{
    // 初始化模拟定时器
    simulateTimer = new QTimer(this);
    simulateTimer->setInterval(100);
    connect(simulateTimer, &QTimer::timeout, this, &UsbManager::simulateData);
}

UsbManager::~UsbManager()
{
    disconnectDevice();
    if (simulateTimer) {
        delete simulateTimer;
        simulateTimer = nullptr;
    }
}

bool UsbManager::connectDevice(const QString &portName)
{
    Q_UNUSED(portName);
    // TODO: 实现实际的USB连接
    // 模拟连接
    m_connected = true;
    m_deviceInfo = "BJ Printer Driver Board v1.0";
    if (simulateTimer) {
        simulateTimer->start();
    }
    emit statusChanged(true);
    return true;
}

void UsbManager::disconnectDevice()
{
    m_connected = false;
    if (simulateTimer) {
        simulateTimer->stop();
    }
    emit statusChanged(false);
}

bool UsbManager::isConnected() const
{
    return m_connected;
}

bool UsbManager::sendCommand(uint8_t cmd, const QByteArray &data)
{
    if (!m_connected) return false;
    // TODO: 实现命令发送
    Q_UNUSED(cmd);
    Q_UNUSED(data);
    return true;
}

bool UsbManager::sendData(const QByteArray &data)
{
    if (!m_connected) return false;
    // TODO: 实现数据发送
    //qDebug() << "Sending data:" << data.toHex();
    return true;
}

QStringList UsbManager::getAvailableDevices() const
{
    // TODO: 实现设备枚举
    // 模拟返回设备列表
    QStringList devices;
    devices << "BJ-Driver-001 (USB3.0)";
    devices << "BJ-Driver-002 (USB3.0)";
    return devices;
}

QString UsbManager::getDeviceInfo() const
{
    return m_deviceInfo;
}

void UsbManager::handleDeviceReadyRead()
{
    // TODO: 处理设备数据读取
}

void UsbManager::simulateData()
{
    // 模拟数据接收（仅用于演示）
    if (m_connected) {
        QByteArray fakeData;
        fakeData.append(static_cast<char>(0x80)); // 状态包
        fakeData.append(static_cast<char>(0x01));
        fakeData.append(static_cast<char>(0x00));
        emit dataReceived(fakeData);
    }
}
