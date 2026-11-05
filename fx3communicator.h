#ifndef FX3COMMUNICATOR_H
#define FX3COMMUNICATOR_H

#include <QObject>
#include <QByteArray>
#include <QMutex>
#include <QString>
#include <atomic>
#include <thread>
#include <libusb-1.0/libusb.h>

class FX3Communicator : public QObject
{
    Q_OBJECT
public:
    explicit FX3Communicator(QObject *parent = nullptr);
    ~FX3Communicator();

    bool init();
    void close();
    bool isConnected() const;
    bool writeData(const QByteArray &data, int timeout = 2000);
    QByteArray readData(int length, int timeout = 2000);
    bool testCommunication();
    QString getDeviceInfo() const;

signals:
    void dataWritten(int bytes);
    void dataRead(const QByteArray &data);   // ← 读线程收到数据就发这个信号
    void errorOccurred(const QString &error);

private:
    void readLoop();   // 后台读线程主函数

    libusb_device_handle *handle;
    mutable QMutex mutex;       // 只保护 handle 的打开/关闭
    libusb_context *context;

    std::atomic<bool> m_readRunning{false};  // 读线程运行标志
    std::thread m_readThread;                // 后台读线程

    static const uint16_t VID = 0x04b4;
    static const uint16_t PID = 0x00f1;
    static const uint8_t EP_OUT = 0x01;
    static const uint8_t EP_IN = 0x81;
};

#endif
