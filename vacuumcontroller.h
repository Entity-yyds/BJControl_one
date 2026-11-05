#ifndef VACUUMCONTROLLER_H
#define VACUUMCONTROLLER_H

#include <QObject>      // 信号槽基类，类要能发信号/槽必须继承它
#include <QByteArray>   // 字节数组，收发的原始帧数据
#include <QHostAddress> // IP 地址封装（192.168.2.6）
#include <QTcpSocket>   // TCP 客户端套接字，真正的网络收发
#include <QTimer>       // 定时器，用于断线自动重连

class VacuumController : public QObject
{                   // 是信号槽机制的核心，编译器通过它生成 `moc` 代码，`signals`/`slots` 里的函数才能被 Qt 运行时调用
    Q_OBJECT        // Qt 元对象宏：让类获得信号槽、属性等元对象能力
public:
    explicit VacuumController(QObject *parent = nullptr);       // `explicit` 防止隐式类型转换
    void connectDevice();           // 发起 TCP 连接 + 启动自动重连
    void disconnectDevice();        // 手动断开（并停止重连）
    bool isConnected() const;       // 当前是否已连接
    void sendCommand(qint16 cmd, qint16 state = 0, qint16 sdata = 0, float fdata = 0.0f);   // 向负压设备发一条命令帧
    void resetConnectionErrorFlag();   // 清空"本次断连已提示"标志，允许再次提示

signals:        // 向外通知的事件（只声明，不实现）
    void connectionChanged(bool connected);     // // 连接/断开状态变化
    void frameReceived(qint16 cmd, qint16 state, qint16 sdata, float fdata);    // 收到一帧完整数据
    void errorOccurred(const QString &error);   // 出错
    // 信号由 Qt 自动生成实现，类内部用 `emit xxx(...)` 发射，外部（这里是 `MainWindow`）通过 `connect` 订阅：
private slots:  // 内部事件处理
    void onConnected();     // 连接成功 → 停重连定时器 + 发 connectionChanged(true)
    void onReadyRead();     // 有数据可读 → 收进 receiveBuffer 再调 parseFrames()
    void onSocketError(QAbstractSocket::SocketError error); // 网络错误 → 发 errorOccurred + 触发重连
    void reconnect();       // 定时重连
    // 它们被 `connect` 绑定到 `socket` 和 `reconnectTimer` 的信号上：
private:        // 内部状态与工具
    QByteArray makeFrame(qint16 cmd, qint16 state, qint16 sdata, float fdata) const;
    // 组帧：把参数打包成 14 字       小端帧（头0x00ff … 尾0x00ee）
    void parseFrames();
    // 流式解析：从 receiveBuffer 里按帧头尾剥离出完整帧，emit frameReceived
    QTcpSocket *socket;                 // 套接字
    QTimer *reconnectTimer;             // 重连定时器（2s 间隔）
    QByteArray receiveBuffer;           // 未处理完的原始字节（处理 TCP 粘包/半包）
    bool manuallyDisconnected;          // 是否手动断开（为 true 时不自动重连）
    bool m_connectionErrorReported;     // 本次断连期间是否已提示过重连错误（只提示一次）
    static const quint16 PORT = 1030;   // 服务端端口
    static const int FRAME_SIZE = 14;   // 帧总长 14 字节
};

#endif
