#ifndef USBMANAGER_H
#define USBMANAGER_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QThread>
#include <cstdint>
#include "usbworker.h"

class UsbManager : public QObject
{
    Q_OBJECT

public:
    // 构造函数声明 - 注意参数
    explicit UsbManager(QObject *parent = nullptr);
    ~UsbManager();

    // 连接管理
    bool connectDevice(const QString &portName = QString());
    void disconnectDevice();
    bool isConnected() const;

    // 数据发送
    bool sendCommand(uint8_t cmd, const QByteArray &data = QByteArray());
    bool sendData(const QByteArray &data);
    void testCommunication();

    // 获取设备列表
    QStringList getAvailableDevices() const;

    // 设备信息
    QString getDeviceInfo() const;

signals:
    void statusChanged(bool connected);
    void dataReceived(const QByteArray &data);
    void errorOccurred(const QString &error);
    void testFinished(bool success);

private slots:
    void handleDeviceReadyRead();
    void handleConnected(bool success, const QString &info);
    void handleDisconnected();
    void handleDataSent(int bytes);

private:
    bool m_connected;
    QString m_deviceInfo;

    QThread *workerThread;
    USBWorker *worker;
};

#endif // USBMANAGER_H
