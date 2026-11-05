#include "vacuumcontroller.h"
#include <QDataStream>

namespace {
const QHostAddress VacuumAddress(QStringLiteral("192.168.2.6"));
const quint16 FrameHead = 0x00ff;
const quint16 FrameEnd = 0x00ee;
}

const quint16 VacuumController::PORT;

VacuumController::VacuumController(QObject *parent)
    : QObject(parent), socket(new QTcpSocket(this)), reconnectTimer(new QTimer(this)), manuallyDisconnected(true), m_connectionErrorReported(false)
{
    reconnectTimer->setInterval(2000);
    connect(socket, &QTcpSocket::connected, this, &VacuumController::onConnected);
    connect(socket, &QTcpSocket::readyRead, this, &VacuumController::onReadyRead);  // 数据到达入口 上行（设备→界面）
    connect(socket, &QTcpSocket::disconnected, this, [this]() {
        emit connectionChanged(false);
        if (!manuallyDisconnected) reconnectTimer->start();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, &VacuumController::onSocketError);
    connect(reconnectTimer, &QTimer::timeout, this, &VacuumController::reconnect);
}

void VacuumController::connectDevice()
{
    manuallyDisconnected = false;
    reconnectTimer->stop();
    if (socket->state() == QAbstractSocket::UnconnectedState)
        socket->connectToHost(VacuumAddress, PORT);
}

void VacuumController::disconnectDevice()
{
    manuallyDisconnected = true;
    reconnectTimer->stop();
    socket->disconnectFromHost();
}

bool VacuumController::isConnected() const { return socket->state() == QAbstractSocket::ConnectedState; }

void VacuumController::sendCommand(qint16 cmd, qint16 state, qint16 sdata, float fdata)     // 发送（下行：上位机 → 设备）
{
    if (!isConnected()) {
        emit errorOccurred("负压控制器未连接");
        return;
    }
    //socket->write(makeFrame(cmd, state, sdata, fdata)); // ← 发送就在这里
    QByteArray frame = makeFrame(cmd, state, sdata, fdata);
    qDebug() << "TX负压帧:" << frame.toHex(' ') << " fdata=" << fdata;
    socket->write(frame);
}

QByteArray VacuumController::makeFrame(qint16 cmd, qint16 state, qint16 sdata, float fdata) const       // 组帧
{
//    QByteArray frame;
//    QDataStream stream(&frame, QIODevice::WriteOnly);
//    stream.setByteOrder(QDataStream::LittleEndian);
//    stream << quint16(FrameHead) << cmd << state << sdata << fdata << quint16(FrameEnd);
//    return frame;
    QByteArray frame;
    frame.resize(14);
    quint8 *b = reinterpret_cast<quint8*>(frame.data());

    // 帧头 ff 00
    b[0] = 0xff;
    b[1] = 0x00;

    // cmd / state / sdata：各 2 字节，小端
    b[2] = quint8(cmd & 0xff);
    b[3] = quint8((cmd >> 8) & 0xff);
    b[4] = quint8(state & 0xff);
    b[5] = quint8((state >> 8) & 0xff);
    b[6] = quint8(sdata & 0xff);
    b[7] = quint8((sdata >> 8) & 0xff);

    // fdata：4 字节 float，小端直接拷（香橙派 ARM 小端）
    memcpy(b + 8, &fdata, sizeof(float));

    // 帧尾 ee 00
    b[12] = 0xee;
    b[13] = 0x00;

    return frame;
}

void VacuumController::onConnected()
{
    reconnectTimer->stop();
    emit connectionChanged(true);
    // 连接成功后，向下发一次 cmd=0x0030 请求，固件据此返回一级/二级/循环负压值
    sendCommand(0x0030, 0, 0, 0.0f);
    sendCommand(0x0060, 0, 0, 0.0f);
}
void VacuumController::onReadyRead()
{
    QByteArray chunk = socket->readAll();
    //qDebug() << "RX raw:" << chunk.toHex(' ');   // ← 加这行，看设备原始数据
    receiveBuffer.append(chunk);
    parseFrames();
//    receiveBuffer.append(socket->readAll());
//    parseFrames();
}
void VacuumController::resetConnectionErrorFlag() { m_connectionErrorReported = false; }

void VacuumController::parseFrames()
{
    while (receiveBuffer.size() >= FRAME_SIZE) {
        int head = -1;
        for (int i = 0; i <= receiveBuffer.size() - FRAME_SIZE; ++i)
        {
            if (quint8(receiveBuffer.at(i)) == 0xff && quint8(receiveBuffer.at(i + 1)) == 0x00) { head = i; break; }
        }
        if (head < 0)
        {
            receiveBuffer = receiveBuffer.right(FRAME_SIZE - 1);
            return;
        }
        if (head > 0) receiveBuffer.remove(0, head);
        if (quint8(receiveBuffer.at(12)) != 0xee || quint8(receiveBuffer.at(13)) != 0x00)
        {
            receiveBuffer.remove(0, 1);
            continue;
        }
//        // ★ 关键修复：先存成具名变量，再建 QDataStream，临时对象不再提前销毁
//        QByteArray frame = receiveBuffer.left(FRAME_SIZE);
//        QDataStream stream(&frame, QIODevice::ReadOnly);
//        stream.setByteOrder(QDataStream::LittleEndian);
////        quint16 ignoredHead, ignoredEnd; qint16 cmd, state, sdata; float fdata;
////        stream >> ignoredHead >> cmd >> state >> sdata >> fdata >> ignoredEnd;
////        emit frameReceived(cmd, state, sdata, fdata);
////        receiveBuffer.remove(0, FRAME_SIZE);
//        quint16 h = 0, e = 0; qint16 cmd = 0, state = 0, sdata = 0; float fdata = 0;
//        stream >> h >> cmd >> state >> sdata >> fdata >> e;

//        qDebug() << "候选帧 raw=" << frame.toHex(' ')
//                 << "head=" << h << " cmd=" << cmd << " state=" << state
//                 << " sdata=" << sdata << " fdata=" << fdata << " end=" << e;
//        // 帧头帧尾都正确才上抛（先看打印，确认帧尾真实值）
//        if (h == 0x00ff && e == 0x00ee) {
//            emit frameReceived(cmd, state, sdata, fdata);
//        }
//        receiveBuffer.remove(0, FRAME_SIZE);

        QByteArray frame = receiveBuffer.left(FRAME_SIZE);   // 帧已通过帧尾校验，这里必是 14 字节

        // 直接按固定偏移解析（小端，香橙派 ARM 就是小端）
        quint16 h     = quint8(frame.at(0)) | (quint8(frame.at(1)) << 8);
        qint16  cmd   = quint8(frame.at(2)) | (quint8(frame.at(3)) << 8);
        qint16  state = quint8(frame.at(4)) | (quint8(frame.at(5)) << 8);
        qint16  sdata = quint8(frame.at(6)) | (quint8(frame.at(7)) << 8);
        float   fdata;
        memcpy(&fdata, frame.constData() + 8, sizeof(float)); // 小端平台直接拷 4 字节
        quint16 e     = quint8(frame.at(12)) | (quint8(frame.at(13)) << 8);

//        qDebug() << "候选帧 raw=" << frame.toHex(' ')
//                 << "head=" << h << " cmd=" << cmd << " state=" << state
//                 << " sdata=" << sdata << " fdata=" << fdata << " end=" << e;

        if (h == 0x00ff && e == 0x00ee) {
            emit frameReceived(cmd, state, sdata, fdata);
        }

        receiveBuffer.remove(0, FRAME_SIZE);
    }
}

void VacuumController::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    // emit errorOccurred(QString("负压TCP错误: %1").arg(socket->errorString()));
    // 同一段断连期间只提示一次，避免 2 秒重连失败反复刷屏
    if (!m_connectionErrorReported)
    {
        m_connectionErrorReported = true;
        emit errorOccurred(QString("负压TCP错误: %1").arg(socket->errorString()));
    }
    if (!manuallyDisconnected) reconnectTimer->start();
}

void VacuumController::reconnect()
{
    if (!isConnected()) socket->connectToHost(VacuumAddress, PORT);
}
