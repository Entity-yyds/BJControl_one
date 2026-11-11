#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include "usbmanager.h"             // USB设备管理
#include "saveparams.h"             // 参数保存/加载
#include "vacuumcontroller.h"       // 负压控制器TCP通信
#include "clifillworker.h"


#include <QMainWindow>
#include <QTimer>           // 定时器，用于状态更新
#include <QThread>          // 多线程支持
#include <QComboBox>        // 下拉选择框
#include <QPushButton>      // 按钮
#include <QLabel>           // 文本标签
#include <QTextEdit>        // 多行文本编辑/显示
#include <QProgressBar>     // 进度条
#include <QLCDNumber>       // LCD数字显示
#include <QDoubleSpinBox>   // 浮点数输入框
#include <QSpinBox>         // 整数输入框
#include <QTabWidget>       // 标签页
#include <QGroupBox>        // 分组框
#include <QGridLayout>      // 网格布局
#include <QVBoxLayout>      // 垂直布局
#include <QHBoxLayout>      // 水平布局
#include <QLineEdit>        // 单行文本输入
#include <QGraphicsView>    // 图形视图
#include <QGraphicsScene>   // 图形场景
#include <QSettings>
#include <QTemporaryDir>
/**
 * @class MainWindow
 * @brief 主窗口类 - Binder Jetting 3D打印机控制软件的主界面
 *
 * 负责：
 * 1. 创建和管理所有UI控件
 * 2. 处理用户交互事件
 * 3. 协调USB通信、负压控制、参数管理等模块
 * 4. 维护打印状态机
 * 5. 实时更新界面状态（温度、负压、进度等）
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT    // Qt元对象宏，启用信号槽机制

public:
    /**
     * @brief 构造函数
     * @param parent 父窗口指针（默认为nullptr）
     *
     * 初始化流程：
     * 1. 创建USB管理器、负压控制器、参数保存对象
     * 2. 加载保存的参数
     * 3. 创建UI界面
     * 4. 连接信号槽
     * 5. 启动状态更新定时器
     */
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
protected:
    /**
     * @brief 重写窗口大小变化事件
     * @param event 大小变化事件
     *
     * 当窗口大小变化时重新绘制压力曲线
     */
    void resizeEvent(QResizeEvent *event) override;
private slots:
    // ========== 硬件控制 ==========
    /**
     * @brief 电机电源切换槽函数
     * @param checked true=上电, false=断电
     *
     * 功能：
     * 1. 更新电机电源状态
     * 2. 改变按钮样式和文字
     * 3. 发送相应命令到驱动板
     */
    void on_btnMotorPower_toggled(bool checked);   // 电机上电/断电
    /**
     * @brief 板卡电源切换槽函数
     * @param checked true=上电, false=断电
     *
     * 功能：
     * 1. 更新板卡电源状态
     * 2. 改变按钮样式和文字
     * 3. 发送相应命令到驱动板
     */
    void on_btnBoardPower_toggled(bool checked);   // 板卡上电/断电
    /**
     * @brief 系统复位槽函数
     *
     * 执行系统复位操作：
     * 1. 发送复位命令
     * 2. 重置电机和板卡电源状态
     * 3. 恢复运动参数到默认值
     */
    void on_btnReset_clicked();
    // ========== USB连接 ==========
    /**
     * @brief 连接设备槽函数
     *
     * 调用UsbManager连接FX3 USB设备
     */
    //void on_btnConnect_clicked();
    /**
     * @brief 断开设备槽函数
     *
     * 调用UsbManager断开当前连接的设备
     */
    //void on_btnDisconnect_clicked();
    /**
     * @brief 刷新设备列表槽函数
     *
     * 扫描并显示可用的FX3 USB设备
     */
    //void on_btnRefresh_clicked();
    /**
     * @brief USB状态变化处理
     * @param connected true=已连接, false=已断开
     *
     * 更新UI中的USB状态显示和按钮可用状态
     */
    void onUsbStatusChanged(bool connected);
    /**
     * @brief USB数据接收处理
     * @param data 接收到的数据
     *
     * 解析并处理从FX3设备收到的数据
     */
    void onUsbDataReceived(const QByteArray &data);
    /**
     * @brief USB错误处理
     * @param error 错误信息
     *
     * 在日志中显示USB错误信息
     */
    void onUsbError(const QString &error);
    /**
     * @brief USB通信测试完成处理
     * @param success true=测试通过, false=测试失败
     *
     * 更新测试按钮状态并显示测试结果
     */
    //void onUsbTestFinished(bool success);
    /**
     * @brief 负压控制器连接状态变化
     * @param connected true=已连接, false=已断开
     */
    void onVacuumConnectionChanged(bool connected);
    /**
     * @brief 负压控制器数据帧接收
     * @param cmd 命令码
     * @param state 状态码
     * @param sdata 短整型数据
     * @param fdata 浮点型数据（压力值）
     *
     * 解析负压控制器返回的数据，更新UI显示
     */
    void onVacuumFrameReceived(qint16 cmd, qint16 state, qint16 sdata, float fdata);
    /**
     * @brief 负压控制器错误处理
     * @param error 错误信息
     */
    void onVacuumError(const QString &error);
    // 一级负压
    void on_btnSetPrimaryPressure_clicked();
    // 二级负压
    void on_btnSetSecondaryPressure_clicked();
    // 循环负压
    void on_btnSetCyclicPressure_clicked();
    // 二级压墨
    void on_btnSecondaryInkPress_clicked();     // 二级压墨
    void on_btnNegativeReset_clicked();         // 复位
    void on_btnSetInkSupplySpeed_clicked();     // 设置供墨泵转速
    void on_btnSetReturnPumpSpeed_clicked();    // 设置回墨泵转速
    void on_btnWasteDischarge_toggled(bool checked);   // 排废液
    // ========== 操控界面 ==========
    /**
     * @brief 开始打印槽函数
     *
     * 前提条件：
     * 1. USB设备已连接
     * 2. 当前状态为IDLE或STOPPED
     *
     * 执行操作：
     * 1. 发送开始打印命令
     * 2. 更新状态为PRINTING
     * 3. 重置进度
     */
    void on_btnStartPrint_clicked();
    /**
     * @brief 暂停/继续打印槽函数
     *
     * 当前状态为PRINTING时：暂停打印
     * 当前状态为PAUSED时：继续打印
     */
    void on_btnPausePrint_clicked();
    /**
     * @brief 停止打印槽函数
     *
     * 停止当前打印任务，状态变为STOPPED
     */
    void on_btnStopPrint_clicked();
    /**
     * @brief 扫描设备槽函数
     *
     * 向设备发送状态扫描命令
     */
    void on_btnScan_clicked();
    /**
     * @brief 清空日志槽函数
     *
     * 清空日志显示区域
     */
    void on_btnClearLog_clicked();
    /**
     * @brief 紧急停止槽函数
     *
     * 立即停止所有操作，状态变为ERROR
     * 发送紧急停止命令到设备
     */
    void on_btnEmergencyStop_clicked();
    void on_btnOpenCli_clicked();
    void on_btnPreviousLayer_clicked();
    void on_btnNextLayer_clicked();
    void onCliConversionFinished(int status, int layerCount, int width,
                                 int height, int rowBytes, int bpp);

    // 应用打印参数
    void on_btnApplyPrintParams_clicked();
    // 位深开关：1bit/pixel（单色）<-> 2bit/pixel（灰度）
    void onPixelFormatChanged(int index);
    // ========== 波形控制 ==========
    /**
     * @brief 应用波形参数槽函数
     *
     * 1. 从界面读取所有波形参数
     * 2. 保存到配置文件
     * 3. 更新波形预览
     * 4. 发送波形参数到设备
     */
    void on_btnApplyWaveform_clicked();
    /**
     * @brief 加载波形文件槽函数
     *
     * 打开文件对话框，选择并加载波形配置文件
     */
    void on_btnLoadWaveform_clicked();
    /**
     * @brief 保存波形配置槽函数
     *
     * 打开保存对话框，将当前波形参数保存为文件
     */
    void on_btnSaveWaveform_clicked();
    /**
     * @brief 执行喷头测试槽函数
     *
     * 根据选择的测试模式执行喷头喷射测试
     */
    void on_btnTestJet_clicked();

    // ========== 运动参数 ==========
    /**
     * @brief 应用运动参数槽函数
     *
     * 1. 从界面读取所有运动参数
     * 2. 保存到配置文件
     * 3. 发送参数到运动控制单元
     */
    void on_btnSetMotionParams_clicked();
    /**
     * @brief 全部归零槽函数
     *
     * 将所有轴移动到原点位置
     */
    void on_btnHomeAll_clicked();
    /**
     * @brief 移动到指定位置槽函数
     *
     * 1. 从界面读取目标坐标（X, Y, Z）
     * 2. 发送移动命令到设备
     * 3. 更新当前位置显示
     */
    void on_btnMoveTo_clicked();
    /**
     * @brief 设置原点槽函数
     *
     * 将当前位置设置为原点
     */
    void on_btnSetOrigin_clicked();

    // ========== 定时器更新 ==========
    /**
     * @brief 定时更新状态
     *
     * 每秒执行一次：
     * 1. 模拟温度变化
     * 2. 模拟负压变化（离线模式）
     * 3. 模拟打印进度
     * 4. 更新UI显示
     */
//    void updateStatus();

private:
    // ========== USB管理 ==========
    // ==================== 核心业务对象 ====================
    /**
     * @brief USB管理器
     *
     * 负责USB设备的连接、断开、数据收发
     * 内部包含工作线程，不阻塞UI
     */
    UsbManager *usbManager;
    /**
     * @brief 负压控制器
     *
     * 通过TCP与负压设备通信
     * 支持自动重连
     */

    VacuumController *vacuumController;
    // ========== 参数保存 ==========
    /**
     * @brief 参数保存对象
     *
     * 使用QSettings保存/加载各种参数
     */
    SaveParams *saveParams;

    // ========== 打印状态 ==========
    /**
     * @brief 打印状态枚举
     *
     * IDLE     - 空闲状态，等待开始
     * PRINTING - 正在打印
     * PAUSED   - 已暂停
     * STOPPED  - 已停止
     * ERROR    - 错误状态
     */
    enum PrintState {
        IDLE,
        PRINTING,
        PAUSED,
        STOPPED,
        ERROR
    };
    /** @brief 当前打印状态 */
    PrintState currentState;

    // ========== 状态定时器 ==========
    /**
     * @brief 状态更新定时器
     *
     * 间隔：1秒
     * 触发：updateStatus()
     */
    QTimer *statusTimer;


    // ========== 参数结构体（使用 saveparams.h 中定义的） ==========
    /** @brief 当前打印参数（喷头个数、DPI、XY速度、左右值、重叠孔） */
    PrintParams currentPrintParams;
    /** @brief 当前运动参数（速度、加速度、层厚等） */
    MotionParams currentMotionParams;
    /** @brief 当前波形参数（电压、时间参数、频率等） */
    WaveformData currentWaveform;
    /** @brief 当前负压参数（目标值、启用状态） */
    NegativePressureParams currentNegativeParams;
    // ========== 负压参数 ==========   注意这种声明加定义
    // 一级负压
    QDoubleSpinBox *spinPrimaryTarget;
    QLCDNumber *lcdPrimaryCurrent;
    // 二级负压
    QDoubleSpinBox *spinSecondaryTarget;
    QLCDNumber *lcdSecondaryCurrent;
    // 循环负压
    QDoubleSpinBox *spinCyclicTarget;
    QLCDNumber *lcdCyclicCurrent;
    // 二级压墨
    QSpinBox *spinSecondaryInkTime;             // 压墨时间
    QSpinBox *spinSecondaryReleaseTime;         // 释放时间
    // 供墨/回墨泵转速 + 排废液
    QLabel *labelInkSupplySpeed;            // 供墨泵实时转速显示
    QLabel *labelReturnPumpSpeed;           // 回墨泵实时转速显示
    QSpinBox *spinInkSupplySpeedSet;        // 供墨泵转速设置
    QSpinBox *spinReturnPumpSpeedSet;       // 回墨泵转速设置
    QPushButton *btnWasteDischarge;         // 排废液按钮
//    struct NegativePressureParams {
//        double pressure;
//        bool enabled;
//    } currentNegativePressure;

    // ========== 模拟数据 ==========
    /** @brief 模拟温度值 */
    double simulatedTemp;

    /** @brief 模拟打印进度 (0-100) */
    int simulatedProgress;

    // ===== 负压数据存储（当前值） =====
    double m_primaryCurrent;    // 一级负压当前值
    double m_secondaryCurrent;  // 二级负压当前值
    double m_cyclicCurrent;     // 循环负压当前值

    // ===== 压力曲线数据 =====
    QList<double> m_primaryHistory;    // 一级负压历史数据
    QList<double> m_secondaryHistory;  // 二级负压历史数据
    QList<double> m_cyclicHistory;     // 循环负压历史数据
    static const int MAX_HISTORY = 100; // 最大历史点数
    bool m_firstDraw;   // ← 添加：首次绘制标志
    QString m_sliceDir; // 数据处理目录
    // ========== UI控件指针 ==========
    // 连接栏
    /** @brief USB状态显示标签 */ // 后续更改到整个系统是否满足打印条件
    QLabel *labelUsbStatus;
    /** @brief 电机电源开关按钮 */
    QPushButton *btnMotorPower;    // 电机上电按钮
    /** @brief 板卡电源开关按钮 */
    QPushButton *btnBoardPower;    // 板卡上电按钮
    /** @brief 系统复位按钮 */
    QPushButton *btnReset;         // 复位按钮
//    QComboBox *comboDevice;
    /** @brief 刷新设备列表按钮 */
//    QPushButton *btnRefresh;
//    /** @brief 连接设备按钮 */
//    QPushButton *btnConnect;
//    /** @brief 断开设备按钮 */
//    QPushButton *btnDisconnect;
//    /** @brief USB通信测试按钮 */
//    QPushButton *btnUsbTest;
//    QLabel *labelUsbStatus;

    // 主Tab
    /** @brief 主标签页控件 */
    QTabWidget *tabWidget;

    // ===== Tab1: 操控界面 =====
    QPushButton *btnStartPrint;     /** @brief 开始打印按钮 */
    QPushButton *btnPausePrint;     /** @brief 暂停/继续打印按钮 */
    QPushButton *btnStopPrint;      /** @brief 停止打印按钮 */
    QPushButton *btnScan;           /** @brief 扫描设备按钮 */
    QPushButton *btnEmergencyStop;  /** @brief 紧急停止按钮 */
    QPushButton *btnClearLog;       /** @brief 清空日志按钮 */
    QPushButton *btnOpenCli;        /** @brief 打开CLI文件按钮 */
    QPushButton *btnPreviousLayer;  /** @brief 上一层预览按钮 */
    QPushButton *btnNextLayer;      /** @brief 下一层预览按钮 */
    QLabel *labelPrintState;        /** @brief 打印状态标签 */
    QLabel *labelPrintStateText;    /** @brief 打印状态文本（空闲/打印中/已暂停等） */
    QProgressBar *progressPrint;    /** @brief 打印进度条 */
    QLabel *labelProgress;          /** @brief 进度百分比标签 */
    QLabel *labelLayerInfo;         /** @brief 层数信息标签 */
    QLabel *labelCliFile;           /** @brief 当前CLI文件信息 */
    QGraphicsView *printPreview;    /** @brief 打印预览视图 */
    QGraphicsScene *printScene;     /** @brief 打印预览场景 */
    QTextEdit *textLog;             /** @brief 日志输出文本框 */

    QTemporaryDir *cliOutputDir;
    QString currentCliFile;
    int currentPreviewLayer;
    int previewLayerCount;
    int previewWidth;
    int previewHeight;
    int previewBpp;          // 当前切片缓存的每像素位数（1 或 2），必须与 meta.txt 一致
    QThread *cliThread;
    CliFillWorker *cliWorker;

    // ===== Tab2: 负压设置 =====
    QDoubleSpinBox *spinNegativePressure;   /** @brief 目标负压值输入框 */
    QPushButton *btnSetNegativePressure;    /** @brief 设置负压值按钮 */
    QPushButton *btnNegativePressureOn;     /** @brief 开启负压按钮 */
    QPushButton *btnNegativePressureOff;    /** @brief 关闭负压按钮 */
    QPushButton *btnNegativePressureReset;  /** @brief 负压复位按钮 */
//    QLCDNumber *lcdNegativePressure;        /** @brief 当前负压LCD显示 */
    QLabel *labelNegativeStatus;            /** @brief 负压状态标签 */
    QGraphicsView *pressureChart;           /** @brief 压力曲线图表视图 */
    QGraphicsScene *pressureScene;          /** @brief 压力曲线图表场景 */

    // ===== Tab: 打印参数 =====
    QSpinBox *spinHeadCount;          /** @brief 喷头个数 (1~8) */
    QSpinBox *spinDpi;                /** @brief DPI (200~1600) */
    QPushButton *btnApplyPrintParams; /** @brief 应用打印参数按钮 */
    QLabel *labelLRNone;              /** @brief 单喷头提示 */
    QGridLayout *printParamGrid;      /** @brief 左右值/重叠孔动态行布局 */
    QList<QSpinBox*> spinLeftRight;   /** @brief 左右值输入框（N-1 个） */
    QList<QSpinBox*> spinOverlap;     /** @brief 重叠孔输入框（N-1 个） */
    QList<QLabel*> m_lrLabels;        /** @brief 接缝行标签（N-1 个） */
    QSpinBox *spinPassCount;              /** @brief pass 数 */
    QComboBox *comboPixelFormat;          /** @brief 位深开关：1bit/pixel 或 2bit/pixel */
    QLabel *labelPixelFormatHint;         /** @brief 位深说明（每行字节数/发送处理方式） */
    QGridLayout *bidirectionalGrid;       /** @brief 双向值动态行布局 */
    QLabel *labelBINone;                  /** @brief 单 pass 提示 */
    QList<QSpinBox*> spinBidirectional;   /** @brief 双向值输入框（pass数-1 个） */
    QList<QLabel*> m_biLabels;            /** @brief 双向值行标签 */
    // ===== Tab3: 波形控制 =====
    QDoubleSpinBox *spinVoltage;            /** @brief 驱动电压输入框 (V) */
    QDoubleSpinBox *spinRiseTime;           /** @brief 上升时间输入框 (μs) */
    QDoubleSpinBox *spinDwellTime;          /** @brief 维持时间输入框 (μs) */
    QDoubleSpinBox *spinFallTime;           /** @brief 下降时间输入框 (μs) */
    QDoubleSpinBox *spinFrequency;          /** @brief 频率输入框 (kHz) */
    QPushButton *btnApplyWaveform;          /** @brief 应用波形按钮 */
    QPushButton *btnLoadWaveform;           /** @brief 加载波形文件按钮 */
    QPushButton *btnSaveWaveform;           /** @brief 保存波形配置按钮 */
    QComboBox *comboTestPattern;            /** @brief 测试模式选择下拉框 */
    QPushButton *btnTestJet;                /** @brief 执行测试按钮 */
    QLCDNumber *lcdJetTemp;                 /** @brief 喷头温度LCD显示 */
    QLabel *labelWaveformStatus;            /** @brief 波形状态标签 */
    QGraphicsView *waveformPreview;         /** @brief 波形预览视图 */
    QGraphicsScene *waveformScene;          /** @brief 波形预览场景 */

    // ===== Tab4: 运动参数 =====
    QDoubleSpinBox *spinSpeedX;             /** @brief XYZ轴速度输入框 (mm/s) */
    QDoubleSpinBox *spinSpeedY;
    QDoubleSpinBox *spinSpeedZ;
    QDoubleSpinBox *spinAcceleration;       /** @brief 加速度输入框 (mm/s²) */
    QDoubleSpinBox *spinJerk;               /** @brief Jerk输入框 (mm/s³) */
    QDoubleSpinBox *spinLayerHeight;        /** @brief 层厚输入框 (mm) */
    QPushButton *btnSetMotionParams;        /** @brief 应用运动参数按钮 */
    QDoubleSpinBox *spinPosX;               /** @brief XYZ轴目标位置输入框 (mm) */
    QDoubleSpinBox *spinPosY;
    QDoubleSpinBox *spinPosZ;
    QPushButton *btnMoveTo;                 /** @brief 移动到指定位置按钮 */
    QPushButton *btnHomeAll;                /** @brief 全部归零按钮 */
    QPushButton *btnSetOrigin;              /** @brief 设置原点按钮 */
    QLabel *labelCurrentPos;                /** @brief 当前位置显示标签 */

    // ===== 状态栏 =====
    QLCDNumber *lcdTemperature;             /** @brief 温度LCD显示 */
    // ========== 状态变量 ==========
    bool motorPowered;   // 电机是否已上电 /** @brief 电机是否已上电 */
    bool boardPowered;   // 板卡是否已上电 /** @brief 板卡是否已上电 */

    // ========== 日志文件 ==========
    QFile *m_logFile;
    QTextStream *m_logStream;
    QString m_logFilePath;
    // ========== 日志轮转 ==========
    QTimer *m_logRotateTimer;     // 每小时检查一次是否需要切分
    QString m_currentLogHour;      // 当前日志文件所属的小时（yyyyMMdd_hh）
    // ========== FX3 初始化序列 ==========
    enum FpgaInitStep {
        FPGA_INIT_IDLE = 0,      // 空闲
        FPGA_INIT_SEND_CMD1,     // 发命令1：喷头个数
        FPGA_INIT_WAIT_ACK1,     // 等命令1 ACK
        FPGA_INIT_SEND_CMD2,     // 发命令2：电压温度
        FPGA_INIT_WAIT_ACK2,     // 等命令2 ACK
        FPGA_INIT_SEND_CMD3,     // 发命令3：零点定位
        FPGA_INIT_WAIT_ACK3,     // 等命令3 ACK
        FPGA_INIT_SEND_CMD4,     // 4: DPI设置 STPI
        FPGA_INIT_WAIT_ACK4,
        FPGA_INIT_DONE           // 初始化完成
    };
    FpgaInitStep m_fpgaInitStep;
    QTimer *m_fpgaInitTimer;     // 上电后延迟启动初始化

    // ========== 打印数据发送状态机 ==========
    enum PrintDataStep {
        PRINT_IDLE = 0,
        PRINT_SEND_HEADER,      // 发首帧命令
        PRINT_WAIT_HEADER_ACK,  // 等首帧ACK
        PRINT_SEND_DATA,        // 发位图数据块
        PRINT_WAIT_DATA_ACK,    // 等数据发送完成ACK
        PRINT_DONE             // 本层完成
    };
    PrintDataStep m_printStep;
    QByteArray m_currentLayerData;   // 当前层的位图数据
    int m_currentLayerNum;           // 当前第几层
    int m_currentPassPixels;         // 当前发送条带的列数（用于 FIRE 1/2PASS 起止脉冲计算）
    // ========== 打印状态查询 ==========
    QTimer *m_printQueryTimer;     // 每200ms查询一次设备状态
    bool m_queryEnabled;           // 是否启用查询
    // ===== 查询状态 =====
    bool m_firstFrameReady;        // 第二值：第一帧接收完成
    bool m_canFire;                // 第三值：可以发点火指令
    bool m_printFinished;          // 第四值：打印完成
    bool m_waitFireAck;   // 正在等点火指令的回复



    // ========== UI函数 ==========
    /**
     * @brief 设置UI
     *
     * 创建所有UI控件并布局
     */
    void setupUI();
    /**
     * @brief 创建连接栏
     * @return 水平布局指针
     *
     * 包含：USB状态、设备选择、连接/断开按钮、电源控制等
     */
    QHBoxLayout* createConnectionBar();
    //void createConnectionBar();
    /**
     * @brief 创建主标签页
     *
     * 包含4个标签页：
     * 1. 操控界面
     * 2. 负压设置
     * 3. 波形控制
     * 4. 运动参数
     */
    void createMainTab();
    /**
     * @brief 创建操控界面标签页
     *
     * 包含：打印控制按钮、任务信息、打印预览、日志输出
     */
    void createControlTab();
    /**
     * @brief 创建负压设置标签页
     *
     * 包含：目标负压设置、当前负压显示、控制按钮、压力曲线
     */
    void createNegativePressureTab();
    // 创建打印参数标签页
    void createPrintParamsTab();
    void rebuildPrintParamRows();     // 根据喷头个数重建左右值/重叠孔行
    void rebuildBidirectionalRows();  // ← 新增：根据 pass 数重建双向值行
    /**
     * @brief 创建波形控制标签页
     *
     * 包含：波形参数设置、加载/保存、喷头测试、波形预览
     */
    void createWaveformTab();
    /**
     * @brief 创建运动参数标签页
     *
     * 包含：运动参数设置、手动控制、当前位置显示
     */
    void createMotionTab();
    /**
     * @brief 创建状态栏
     *
     * 显示：温度、负压、进度等信息
     */
    void createStatusBar();
    /**
     * @brief 更新打印状态
     * @param state 新的打印状态
     *
     * 更新：
     * 1. currentState变量
     * 2. 控制按钮的启用/禁用状态
     * 3. 状态标签的文字和颜色
     */
    void updatePrintState(PrintState state);
    /**
     * @brief 追加日志
     * @param message 日志消息
     * @param color 颜色（默认黑色）
     *
     * 格式：[时间戳] 消息
     */
    //void appendLog(const QString &message, const QString &color = "black");
    // ========== 日志输出目标 ==========
    enum LogTarget {
        LOG_BOTH = 0,      // 界面+文件（默认）
        LOG_UI_ONLY,       // 仅界面
        LOG_FILE_ONLY      // 仅文件
    };

    void appendLog(const QString &message, const QString &color = "black",
                   LogTarget target = LOG_BOTH);
    void checkLogRotate();        // 检查是否需要切分日志
    void cleanupOldLogs();         // 清理旧日志文件

    /**
     * @brief 更新打印预览
     * @param layer 当前层数
     *
     * 根据当前打印数据更新预览视图（TODO）
     */
    void updatePrintPreview(int layer);
    /**
     * @brief 更新USB状态
     * @param connected 是否已连接
     *
     * 更新按钮状态和状态标签
     */
    void updateUsbStatus(bool connected);
    /**
     * @brief 绘制波形预览
     *
     * 根据当前波形参数在预览区域绘制波形图
     */
    void drawWaveformPreview();
    /**
     * @brief 绘制压力曲线
     *
     * 在压力图表区域绘制实时压力曲线
     */
    void drawPressureChart();

    // ========== 数据持久化 ==========
    void loadMotionParams();      // 加载运动参数
    void saveMotionParams();      // 保存运动参数
    void loadWaveformParams();    // 加载波形参数（预留）
    void saveWaveformParams();    // 保存波形参数（预留）
    void loadNegativePressure();  // 加载负压参数（预留）
    void saveNegativePressure();  // 保存负压参数（预留




    // ========== 函数声明 ==========
    void startFpgaInit();                // 启动FX3初始化序列
    void sendNextFpgaInitCommand();      // 发送下一条初始化命令
    QByteArray buildFpgaCmd(const char cmd[4], uint32_t p1, uint32_t p2,
                            uint32_t p3, uint32_t p4, uint32_t p5);  // 构造32字节命令帧
    // ========== 辅助函数 ==========
    /**
     * @brief 创建分组框
     * @param title 分组框标题
     * @return 分组框指针
     *
     * 统一的分组框样式
     */
    QGroupBox* createGroupBox(const QString &title);
    /**
     * @brief 添加控件到布局
     * @param layout 目标布局
     * @param widget 要添加的控件
     * @param row 行号
     * @param col 列号
     * @param rowSpan 行跨度
     * @param colSpan 列跨度
     *
     * 方便添加控件到网格布局（当前未完全实现）
     */
    void addWidgetToLayout(QLayout *layout, QWidget *widget, int row, int col, int rowSpan = 1, int colSpan = 1);
    // 辅助函数
    void updateNegativeUI();
    void sendNegativeCommand(int channel, bool enable, double target);

    void initLogFile();   // 初始化日志文件
    void writeToLogFile(const QString &msg);  // 写一行到日志文件
    // 发送某层的首帧命令
    void sendLayerHeader(int layerNum, int dataSize);
    void sendQueryCmd();           // 发送查询指令
    void parseQueryResponse(const QByteArray &data);  // 解析查询回复
    void sendFireCmd();   // 发送点火指令（预留）
    void applySliceResult(int layerCount, int width, int height, int rowBytes, int bpp);   // 统一恢复切片预览界面
    void openCliFile(const QString &fileName);   // 打开并切片CLI文件（按钮和自动加载共用），会保存路径到配置

public slots:
    void sendLayerBitmap(const QByteArray &bitmapData);   // 发送一层位图（分块4MB）
};

#endif // MAINWINDOW_H
