#ifndef USBMANAGER_H
#define USBMANAGER_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QTimer>

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

    // 获取设备列表
    QStringList getAvailableDevices() const;

    // 设备信息
    QString getDeviceInfo() const;

signals:
    void statusChanged(bool connected);
    void dataReceived(const QByteArray &data);
    void errorOccurred(const QString &error);

private slots:
    void handleDeviceReadyRead();

private:
    bool m_connected;
    QString m_deviceInfo;

    // 后期添加实际的USB通信对象
    //QSerialPort *serialPort;    // 如果使用串口
    // QUsbDevice *usbDevice;   // 如果使用QtUsb

    // 模拟数据定时器（仅用于演示）
    QTimer *simulateTimer;

    void simulateData();
};

#endif // USBMANAGER_H
