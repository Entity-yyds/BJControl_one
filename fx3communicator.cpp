#include "fx3communicator.h"
#include <QThread>
#include <QDebug>

FX3Communicator::FX3Communicator(QObject *parent)
    : QObject(parent), handle(nullptr), context(nullptr)
{
    const int ret = libusb_init(&context);
    if (ret < 0)
        emit errorOccurred(QString("libusb初始化失败: %1").arg(libusb_error_name(ret)));
}

FX3Communicator::~FX3Communicator()
{
    close();
    if (context)
        libusb_exit(context);
}

bool FX3Communicator::isConnected() const
{
    QMutexLocker locker(&mutex);
    return handle != nullptr;
}

bool FX3Communicator::init()
{
    QMutexLocker locker(&mutex);
    if (!context)
        return false;
    if (handle) {
        libusb_release_interface(handle, 0);
        libusb_close(handle);
        handle = nullptr;
    }

    handle = libusb_open_device_with_vid_pid(context, VID, PID);        // VID=0x04b4, PID=0x00f1 找设备
    if (!handle) {
        emit errorOccurred(QString("未找到FX3设备 (VID: 0x%1, PID: 0x%2)")
                           .arg(VID, 4, 16, QChar('0')).arg(PID, 4, 16, QChar('0')));
        return false;
    }

    if (libusb_kernel_driver_active(handle, 0) == 1)                    // 如果内核驱动占着，先 detach
        libusb_detach_kernel_driver(handle, 0);

    int ret = libusb_claim_interface(handle, 0);                        // 声明接口，失败会报 LIBUSB_ERROR_BUSY
    if (ret < 0) {
        emit errorOccurred(QString("声明USB接口失败: %1").arg(libusb_error_name(ret)));
        libusb_close(handle);
        handle = nullptr;
        return false;
    }

    // ===== 启动后台读线程 =====
    m_readRunning = true;
    m_readThread = std::thread(&FX3Communicator::readLoop, this);       // 启动后台读线程 readLoop()

    return true;
}

void FX3Communicator::close()
{
    // ===== 先停读线程 =====
    m_readRunning = false;
    if (m_readThread.joinable())
        m_readThread.join();

    QMutexLocker locker(&mutex);
    if (handle) {
        libusb_release_interface(handle, 0);
        libusb_close(handle);
        handle = nullptr;
    }
}

// ===== 后台读线程：持续循环读 EP_IN =====
void FX3Communicator::readLoop()
{
    qDebug() << "FX3 read thread started";

    // 读缓冲区：一次最多读 64KB
    const int BUF_SIZE = 64 * 1024;
    unsigned char *buf = (unsigned char *)malloc(BUF_SIZE);
    if (!buf) return;

    while (m_readRunning) {
        // 拿 handle 的快照，避免读线程和 close() 竞争
        libusb_device_handle *h;
        {
            QMutexLocker locker(&mutex);
            h = handle;
        }
        if (!h) break;

        int transferred = 0;
        // timeout=100ms：每100ms醒一次检查 m_readRunning，方便快速退出
        const int ret = libusb_bulk_transfer(h, EP_IN, buf, BUF_SIZE,
                                             &transferred, 100);
        if (ret == LIBUSB_SUCCESS && transferred > 0) {
            QByteArray data = QByteArray::fromRawData(reinterpret_cast<char*>(buf), transferred);
            emit dataRead(data);   // 发到主线程，日志打印
        }
        // timeout 是正常情况，不报错，继续循环
        else if (ret != LIBUSB_ERROR_TIMEOUT) {
            // 其他错误才打印（断开、设备拔了之类）
            // 不在这里 emit errorOccurred，避免刷屏
        }
    }

    free(buf);
    qDebug() << "FX3 read thread stopped";
}

bool FX3Communicator::writeData(const QByteArray &data, int timeout)
{
    QMutexLocker locker(&mutex);
    if (!handle) {
        emit errorOccurred("设备未连接");
        return false;
    }
    int transferred = 0;
    const int ret = libusb_bulk_transfer(handle, EP_OUT,
        reinterpret_cast<unsigned char *>(const_cast<char *>(data.constData())),
        data.size(), &transferred, timeout);
    if (ret < 0) {
        emit errorOccurred(QString("USB写入失败: %1").arg(libusb_error_name(ret)));
        return false;
    }
    emit dataWritten(transferred);
    return transferred == data.size();
}

QByteArray FX3Communicator::readData(int length, int timeout)
{
    QMutexLocker locker(&mutex);
    if (!handle) {
        emit errorOccurred("设备未连接");
        return QByteArray();
    }
    QByteArray buffer(length, 0);
    int transferred = 0;
    const int ret = libusb_bulk_transfer(handle, EP_IN,
        reinterpret_cast<unsigned char *>(buffer.data()), length, &transferred, timeout);
    if (ret < 0) {
        if (ret != LIBUSB_ERROR_TIMEOUT)
            emit errorOccurred(QString("USB读取失败: %1").arg(libusb_error_name(ret)));
        return QByteArray();
    }
    buffer.resize(transferred);
    emit dataRead(buffer);
    return buffer;
}

bool FX3Communicator::testCommunication()
{
    const QByteArray testData = QByteArray::fromHex(
        "33444459434950475A45524F00000800000000800000000050494300000000");
    if (!writeData(testData))
        return false;
    QThread::msleep(50);
    const QByteArray response = readData(testData.size());
    if (response != testData)
        emit errorOccurred(QString("USB回环校验失败: 发送%1字节，收到%2字节")
                           .arg(testData.size()).arg(response.size()));
    return response == testData;
}

QString FX3Communicator::getDeviceInfo() const
{
    QMutexLocker locker(&mutex);
    if (!handle)
        return "设备未连接";
    libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(libusb_get_device(handle), &desc) < 0)
        return "FX3设备";
    return QString("VID: 0x%1, PID: 0x%2").arg(desc.idVendor, 4, 16, QChar('0'))
           .arg(desc.idProduct, 4, 16, QChar('0'));
}

