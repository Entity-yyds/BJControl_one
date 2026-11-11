#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QTextStream>
#include <QDir>
#include <QMenu>
#include <QAction>
#include <QDesktopServices>
#include <QMessageBox>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QImage>
#include <QStandardPaths>
#include <QDateTime>
#include <QDebug>
#include <QRandomGenerator>
#include <QPen>
#include <QBrush>
#include <QPainter>
#include <QPainterPath>
#include <QSpacerItem>
#include <QStatusBar>
#include <cmath>
#include <QGraphicsTextItem>   // ← 添加这个头文件
#include <QGraphicsView>       // ← 如果还没有的话也加上
#include <QProcess>
#include <QVector>
#include "binrowoffset.h"


extern "C" {
#include "cli_fill2.h"
}
//MainWindow::MainWindow(QWidget *parent)
//    : QMainWindow(parent)
//    , ui(new Ui::MainWindow)
//{
//    ui->setupUi(this);
//}

//MainWindow::~MainWindow()
//{
//    delete ui;
//}

#include <QPen>
#include <QBrush>
#include <QPainter>
#include <QSpacerItem>
/**
 * @brief MainWindow 构造函数
 *
 * 初始化顺序：
 * 1. 调用父类构造函数
 * 2. 创建核心业务对象（UsbManager, VacuumController, SaveParams）
 * 3. 初始化状态变量（打印状态、电源状态、模拟数据）
 * 4. 连接信号槽
 * 5. 加载保存的参数
 * 6. 创建UI
 * 7. 启动状态更新定时器
 *
 * @param parent 父窗口指针
 */
MainWindow::MainWindow(QWidget *parent) // 初始化列表优点：在初始化列表中，C++ 会按声明顺序执行初始化
    : QMainWindow(parent)               // 先调用基类 QMainWindow 构造函数，完成窗口基础初始化
    , usbManager(new UsbManager(this))  // UsbManager 会被添加到 Qt 对象树中，当 MainWindow 销毁时，UsbManager 会自动被删除，无需手动 delete
    , vacuumController(new VacuumController(this))      // 负压系统链接
    , saveParams(new SaveParams(this))
    , currentState(IDLE)            // 将打印状态初始化为"空闲"，表示打印机刚启动，还没有开始任何任务
    , simulatedTemp(25.0)           // 模拟温度初始化为 25°C（室温），后续会通过 updateStatus() 定时器模拟变化
    , simulatedProgress(0)          // 打印进度初始化为 0%，表示还没有开始打印
    , m_primaryCurrent(0.0)         // ← 新增 一级负压当前值：0.0 kPa
    , m_secondaryCurrent(0.0)       // ← 新增 二级负压当前值：0.0 kPa
    , m_cyclicCurrent(0.0)          // ← 新增 循环负压当前值：0.0 kPa
    , m_firstDraw(true)             // ← 添加：首次绘制标志
    , cliOutputDir(nullptr)         // CLI切片输出临时目录指针
    , currentPreviewLayer(0)        // 当前预览层号：0=无预览   有效范围 1 ~ previewLayerCount
    , previewLayerCount(0)          // 总层数：0=尚未加载任何CLI文件
    , previewWidth(0)               // 切片画布宽度（像素）：0=未加载 由 cli_fill2_convert() 通过 CliFillResult 返回
    , previewHeight(0)              // 切片画布高度（像素）：0=未加载
    , previewBpp(2)                 // 当前切片缓存的每像素位数：默认 2，加载缓存时以 meta.txt 为准
    , cliThread(nullptr)            // CLI切片线程指针
    , cliWorker(nullptr)            // CLI切片工作对象
    , motorPowered(false)    // ← 新增
    , boardPowered(false)    // ← 新增
    , m_fpgaInitStep(FPGA_INIT_IDLE)
    , m_fpgaInitTimer(new QTimer(this))
    , m_printStep(PRINT_IDLE)
    , m_currentPassPixels(0)
    , m_queryEnabled(false)
    , m_firstFrameReady(false)
    , m_canFire(false)
    , m_printFinished(false)
    , m_waitFireAck(false)
{
    connect(this, &QObject::destroyed, this, [this]() {
        qDebug() << "🔥 MainWindow 自己被销毁了！" ;
        // 此时 settings 可能还存在，但马上会被删除
        // settings = nullptr; // 如果在这里设置，外面还能看到吗？
    }, Qt::DirectConnection);
    // 设置窗口属性
    setWindowTitle("Binder Jetting 3D Printer Controller");
    resize(1400, 900);

    // ====== 加载保存的参数 ======
    currentMotionParams = saveParams->loadMotionParams();
    currentWaveform = saveParams->loadWaveformParams();
    currentNegativeParams = saveParams->loadNegativePressure();
    currentPrintParams = saveParams->loadPrintParams();

    // 创建UI
    setupUI();
    // ====== 初始化日志文件 ======
    initLogFile();
    // FX3初始化定时器：板卡上电后延迟500ms再发命令，等FX3稳定
    m_fpgaInitTimer->setSingleShot(true);
    m_fpgaInitTimer->setInterval(500);
    connect(m_fpgaInitTimer, &QTimer::timeout, this, &MainWindow::startFpgaInit);       // 500ms 定时器到了之后
    // ====== 初始化USB管理 ======   // USB状态变化 → 更新UI
    connect(usbManager, &UsbManager::statusChanged, // USB插入/拔出 → usbManager 发出 statusChanged(true/false)
            this, &MainWindow::onUsbStatusChanged); // → 自动调用 onUsbStatusChanged(bool connected)
    // USB数据接收 → 解析处理 // → 解析数据并更新界面
    connect(usbManager, &UsbManager::dataReceived,  // USB收到数据 → usbManager 发出 dataReceived(数据)
            this, &MainWindow::onUsbDataReceived);  // → 自动调用 onUsbDataReceived(QByteArray data)
    // USB错误 → 显示日志
    connect(usbManager, &UsbManager::errorOccurred,
            this, &MainWindow::onUsbError);         // USB发生错误 → usbManager 发出 errorOccurred("错误信息")
                                                    // → 自动调用 onUsbError(QString error)
                                                    // → 在日志中显示错误信息
//    // USB测试完成 → 更新按钮状态
//    connect(usbManager, &UsbManager::testFinished,
//            this, &MainWindow::onUsbTestFinished);
    // 负压控制器的信号
    connect(vacuumController, &VacuumController::connectionChanged,
            this, &MainWindow::onVacuumConnectionChanged);
    connect(vacuumController, &VacuumController::frameReceived,
            this, &MainWindow::onVacuumFrameReceived);
    connect(vacuumController, &VacuumController::errorOccurred,
            this, &MainWindow::onVacuumError);
    // 打印状态查询定时器
    m_printQueryTimer = new QTimer(this);
    m_printQueryTimer->setInterval(200);  // 200ms
    connect(m_printQueryTimer, &QTimer::timeout, this, &MainWindow::sendQueryCmd);
    // 自动连接负压设备
    vacuumController->connectDevice();
    // ====== 初始化状态定时器 ======
    statusTimer = new QTimer(this);
    statusTimer->setInterval(1000);             // 每秒更新一次
    // 修改定时器连接
    connect(statusTimer, &QTimer::timeout, this, [this]() {
                                    // 更新负压UI
                                    updateNegativeUI();
                                });

    statusTimer->start();                       // 定时器启动 → 每隔1秒发出 timeout 信号

    // ====== 初始化UI状态 ======
    updatePrintState(IDLE);
    updateUsbStatus(false);

    // ====== 刷新设备列表 ======
    appendLog("程序已启动，等待连接设备...", "blue");
    appendLog("星光喷头 600DPI 控制就绪", "green");
    appendLog(QString("已加载运动参数: X=%1, Y=%2, Z=%3, 层厚=%4mm")
              .arg(currentMotionParams.speedX)
              .arg(currentMotionParams.speedY)
              .arg(currentMotionParams.speedZ)
              .arg(currentMotionParams.layerHeight), "blue");
    appendLog(QString("配置文件路径: %1").arg(saveParams->getConfigFilePath()), "blue");
    // ====== 自动打开上次的CLI文件 ======
    const QString lastCliFile = saveParams->loadLastCliFile();
    if (!lastCliFile.isEmpty() && QFileInfo(lastCliFile).exists()) {
        appendLog("检测到上次打开的切片文件，自动打开...", "blue");
        openCliFile(lastCliFile);
    } else if (!lastCliFile.isEmpty()) {
        appendLog(QString("上次的切片文件不存在（已删除?），跳过自动打开: %1")
                  .arg(lastCliFile), "orange");
    }
}
/**
 * @brief MainWindow 析构函数
 *
 * Qt会自动删除添加到对象树的子对象
 * 不需要手动delete
 */
MainWindow::~MainWindow()
{
    qDebug() << "=== MainWindow 析构函数开始 ===";

    // 停止定时器
    if (statusTimer) {
        statusTimer->stop();
        delete statusTimer;
        statusTimer = nullptr;
    }

    // 断开USB连接（如果已连接）
    if (usbManager) {
        usbManager->disconnectDevice();
        // 不需要 delete，Qt 会自动删除子对象
    }

    // 断开负压连接
    if (vacuumController) {
        vacuumController->disconnectDevice();
    }

    if (cliThread) {
        cliThread->quit();
        cliThread->wait();
        delete cliWorker;
        cliWorker = nullptr;
        delete cliThread;
        cliThread = nullptr;
    }

    qDebug() << "=== MainWindow 析构完成 ===";
}

// ==================== UI创建 ====================
/**
 * @brief 设置主UI布局
 *
 * 结构：
 * ┌─────────────────────────────────────────────┐
 * │  连接栏 (水平布局)                          │
 * │  [状态] [刷新] [连接] [断开] [测试] [电源]  │
 * ├─────────────────────────────────────────────┤
 * │  主标签页 (QTabWidget)                     │
 * │  ┌──────┬──────┬──────┬──────────┐        │
 * │  │操控界面│负压设置│波形控制│运动参数│        │
 * │  └──────┴──────┴──────┴──────────┘        │
 * ├─────────────────────────────────────────────┤
 * │  状态栏 (QStatusBar)                       │
 * │  温度: 25°C  负压: -50kPa  进度: 0%       │
 * └─────────────────────────────────────────────┘
 */
void MainWindow::setupUI()
{
    // 创建中央部件
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    // 主布局：垂直
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(5);
    mainLayout->setContentsMargins(5, 5, 5, 5);

    // 创建各个部分 // 连接栏（顶部）
    //createConnectionBar();
    QHBoxLayout *connectionLayout = createConnectionBar();
    mainLayout->addLayout(connectionLayout);
    //mainLayout->addLayout(static_cast<QLayout*>(findChild<QHBoxLayout*>("connectionLayout")));
    // 主标签页（中间）
    createMainTab();
    mainLayout->addWidget(tabWidget);
    // 状态栏（底部）
    createStatusBar();
}

/**
 * @brief 创建连接栏
 * @return 水平布局指针
 *
 * 布局（从左到右）：
 * [● 系统就绪] [刷新] [连接] [断开] [通信测试]
 * [⚡ 电机上电] [🔌 板卡上电] [🔄 复位] [⚠ 紧急停止]
 */
QHBoxLayout* MainWindow::createConnectionBar()
{
    qDebug() << "  -> createConnectionBar() 开始";
    QHBoxLayout *layout = new QHBoxLayout();
    layout->setSpacing(8);
    layout->setContentsMargins(5, 5, 5, 5);


//    // 按钮
//    btnRefresh = new QPushButton("刷新", this);
//    //btnRefresh->setFixedWidth(80);
//    btnRefresh->setFixedSize(80, 30);
//    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::on_btnRefresh_clicked);
//    layout->addWidget(btnRefresh);

//    btnConnect = new QPushButton("连接", this);
//    //btnConnect->setFixedWidth(80);
//    btnConnect->setFixedSize(80, 30);
//    btnConnect->setStyleSheet("background-color: #4CAF50; color: white;");
//    connect(btnConnect, &QPushButton::clicked, this, &MainWindow::on_btnConnect_clicked);
//    layout->addWidget(btnConnect);

//    btnDisconnect = new QPushButton("断开", this);
//    //btnDisconnect->setFixedWidth(80);
//    btnDisconnect->setFixedSize(80, 30);
//    btnDisconnect->setEnabled(false);
//    connect(btnDisconnect, &QPushButton::clicked, this, &MainWindow::on_btnDisconnect_clicked);
//    layout->addWidget(btnDisconnect);

//    btnUsbTest = new QPushButton("通信测试", this);
//    btnUsbTest->setFixedSize(90, 30);
//    btnUsbTest->setEnabled(false);
//    connect(btnUsbTest, &QPushButton::clicked, this, [this]() {
//        btnUsbTest->setEnabled(false);
//        appendLog("开始USB回环通信测试...", "blue");
//        usbManager->testCommunication();
//    });
//    layout->addWidget(btnUsbTest);

    // ===== 状态指示灯 =====
//    labelUsbStatus = new QLabel("● 系统就绪", this);
//    labelUsbStatus->setStyleSheet("color: green; font-weight: bold; font-size: 12px;");
//    labelUsbStatus->setMinimumWidth(100);
//    labelUsbStatus->setFixedHeight(30);
//    layout->addWidget(labelUsbStatus);
//    layout->addSpacing(20);
    labelUsbStatus = new QLabel("● 板卡未上电", this);
    labelUsbStatus->setStyleSheet("color: gray; font-weight: bold; font-size: 12px;");
    labelUsbStatus->setMinimumWidth(120);
    labelUsbStatus->setFixedHeight(30);
    layout->addWidget(labelUsbStatus);
    layout->addSpacing(20);
    // ===== 电机上电按钮 =====
    btnMotorPower = new QPushButton("⚡ 电机上电", this);
    btnMotorPower->setFixedSize(120, 35);
    btnMotorPower->setStyleSheet(
        "QPushButton {"
        "    background-color: #FF9800;"
        "    color: white;"
        "    font-weight: bold;"
        "    font-size: 12px;"
        "    border-radius: 4px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #F57C00;"
        "}"
        "QPushButton:checked {"
        "    background-color: #4CAF50;"
        "}"
    );
    btnMotorPower->setCheckable(true);  // 可切换状态：上电/断电
    connect(btnMotorPower, &QPushButton::toggled, this, &MainWindow::on_btnMotorPower_toggled);
    layout->addWidget(btnMotorPower);

    layout->addSpacing(5);

    // ===== 板卡上电按钮 =====
    btnBoardPower = new QPushButton("🔌 板卡上电", this);
    btnBoardPower->setFixedSize(120, 35);
    btnBoardPower->setStyleSheet(
        "QPushButton {"
        "    background-color: #2196F3;"
        "    color: white;"
        "    font-weight: bold;"
        "    font-size: 12px;"
        "    border-radius: 4px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #1976D2;"
        "}"
        "QPushButton:checked {"
        "    background-color: #4CAF50;"
        "}"
    );
    btnBoardPower->setCheckable(true);  // 可切换状态：上电/断电
    connect(btnBoardPower, &QPushButton::toggled, this, &MainWindow::on_btnBoardPower_toggled);
    layout->addWidget(btnBoardPower);

    layout->addSpacing(5);

    // ===== 复位按钮 =====
    btnReset = new QPushButton("🔄 复位", this);
    btnReset->setFixedSize(100, 35);
    btnReset->setStyleSheet(
        "QPushButton {"
        "    background-color: #9E9E9E;"
        "    color: white;"
        "    font-weight: bold;"
        "    font-size: 12px;"
        "    border-radius: 4px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #757575;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #616161;"
        "}"
    );
    connect(btnReset, &QPushButton::clicked, this, &MainWindow::on_btnReset_clicked);
    layout->addWidget(btnReset);



    // ===== 右侧：弹簧推到底 =====
    layout->addStretch();
    // 添加紧急停止按钮到连接栏右侧
    // ===== 最右侧：紧急停止按钮 =====
    QPushButton *emergencyBtn = new QPushButton("⚠ 紧急停止", this);
    emergencyBtn->setFixedSize(130, 40);
    emergencyBtn->setStyleSheet(
        "QPushButton {"
        "    background-color: #f44336;"
        "    color: white;"
        "    font-weight: bold;"
        "    font-size: 14px;"
        "    border-radius: 4px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #d32f2f;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #b71c1c;"
        "}"
    );
    connect(emergencyBtn, &QPushButton::clicked, this, &MainWindow::on_btnEmergencyStop_clicked);
    layout->addWidget(emergencyBtn);

    // ===== 三个点更多菜单按钮 =====
    QPushButton *btnMoreMenu = new QPushButton("⋮", this);
    btnMoreMenu->setFixedSize(40, 40);
    btnMoreMenu->setStyleSheet(
        "QPushButton {"
        "    background-color: transparent;"
        "    color: #666;"
        "    font-size: 20px;"
        "    font-weight: bold;"
        "    border: none;"
        "    border-radius: 20px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #e0e0e0;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #d0d0d0;"
        "}"
    );

    QMenu *moreMenu = new QMenu(this);

    // 菜单项
//    QAction *actionOpenLog = moreMenu->addAction("📂 打开日志文件");
//    connect(actionOpenLog, &QAction::triggered, this, [this]() {
//        if (m_logFile && m_logFile->isOpen()) {
//            m_logStream->flush();
//            QDesktopServices::openUrl(QUrl::fromLocalFile(m_logFilePath));
//            appendLog(QString("打开日志文件: %1").arg(m_logFilePath), "blue");
//        } else {
//            QMessageBox::warning(this, "提示", "日志文件未打开");
//        }
//    });
    QAction *actionOpenLog = moreMenu->addAction("📂 打开日志文件");
    connect(actionOpenLog, &QAction::triggered, this, [this]() {
        if (!m_logFile || !m_logFile->isOpen()) {
            QMessageBox::warning(this, "提示", "日志文件未打开");
            return;
        }
        m_logStream->flush();

        // 启动终端，tail -f 实时跟踪日志
        //QString cmd = QString("tail -f '%1'").arg(m_logFilePath);
        QString cmd = QString("less -R '%1'").arg(m_logFilePath);
        // 尝试几种常见终端，香橙派一般是 xfce4-terminal 或 gnome-terminal
        QProcess *proc = new QProcess(this);
        QStringList args;

        // 先试 xfce4-terminal（香橙派桌面常用）
        args << "-e" << cmd;
        proc->startDetached("xfce4-terminal", args);

        // 如果上面没启动起来，再试 gnome-terminal
        if (proc->error() == QProcess::FailedToStart) {
            QStringList args2;
            args2 << "--" << "tail" << "-f" << m_logFilePath;
            QProcess::startDetached("gnome-terminal", args2);
        }

        appendLog("打开日志终端窗口...", "blue");
    });


    QAction *actionClearLog = moreMenu->addAction("🗑 清空界面日志");
    connect(actionClearLog, &QAction::triggered, this, [this]() {
        textLog->clear();
        appendLog("界面日志已清空", "blue");
    });

    moreMenu->addSeparator();

    QAction *actionAbout = moreMenu->addAction("ℹ 关于");
    connect(actionAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "关于",
            "<h3>Binder Jetting 3D Printer Controller</h3>"
            "<p>版本: V1.0</p>"
            "<p>硬件: Cypress FX3 + 星光喷头 600DPI</p>"
            "<p>运行平台: 香橙派 H618</p>");
    });
    // 手动点击弹出菜单，不显示下拉箭头
    connect(btnMoreMenu, &QPushButton::clicked, this, [this, btnMoreMenu, moreMenu]() {
        // 在按钮下方弹出
        QPoint pos = btnMoreMenu->mapToGlobal(QPoint(0, btnMoreMenu->height()));
        moreMenu->exec(pos);
    });
    layout->addWidget(btnMoreMenu);


    qDebug() << "  <- createConnectionBar() 完成";
    // 保存布局引用
    layout->setObjectName("connectionLayout");

    return layout;
}

void MainWindow::createMainTab()
{
    tabWidget = new QTabWidget(this);

    createControlTab();
    createNegativePressureTab();
    createPrintParamsTab();     // ← 新增
    createWaveformTab();
    createMotionTab();

    tabWidget->addTab(findChild<QWidget*>("controlTab"), "操控界面");
    tabWidget->addTab(findChild<QWidget*>("negativeTab"), "负压设置");
    tabWidget->addTab(findChild<QWidget*>("printParamsTab"), "打印参数");  // ← 新增
    tabWidget->addTab(findChild<QWidget*>("waveformTab"), "波形控制");
    tabWidget->addTab(findChild<QWidget*>("motionTab"), "运动参数");
}

void MainWindow::on_btnClearLog_clicked()
{
    textLog->clear();
    appendLog("界面日志已清空", "blue");
}


void MainWindow::createControlTab()
{
    QWidget *tab = new QWidget(this);
    tab->setObjectName("controlTab");

    QHBoxLayout *mainLayout = new QHBoxLayout(tab);

    // ===== 左侧：控制按钮 =====
    QVBoxLayout *leftLayout = new QVBoxLayout();

    QGroupBox *controlGroup = createGroupBox("打印控制");
    QVBoxLayout *controlLayout = new QVBoxLayout(controlGroup);

    // 开始按钮
    btnStartPrint = new QPushButton("▶ 开始打印", this);
    btnStartPrint->setStyleSheet("background-color: #4CAF50; color: white; font-size: 14px; padding: 10px;");
    connect(btnStartPrint, &QPushButton::clicked, this, &MainWindow::on_btnStartPrint_clicked);
    controlLayout->addWidget(btnStartPrint);

    // 暂停/继续按钮
    btnPausePrint = new QPushButton("⏸ 暂停", this);
    btnPausePrint->setEnabled(false);
    btnPausePrint->setStyleSheet("background-color: #FF9800; color: white; font-size: 14px; padding: 10px;");
    connect(btnPausePrint, &QPushButton::clicked, this, &MainWindow::on_btnPausePrint_clicked);
    controlLayout->addWidget(btnPausePrint);

    // 停止按钮
    btnStopPrint = new QPushButton("⏹ 停止", this);
    btnStopPrint->setEnabled(false);
    btnStopPrint->setStyleSheet("background-color: #f44336; color: white; font-size: 14px; padding: 10px;");
    connect(btnStopPrint, &QPushButton::clicked, this, &MainWindow::on_btnStopPrint_clicked);
    controlLayout->addWidget(btnStopPrint);

    controlLayout->addSpacing(10);

    // 扫描按钮
    btnScan = new QPushButton("⟳ 扫描设备", this);
    btnScan->setEnabled(false);
    connect(btnScan, &QPushButton::clicked, this, &MainWindow::on_btnScan_clicked);
    controlLayout->addWidget(btnScan);

    // CLI 文件选择与切片
    btnOpenCli = new QPushButton("打开CLI文件", this);
    connect(btnOpenCli, &QPushButton::clicked, this, &MainWindow::on_btnOpenCli_clicked);
    controlLayout->addWidget(btnOpenCli);

    // 清空日志
    btnClearLog = new QPushButton("🗑 清空日志", this);
    connect(btnClearLog, &QPushButton::clicked, this, &MainWindow::on_btnClearLog_clicked);
    controlLayout->addWidget(btnClearLog);

    controlLayout->addStretch();
    leftLayout->addWidget(controlGroup);

    // 任务信息
    QGroupBox *taskGroup = createGroupBox("当前任务");
    QVBoxLayout *taskLayout = new QVBoxLayout(taskGroup);
    labelCliFile = new QLabel("模型: 未加载", this);
    labelCliFile->setWordWrap(true);
    taskLayout->addWidget(labelCliFile);
    QLabel *timeLabel = new QLabel("预计时间: --", this);
    taskLayout->addWidget(timeLabel);
    leftLayout->addWidget(taskGroup);

    mainLayout->addLayout(leftLayout, 1);

    // ===== 中间：打印预览 =====
    QVBoxLayout *centerLayout = new QVBoxLayout();

    // 状态显示
    QHBoxLayout *statusLayout = new QHBoxLayout();
    QLabel *statusLabel = new QLabel("状态:", this);
    statusLayout->addWidget(statusLabel);

    labelPrintStateText = new QLabel("空闲", this);
    labelPrintStateText->setStyleSheet("color: gray; font-weight: bold; font-size: 14px;");
    statusLayout->addWidget(labelPrintStateText);

    statusLayout->addStretch();

    QLabel *layerLabel = new QLabel("层数:", this);
    statusLayout->addWidget(layerLabel);

    labelLayerInfo = new QLabel("0/0", this);
    statusLayout->addWidget(labelLayerInfo);

    centerLayout->addLayout(statusLayout);

    // 进度条
    QHBoxLayout *progressLayout = new QHBoxLayout();
    progressPrint = new QProgressBar(this);
    progressPrint->setRange(0, 100);
    progressPrint->setValue(0);
    progressLayout->addWidget(progressPrint);

    labelProgress = new QLabel("0%", this);
    labelProgress->setMinimumWidth(40);
    progressLayout->addWidget(labelProgress);
    centerLayout->addLayout(progressLayout);

    // 打印预览
    QGroupBox *previewGroup = createGroupBox("打印预览");
    QVBoxLayout *previewLayout = new QVBoxLayout(previewGroup);

    printScene = new QGraphicsScene(this);
    printPreview = new QGraphicsView(printScene, this);
    printPreview->setMinimumHeight(300);
    printPreview->setBackgroundBrush(QBrush(Qt::white));
    previewLayout->addWidget(printPreview);

    // 切片层浏览
    QHBoxLayout *layerButtonLayout = new QHBoxLayout();
    btnPreviousLayer = new QPushButton("上一层", this);
    btnPreviousLayer->setEnabled(false);
    connect(btnPreviousLayer, &QPushButton::clicked,
            this, &MainWindow::on_btnPreviousLayer_clicked);
    layerButtonLayout->addWidget(btnPreviousLayer);

    btnNextLayer = new QPushButton("下一层", this);
    btnNextLayer->setEnabled(false);
    connect(btnNextLayer, &QPushButton::clicked,
            this, &MainWindow::on_btnNextLayer_clicked);
    layerButtonLayout->addWidget(btnNextLayer);
    previewLayout->addLayout(layerButtonLayout);

    centerLayout->addWidget(previewGroup);

    mainLayout->addLayout(centerLayout, 2);

    // ===== 右侧：日志 =====
    QVBoxLayout *rightLayout = new QVBoxLayout();

    QGroupBox *logGroup = createGroupBox("日志输出");
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);

    textLog = new QTextEdit(this);
    textLog->setReadOnly(true);
    textLog->setFont(QFont("Monospace", 10));
    textLog->setMinimumWidth(400);
    logLayout->addWidget(textLog);

    rightLayout->addWidget(logGroup);

    mainLayout->addLayout(rightLayout, 1);
}

void MainWindow::createNegativePressureTab()
{
    QWidget *tab = new QWidget(this);
    tab->setObjectName("negativeTab");

    QVBoxLayout *mainLayout = new QVBoxLayout(tab);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // ===== 第一行：三个负压通道控制 =====
    QHBoxLayout *channelsLayout = new QHBoxLayout();
    channelsLayout->setSpacing(15);

    // ----- 一级负压 -----
    QGroupBox *primaryGroup = createGroupBox("一级负压");
    QGridLayout *primaryLayout = new QGridLayout(primaryGroup);
    primaryLayout->setSpacing(8);
    primaryLayout->setContentsMargins(15, 20, 15, 15);

    // 目标值
    primaryLayout->addWidget(new QLabel("目标值:", this), 0, 0);
    spinPrimaryTarget = new QDoubleSpinBox(this);
    spinPrimaryTarget->setRange(-50, 0);
    spinPrimaryTarget->setValue(-12);
    spinPrimaryTarget->setSuffix(" kPa");
    spinPrimaryTarget->setFixedWidth(120);
    primaryLayout->addWidget(spinPrimaryTarget, 0, 1);

    QPushButton *btnSetPrimary = new QPushButton("设置", this);
    btnSetPrimary->setFixedWidth(60);
    connect(btnSetPrimary, &QPushButton::clicked,
            this, &MainWindow::on_btnSetPrimaryPressure_clicked);
    primaryLayout->addWidget(btnSetPrimary, 0, 2);

    // 一级负压 - 当前值显示
    primaryLayout->addWidget(new QLabel("当前值:", this), 1, 0);
    lcdPrimaryCurrent = new QLCDNumber(this);
    lcdPrimaryCurrent->setDigitCount(6);
    lcdPrimaryCurrent->display("-50.0");  // ← 显示初始值
    lcdPrimaryCurrent->setFixedHeight(40);
    primaryLayout->addWidget(lcdPrimaryCurrent, 1, 1, 1, 2);

    channelsLayout->addWidget(primaryGroup);

    // ----- 二级负压 -----
    QGroupBox *secondaryGroup = createGroupBox("二级负压");
    QGridLayout *secondaryLayout = new QGridLayout(secondaryGroup);
    secondaryLayout->setSpacing(8);
    secondaryLayout->setContentsMargins(15, 20, 15, 15);

    secondaryLayout->addWidget(new QLabel("目标值:", this), 0, 0);
    spinSecondaryTarget = new QDoubleSpinBox(this);
    spinSecondaryTarget->setRange(-50, 0);
    spinSecondaryTarget->setValue(-4);
    spinSecondaryTarget->setSingleStep(0.1);   // ← 新增：上下调节步进 0.1
    spinSecondaryTarget->setSuffix(" kPa");
    spinSecondaryTarget->setFixedWidth(120);
    secondaryLayout->addWidget(spinSecondaryTarget, 0, 1);

    QPushButton *btnSetSecondary = new QPushButton("设置", this);
    btnSetSecondary->setFixedWidth(60);
    connect(btnSetSecondary, &QPushButton::clicked,
            this, &MainWindow::on_btnSetSecondaryPressure_clicked);
    secondaryLayout->addWidget(btnSetSecondary, 0, 2);

    // 二级负压 - 当前值显示
    secondaryLayout->addWidget(new QLabel("当前值:", this), 1, 0);
    lcdSecondaryCurrent = new QLCDNumber(this);
    lcdSecondaryCurrent->setDigitCount(6);
    lcdSecondaryCurrent->display("-30.0");  // ← 显示初始值
    lcdSecondaryCurrent->setFixedHeight(40);
    secondaryLayout->addWidget(lcdSecondaryCurrent, 1, 1, 1, 2);

    channelsLayout->addWidget(secondaryGroup);

    // ----- 循环负压 -----
    QGroupBox *cyclicGroup = createGroupBox("循环负压");
    QGridLayout *cyclicLayout = new QGridLayout(cyclicGroup);
    cyclicLayout->setSpacing(8);
    cyclicLayout->setContentsMargins(15, 20, 15, 15);

    // 目标值
    cyclicLayout->addWidget(new QLabel("目标值:", this), 0, 0);
    spinCyclicTarget = new QDoubleSpinBox(this);
    spinCyclicTarget->setRange(-50, 0);
    spinCyclicTarget->setValue(-5);
    spinCyclicTarget->setSingleStep(0.1);   // ← 新增：上下调节步进 0.1
    spinCyclicTarget->setSuffix(" kPa");
    spinCyclicTarget->setFixedWidth(120);
    cyclicLayout->addWidget(spinCyclicTarget, 0, 1);

    QPushButton *btnSetCyclic = new QPushButton("设置", this);
    btnSetCyclic->setFixedWidth(60);
    connect(btnSetCyclic, &QPushButton::clicked,
            this, &MainWindow::on_btnSetCyclicPressure_clicked);
    cyclicLayout->addWidget(btnSetCyclic, 0, 2);

    // 当前值
    // 循环负压 - 当前值显示
    cyclicLayout->addWidget(new QLabel("当前值:", this), 1, 0);
    lcdCyclicCurrent = new QLCDNumber(this);
    lcdCyclicCurrent->setDigitCount(6);
    lcdCyclicCurrent->display("-20.0");  // ← 显示初始值
    lcdCyclicCurrent->setFixedHeight(40);
    cyclicLayout->addWidget(lcdCyclicCurrent, 1, 1, 1, 2);
    channelsLayout->addWidget(cyclicGroup);

    mainLayout->addLayout(channelsLayout);
    mainLayout->addLayout(channelsLayout);

    // ===== 二级压墨 + 复位控制 =====
    QGroupBox *inkGroup = createGroupBox("二级压墨控制");
    QHBoxLayout *inkLayout = new QHBoxLayout(inkGroup);
    inkLayout->setSpacing(12);
    inkLayout->setContentsMargins(15, 20, 15, 15);

    inkLayout->addWidget(new QLabel("压墨时间:", this));
    spinSecondaryInkTime = new QSpinBox(this);
    spinSecondaryInkTime->setRange(0, 300);
    //spinSecondaryInkTime->setValue(5);
    spinSecondaryInkTime->setValue(currentNegativeParams.secondaryInkTime);       // 原 setValue(5)
    spinSecondaryInkTime->setSuffix(" s");
    spinSecondaryInkTime->setFixedWidth(100);
    inkLayout->addWidget(spinSecondaryInkTime);

    inkLayout->addWidget(new QLabel("释放时间:", this));
    spinSecondaryReleaseTime = new QSpinBox(this);
    spinSecondaryReleaseTime->setRange(0, 300);
    //spinSecondaryReleaseTime->setValue(3);
    spinSecondaryReleaseTime->setValue(currentNegativeParams.secondaryReleaseTime); // 原 setValue(1)
    spinSecondaryReleaseTime->setSuffix(" s");
    spinSecondaryReleaseTime->setFixedWidth(100);
    inkLayout->addWidget(spinSecondaryReleaseTime);

    QPushButton *btnSecondaryInkPress = new QPushButton("二级压墨", this);
    btnSecondaryInkPress->setStyleSheet("background-color: #2196F3; color: white;");
    btnSecondaryInkPress->setFixedHeight(40);
    connect(btnSecondaryInkPress, &QPushButton::clicked,
            this, &MainWindow::on_btnSecondaryInkPress_clicked);
    inkLayout->addWidget(btnSecondaryInkPress);

    QPushButton *btnNegativeReset = new QPushButton("负压复位", this);
    btnNegativeReset->setStyleSheet("background-color: #f44336; color: white;");
    btnNegativeReset->setFixedHeight(40);
    connect(btnNegativeReset, &QPushButton::clicked,
            this, &MainWindow::on_btnNegativeReset_clicked);
    inkLayout->addWidget(btnNegativeReset);

    mainLayout->addWidget(inkGroup);

    // ===== 供墨/回墨泵 + 排废液 =====
    QGroupBox *pumpGroup = createGroupBox("泵控制");
    QHBoxLayout *pumpLayout = new QHBoxLayout(pumpGroup);
    pumpLayout->setSpacing(12);
    pumpLayout->setContentsMargins(15, 20, 15, 15);

    // 供墨泵
    pumpLayout->addWidget(new QLabel("供墨泵转速:", this));
    labelInkSupplySpeed = new QLabel("0", this);
    labelInkSupplySpeed->setFixedWidth(70);
    labelInkSupplySpeed->setStyleSheet("font-weight:bold;");
    pumpLayout->addWidget(labelInkSupplySpeed);
    pumpLayout->addWidget(new QLabel("设置:", this));
    spinInkSupplySpeedSet = new QSpinBox(this);
    spinInkSupplySpeedSet->setRange(0, 3000);
    spinInkSupplySpeedSet->setValue(currentNegativeParams.inkSupplySpeed);
    spinInkSupplySpeedSet->setSingleStep(100);   // ← 新增：上下调节步进 100
    spinInkSupplySpeedSet->setSuffix(" r/min");
    spinInkSupplySpeedSet->setFixedWidth(90);
    pumpLayout->addWidget(spinInkSupplySpeedSet);

    QPushButton *btnSetInkSupply = new QPushButton("设置", this);
    btnSetInkSupply->setFixedWidth(50);
    connect(btnSetInkSupply, &QPushButton::clicked,
            this, &MainWindow::on_btnSetInkSupplySpeed_clicked);
    pumpLayout->addWidget(btnSetInkSupply);
    pumpLayout->addSpacing(10);

    // 回墨泵
    pumpLayout->addWidget(new QLabel("回墨泵转速:", this));
    labelReturnPumpSpeed = new QLabel("0", this);
    labelReturnPumpSpeed->setFixedWidth(70);
    labelReturnPumpSpeed->setStyleSheet("font-weight:bold;");
    pumpLayout->addWidget(labelReturnPumpSpeed);
    pumpLayout->addWidget(new QLabel("设置:", this));
    spinReturnPumpSpeedSet = new QSpinBox(this);
    spinReturnPumpSpeedSet->setRange(0, 3000);
    spinReturnPumpSpeedSet->setValue(currentNegativeParams.returnPumpSpeed);
    spinReturnPumpSpeedSet->setSingleStep(100);  // ← 新增：上下调节步进 100
    spinReturnPumpSpeedSet->setSuffix(" r/min");
    spinReturnPumpSpeedSet->setFixedWidth(90);
    pumpLayout->addWidget(spinReturnPumpSpeedSet);
    QPushButton *btnSetReturnPump = new QPushButton("设置", this);
    btnSetReturnPump->setFixedWidth(50);
    connect(btnSetReturnPump, &QPushButton::clicked,
            this, &MainWindow::on_btnSetReturnPumpSpeed_clicked);
    pumpLayout->addWidget(btnSetReturnPump);
    pumpLayout->addSpacing(10);
    // 弹性空间（把排废液推到行尾）
    //pumpLayout->addStretch();
    // 排废液按钮（开关切换）
    btnWasteDischarge = new QPushButton("排废液开", this);
    btnWasteDischarge->setCheckable(true);
    btnWasteDischarge->setFixedHeight(40);
    btnWasteDischarge->setStyleSheet("background-color: #FF9800; color: white;");
    connect(btnWasteDischarge, &QPushButton::toggled,
            this, &MainWindow::on_btnWasteDischarge_toggled);
    pumpLayout->addWidget(btnWasteDischarge);

    pumpLayout->addStretch();
    mainLayout->addWidget(pumpGroup);
    // ===== 第二行：压力曲线图 =====
    QGroupBox *chartGroup = createGroupBox("压力曲线 (两通道)");
    QVBoxLayout *chartLayout = new QVBoxLayout(chartGroup);
    chartLayout->setContentsMargins(10, 10, 10, 10);

    pressureScene = new QGraphicsScene(this);
    pressureChart = new QGraphicsView(pressureScene, this);
    pressureChart->setMinimumHeight(200);
    pressureChart->setBackgroundBrush(QBrush(Qt::white));
    chartLayout->addWidget(pressureChart);

    // 图例
    QHBoxLayout *legendLayout = new QHBoxLayout();
    legendLayout->addSpacing(20);

    QLabel *legend2 = new QLabel("● 二级负压", this);
    legend2->setStyleSheet("color: #FF9800; font-weight: bold;");
    legendLayout->addWidget(legend2);
    legendLayout->addSpacing(20);

    QLabel *legend3 = new QLabel("● 循环负压", this);
    legend3->setStyleSheet("color: #4CAF50; font-weight: bold;");
    legendLayout->addWidget(legend3);
    legendLayout->addStretch();

    chartLayout->addLayout(legendLayout);
    mainLayout->addWidget(chartGroup);

    // 绘制初始图表
    drawPressureChart();
}

void MainWindow::createPrintParamsTab()
{
    QWidget *tab = new QWidget(this);
    tab->setObjectName("printParamsTab");

    QHBoxLayout *mainLayout = new QHBoxLayout(tab);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // ===== 左侧：打印参数 =====
    QVBoxLayout *leftLayout = new QVBoxLayout();
    QGroupBox *paramGroup = createGroupBox("打印参数");
    QGridLayout *paramLayout = new QGridLayout(paramGroup);
    paramLayout->setSpacing(8);
    paramLayout->setContentsMargins(15, 20, 15, 15);

    // 喷头个数
    paramLayout->addWidget(new QLabel("喷头个数:", this), 0, 0);
    spinHeadCount = new QSpinBox(this);
    spinHeadCount->setRange(1, 8);
    spinHeadCount->setValue(currentPrintParams.headCount);
    spinHeadCount->setSuffix(" 个");
    spinHeadCount->setFixedWidth(120);
    paramLayout->addWidget(spinHeadCount, 0, 1);
    QLabel *lrCountHint = new QLabel(this);
    lrCountHint->setStyleSheet("color: #666; font-size: 12px;");
    paramLayout->addWidget(lrCountHint, 0, 2);
    connect(spinHeadCount, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this, lrCountHint](int n) {
        lrCountHint->setText(QString("左右值/重叠孔: %1 个")
                             .arg(n > 1 ? n - 1 : 0));
        rebuildPrintParamRows();
    });
    lrCountHint->setText(QString("左右值/重叠孔: %1 个")
                         .arg(spinHeadCount->value() > 1 ? spinHeadCount->value() - 1 : 0));

    // DPI
    paramLayout->addWidget(new QLabel("DPI:", this), 1, 0);
    spinDpi = new QSpinBox(this);
    spinDpi->setRange(200, 1600);
    spinDpi->setSingleStep(50);
    spinDpi->setValue(currentPrintParams.dpi);
    spinDpi->setSuffix(" dpi");
    paramLayout->addWidget(spinDpi, 1, 1);

    // pass 数（替换原 X/Y 打印速度）
    paramLayout->addWidget(new QLabel("pass 数:", this), 2, 0);
    spinPassCount = new QSpinBox(this);
    spinPassCount->setRange(1, 16);   // 上限是默认值，你确定量程后可改
    spinPassCount->setValue(currentPrintParams.passCount);
    spinPassCount->setSuffix(" pass");
    paramLayout->addWidget(spinPassCount, 2, 1);
    QLabel *biCountHint = new QLabel(this);
    biCountHint->setStyleSheet("color: #666; font-size: 12px;");
    paramLayout->addWidget(biCountHint, 2, 2);
    connect(spinPassCount, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this, biCountHint](int p) {
        biCountHint->setText(QString("双向值: %1 个").arg(p > 1 ? p - 1 : 0));
        rebuildBidirectionalRows();
    });
    biCountHint->setText(QString("双向值: %1 个")
                         .arg(spinPassCount->value() > 1 ? spinPassCount->value() - 1 : 0));

    // ===== 位深开关：.bin / tem.bin 一个像素用 1bit 还是 2bit 表示 =====
    paramLayout->addWidget(new QLabel("位深:", this), 3, 0);
    comboPixelFormat = new QComboBox(this);
    // itemData 存真实位深，界面文案以后随便改，取值逻辑不受影响
    comboPixelFormat->addItem("2bit/像素（灰度，兼容原流程）", 2);
    comboPixelFormat->addItem("1bit/像素（单色，数据量减半）", 1);
    comboPixelFormat->setCurrentIndex(currentPrintParams.bitsPerPixel == 1 ? 1 : 0);
    paramLayout->addWidget(comboPixelFormat, 3, 1, 1, 2);
    connect(comboPixelFormat, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPixelFormatChanged);

    labelPixelFormatHint = new QLabel(this);
    labelPixelFormatHint->setStyleSheet("color: #666; font-size: 12px;");
    labelPixelFormatHint->setWordWrap(true);
    paramLayout->addWidget(labelPixelFormatHint, 4, 0, 1, 3);
    // 初始说明文案：按当前设置填一次（此时信号已连上，但 setCurrentIndex 在 connect 之前调用过，不会触发重切片）
    labelPixelFormatHint->setText((currentPrintParams.bitsPerPixel == 1)
        ? "1bit/像素：每行 ceil(宽/8) 字节，像素1=喷；发送端按位直接取 1024 像素，不再做 2bit→喷/不喷 转换。"
        : "2bit/像素：每行 ceil(宽/4) 字节，像素0=背景、1/2/3=喷印；发送端把非 0 像素转成 1bit 再下发。");

    leftLayout->addWidget(paramGroup);

    // 应用按钮
    btnApplyPrintParams = new QPushButton("应用打印参数", this);
    btnApplyPrintParams->setMinimumHeight(35);
    btnApplyPrintParams->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
    connect(btnApplyPrintParams, &QPushButton::clicked,
            this, &MainWindow::on_btnApplyPrintParams_clicked);
    leftLayout->addWidget(btnApplyPrintParams);

    leftLayout->addStretch();
    mainLayout->addLayout(leftLayout, 1);

    // ===== 右侧：两个动态组 =====
    QVBoxLayout *rightLayout = new QVBoxLayout();

    // 组1：左右值 / 重叠孔（喷头拼接校准）
    QGroupBox *lrGroup = createGroupBox("左右值 / 重叠孔（喷头拼接校准）");
    QVBoxLayout *lrOuter = new QVBoxLayout(lrGroup);
    lrOuter->setSpacing(8);
    QLabel *hint = new QLabel(
        "左右值：X 方向安装偏差矫正，按 DPI 图像像素计算。\n"
        "重叠孔：Y 方向重叠孔数，一般按 400DPI 计算重叠一个孔的位置，\n"
        "        喷头安装时特意重叠几个孔，防止拼接处断裂。\n"
        "单喷头（个数=1）时无需左右值与重叠孔。", this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #666; font-size: 12px;");
    lrOuter->addWidget(hint);
    labelLRNone = new QLabel("单喷头：无需左右值 / 重叠孔", this);
    labelLRNone->setStyleSheet("color: #888; font-style: italic;");
    labelLRNone->setVisible(false);
    lrOuter->addWidget(labelLRNone);
    printParamGrid = new QGridLayout();
    printParamGrid->setSpacing(8);
    printParamGrid->addWidget(new QLabel("接缝（喷头间）", this), 0, 0);
    printParamGrid->addWidget(new QLabel("左右值 (px)", this), 0, 1);
    printParamGrid->addWidget(new QLabel("重叠孔 (个)", this), 0, 2);
    lrOuter->addLayout(printParamGrid);
    rightLayout->addWidget(lrGroup);

    // 组2：双向值（往返 pass 起点对齐）
    QGroupBox *biGroup = createGroupBox("双向值（往返 pass 起点对齐）");
    QVBoxLayout *biOuter = new QVBoxLayout(biGroup);
    biOuter->setSpacing(8);
    QLabel *biHint = new QLabel(
        "双向值：调节多 pass 之间的打印起点。去程为 1pass、回程为 2pass，\n"
        "方向不一致会导致相同起点的喷墨落点不同，用双向值按 DPI 像素补偿。\n"
        "单 pass（pass数=1）时无需双向值。", this);
    biHint->setWordWrap(true);
    biHint->setStyleSheet("color: #666; font-size: 12px;");
    biOuter->addWidget(biHint);
    labelBINone = new QLabel("单 pass：无需双向值", this);
    labelBINone->setStyleSheet("color: #888; font-style: italic;");
    labelBINone->setVisible(false);
    biOuter->addWidget(labelBINone);
    bidirectionalGrid = new QGridLayout();
    bidirectionalGrid->setSpacing(8);
    bidirectionalGrid->addWidget(new QLabel("pass 间", this), 0, 0);
    bidirectionalGrid->addWidget(new QLabel("双向值 (px)", this), 0, 1);
    biOuter->addLayout(bidirectionalGrid);
    rightLayout->addWidget(biGroup);

    rightLayout->addStretch();
    mainLayout->addLayout(rightLayout, 2);

    // 生成初始动态行
    rebuildPrintParamRows();
    rebuildBidirectionalRows();
}

// 根据喷头个数重建左右值/重叠孔输入行（N-1 个；N=1 时无）
void MainWindow::rebuildPrintParamRows()
{
    // 清空旧行
    for (QSpinBox *s : spinLeftRight) { printParamGrid->removeWidget(s); delete s; }
    spinLeftRight.clear();
    for (QSpinBox *s : spinOverlap)   { printParamGrid->removeWidget(s); delete s; }
    spinOverlap.clear();
    for (QLabel *l : m_lrLabels)      { printParamGrid->removeWidget(l); delete l; }
    m_lrLabels.clear();

    const int n = spinHeadCount->value();
    const bool multi = n > 1;
    labelLRNone->setVisible(!multi);
    if (!multi) return;

    for (int i = 0; i < n - 1; ++i) {
        const int row = i + 1;   // 第 0 行是表头

        QLabel *lab = new QLabel(QString("接缝%1：喷头%2 ↔ 喷头%3")
                                 .arg(i + 1).arg(i + 1).arg(i + 2), this);
        m_lrLabels << lab;
        printParamGrid->addWidget(lab, row, 0);

        QSpinBox *lr = new QSpinBox(this);
        lr->setRange(-100, 100);   // 左右偏移像素，范围可改
        lr->setValue(i < currentPrintParams.leftRight.size()
                     ? currentPrintParams.leftRight.at(i) : 0);
        lr->setSuffix(" px");
        spinLeftRight << lr;
        printParamGrid->addWidget(lr, row, 1);

        QSpinBox *ov = new QSpinBox(this);
        ov->setRange(0, 32);       // 重叠孔 0~32，范围可改
        ov->setValue(i < currentPrintParams.overlapNozzle.size()
                     ? currentPrintParams.overlapNozzle.at(i) : 0);
        ov->setSuffix(" 孔");
        spinOverlap << ov;
        printParamGrid->addWidget(ov, row, 2);
    }
}

// 根据 pass 数重建双向值输入行（pass数-1 个；pass=1 时无）
void MainWindow::rebuildBidirectionalRows()
{
    // 清空旧行
    for (QSpinBox *s : spinBidirectional) { bidirectionalGrid->removeWidget(s); delete s; }
    spinBidirectional.clear();
    for (QLabel *l : m_biLabels) { bidirectionalGrid->removeWidget(l); delete l; }
    m_biLabels.clear();

    const int p = spinPassCount->value();
    const bool multi = p > 1;
    labelBINone->setVisible(!multi);
    if (!multi) return;

    for (int i = 0; i < p - 1; ++i) {
        const int row = i + 1;   // 第 0 行是表头

        QLabel *lab = new QLabel(QString("pass%1 ↔ pass%2").arg(i + 1).arg(i + 2), this);
        m_biLabels << lab;
        bidirectionalGrid->addWidget(lab, row, 0);

        QSpinBox *bi = new QSpinBox(this);
        bi->setRange(-50, 50);    // 双向值范围 -50~50
        bi->setValue(i < currentPrintParams.bidirectional.size()
                     ? currentPrintParams.bidirectional.at(i) : 0);
        bi->setSuffix(" px");
        spinBidirectional << bi;
        bidirectionalGrid->addWidget(bi, row, 1);
    }
}


// 应用打印参数：保存 + 日志 + （待补固件下发）

void MainWindow::on_btnApplyPrintParams_clicked()
{
    currentPrintParams.headCount = spinHeadCount->value();
    currentPrintParams.dpi       = spinDpi->value();
    currentPrintParams.passCount = spinPassCount->value();
    if (comboPixelFormat) {
        const int bpp = comboPixelFormat->currentData().toInt();
        currentPrintParams.bitsPerPixel = (bpp == 1) ? 1 : 2;
    }

    currentPrintParams.leftRight.clear();
    currentPrintParams.overlapNozzle.clear();
    currentPrintParams.bidirectional.clear();
    for (QSpinBox *s : spinLeftRight)     currentPrintParams.leftRight << s->value();
    for (QSpinBox *s : spinOverlap)       currentPrintParams.overlapNozzle << s->value();
    for (QSpinBox *s : spinBidirectional) currentPrintParams.bidirectional << s->value();

    saveParams->savePrintParams(currentPrintParams);

    QStringList lr, ov, bi;
    for (int v : currentPrintParams.leftRight)      lr << QString::number(v);
    for (int v : currentPrintParams.overlapNozzle)  ov << QString::number(v);
    for (int v : currentPrintParams.bidirectional)  bi << QString::number(v);

    appendLog(QString("应用打印参数: 喷头=%1, DPI=%2, pass数=%3, 位深=%4bit/px")
              .arg(currentPrintParams.headCount).arg(currentPrintParams.dpi)
              .arg(currentPrintParams.passCount).arg(currentPrintParams.bitsPerPixel), "blue");
    appendLog(QString("左右值(px): %1").arg(lr.join(", ")), "darkCyan");
    appendLog(QString("重叠孔(个): %1").arg(ov.join(", ")), "darkCyan");
    appendLog(QString("双向值(px): %1").arg(bi.join(", ")), "darkCyan");

    // TODO: 下发到设备的命令字待固件协议确认后补充（不编造）
}

/*
 * 位深开关：切换 .bin / tem.bin 的每像素位数（1bit 或 2bit）。
 *
 * 1）记录到 currentPrintParams 并落盘（SaveParams，键 PrintParams/bitsPerPixel）；
 * 2）已加载模型时立刻按新位深重切：切片缓存目录名带 _bppN，
 *    所以换位深必然走不到旧缓存，不会拿到位深不匹配的 layer_001.bin；
 * 3）发送端（on_btnStartPrint_clicked）按 previewBpp 选对应的取像素方式，
 *    最终下发到 FPGA 的仍然是 1bit/喷孔（1024 喷孔 = 128 字节），线格式不受影响。
 */
void MainWindow::onPixelFormatChanged(int index)
{
    const int bpp = (comboPixelFormat->itemData(index).toInt() == 1) ? 1 : 2;
    const bool changed = (currentPrintParams.bitsPerPixel != bpp);

    currentPrintParams.bitsPerPixel = bpp;
    saveParams->savePrintParams(currentPrintParams);

    if (labelPixelFormatHint) {
        labelPixelFormatHint->setText((bpp == 1)
            ? "1bit/像素：每行 ceil(宽/8) 字节，像素1=喷；发送端按位直接取 1024 像素，不再做 2bit→喷/不喷 转换。"
            : "2bit/像素：每行 ceil(宽/4) 字节，像素0=背景、1/2/3=喷印；发送端把非 0 像素转成 1bit 再下发。");
    }
    appendLog(QString("位深切换为 %1bit/pixel（%2）")
              .arg(bpp).arg(bpp == 1 ? "单色，体积减半" : "灰度，兼容原流程"), "blue");

    if (!changed) return;
    if (currentCliFile.isEmpty()) {
        appendLog("尚未加载模型，下次打开 CLI 时按新位深切片", "darkCyan");
        return;
    }
    if (cliThread && cliThread->isRunning()) {
        appendLog("切片正在进行中，本次位深改动将在重新打开 CLI 后生效", "orange");
        return;
    }
    appendLog("按新位深重新切片...", "orange");
    openCliFile(currentCliFile);   // 缓存名带位深，必然重新切片
}


void MainWindow::createWaveformTab()
{
    QWidget *tab = new QWidget(this);
    tab->setObjectName("waveformTab");

    QHBoxLayout *mainLayout = new QHBoxLayout(tab);

    // ===== 左侧：参数控制 =====
    QVBoxLayout *leftLayout = new QVBoxLayout();

    QGroupBox *paramGroup = createGroupBox("波形参数");
    QGridLayout *paramLayout = new QGridLayout(paramGroup);

    // 电压
    paramLayout->addWidget(new QLabel("驱动电压:", this), 0, 0);
    spinVoltage = new QDoubleSpinBox(this);
    spinVoltage->setRange(0, 80);
    spinVoltage->setValue(45);
    spinVoltage->setSuffix(" V");
    paramLayout->addWidget(spinVoltage, 0, 1);

    // 上升时间
    paramLayout->addWidget(new QLabel("上升时间:", this), 1, 0);
    spinRiseTime = new QDoubleSpinBox(this);
    spinRiseTime->setRange(0.5, 10);
    spinRiseTime->setValue(2);
    spinRiseTime->setSuffix(" μs");
    paramLayout->addWidget(spinRiseTime, 1, 1);

    // 维持时间
    paramLayout->addWidget(new QLabel("维持时间:", this), 2, 0);
    spinDwellTime = new QDoubleSpinBox(this);
    spinDwellTime->setRange(1, 20);
    spinDwellTime->setValue(6);
    spinDwellTime->setSuffix(" μs");
    paramLayout->addWidget(spinDwellTime, 2, 1);

    // 下降时间
    paramLayout->addWidget(new QLabel("下降时间:", this), 3, 0);
    spinFallTime = new QDoubleSpinBox(this);
    spinFallTime->setRange(0.5, 10);
    spinFallTime->setValue(2);
    spinFallTime->setSuffix(" μs");
    paramLayout->addWidget(spinFallTime, 3, 1);

    // 频率
    paramLayout->addWidget(new QLabel("频率:", this), 4, 0);
    spinFrequency = new QDoubleSpinBox(this);
    spinFrequency->setRange(1, 50);
    spinFrequency->setValue(20);
    spinFrequency->setSuffix(" kHz");
    paramLayout->addWidget(spinFrequency, 4, 1);

    leftLayout->addWidget(paramGroup);

    // 控制按钮
    QVBoxLayout *btnLayout = new QVBoxLayout();
    btnApplyWaveform = new QPushButton("应用波形", this);
    btnApplyWaveform->setStyleSheet("background-color: #4CAF50; color: white; font-size: 12px; padding: 8px;");
    connect(btnApplyWaveform, &QPushButton::clicked, this, &MainWindow::on_btnApplyWaveform_clicked);
    btnLayout->addWidget(btnApplyWaveform);

    btnLoadWaveform = new QPushButton("加载波形文件", this);
    connect(btnLoadWaveform, &QPushButton::clicked, this, &MainWindow::on_btnLoadWaveform_clicked);
    btnLayout->addWidget(btnLoadWaveform);

    btnSaveWaveform = new QPushButton("保存波形配置", this);
    connect(btnSaveWaveform, &QPushButton::clicked, this, &MainWindow::on_btnSaveWaveform_clicked);
    btnLayout->addWidget(btnSaveWaveform);

    leftLayout->addLayout(btnLayout);

    // 喷头测试
    QGroupBox *testGroup = createGroupBox("喷头测试");
    QVBoxLayout *testLayout = new QVBoxLayout(testGroup);

    QHBoxLayout *testRow = new QHBoxLayout();
    testRow->addWidget(new QLabel("测试模式:", this));
    comboTestPattern = new QComboBox(this);
    comboTestPattern->addItems({"单孔测试", "全孔测试", "脉冲测试", "连续喷射"});
    testRow->addWidget(comboTestPattern);
    testLayout->addLayout(testRow);

    btnTestJet = new QPushButton("执行测试", this);
    btnTestJet->setStyleSheet("background-color: #FF9800; color: white;");
    connect(btnTestJet, &QPushButton::clicked, this, &MainWindow::on_btnTestJet_clicked);
    testLayout->addWidget(btnTestJet);

    // 喷头温度
    QHBoxLayout *tempRow = new QHBoxLayout();
    tempRow->addWidget(new QLabel("喷头温度:", this));
    lcdJetTemp = new QLCDNumber(this);
    lcdJetTemp->setDigitCount(5);
    lcdJetTemp->display(35.2);
    tempRow->addWidget(lcdJetTemp);
    tempRow->addWidget(new QLabel("°C", this));
    testLayout->addLayout(tempRow);

    // 状态
    QHBoxLayout *statusRow = new QHBoxLayout();
    statusRow->addWidget(new QLabel("状态:", this));
    labelWaveformStatus = new QLabel("○ 未加载", this);
    labelWaveformStatus->setStyleSheet("color: gray; font-weight: bold;");
    statusRow->addWidget(labelWaveformStatus);
    testLayout->addLayout(statusRow);

    leftLayout->addWidget(testGroup);
    leftLayout->addStretch();

    mainLayout->addLayout(leftLayout, 1);

    // ===== 右侧：波形预览 =====
    QVBoxLayout *rightLayout = new QVBoxLayout();

    QGroupBox *previewGroup = createGroupBox("波形预览");
    QVBoxLayout *previewLayout = new QVBoxLayout(previewGroup);

    waveformScene = new QGraphicsScene(this);
    waveformPreview = new QGraphicsView(waveformScene, this);
    waveformPreview->setMinimumHeight(400);
    waveformPreview->setBackgroundBrush(QBrush(Qt::white));
    previewLayout->addWidget(waveformPreview);

    rightLayout->addWidget(previewGroup);

    mainLayout->addLayout(rightLayout, 2);

    // 绘制初始波形
    drawWaveformPreview();
}

void MainWindow::createMotionTab()
{
    QWidget *tab = new QWidget(this);       //运动参数 Tab 页
    tab->setObjectName("motionTab");

    QHBoxLayout *mainLayout = new QHBoxLayout(tab);     //(QHBoxLayout) ── 水平布局
    mainLayout->setSpacing(10);                         // 设置布局内控件之间的间距 px像素
    mainLayout->setContentsMargins(10, 10, 10, 10);     // 设置布局内容与父容器边界之间的边距 左边距 10px 上边距 10px 右边距 10px 下边距 10px

    // ===== 左侧：运动参数 =====
    QVBoxLayout *leftLayout = new QVBoxLayout();        // (QVBoxLayout) ── 垂直布局（左侧）
    leftLayout->setSpacing(10);

    QGroupBox *paramGroup = new QGroupBox("运动参数", this);
    paramGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 1px solid #ccc; border-radius: 5px; margin-top: 10px; }"
                              "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QGridLayout *paramLayout = new QGridLayout(paramGroup);
    paramLayout->setSpacing(8);
    paramLayout->setContentsMargins(15, 20, 15, 15);

    // X轴速度
    paramLayout->addWidget(new QLabel("X轴速度:", this), 0, 0);                                                                                        // 标签放 (0, 0)
    spinSpeedX = new QDoubleSpinBox(this);      // ← 新增控件
    spinSpeedX->setRange(100, 2000);            // 控件与控件之间的空白
    //spinSpeedX->setValue(300);                  // 程序启动时的初始值
    spinSpeedX->setValue(currentMotionParams.speedX);
    spinSpeedX->setSuffix(" mm/s");             // 数值后面显示的单位
    spinSpeedX->setFixedWidth(150);             // 控件的宽度（不随窗口变化）
    paramLayout->addWidget(spinSpeedX, 0, 1);   // 将spinSpeedX 控件添加到网格布局 (spinSpeedX, 0, 1) (要添加的控件（浮点数输入框）, 放在第 0 行, 放在第 1 列)  // 输入框放 (0, 1)


    // Y轴速度
    paramLayout->addWidget(new QLabel("Y轴速度:", this), 1, 0);
    spinSpeedY = new QDoubleSpinBox(this);    // ← 新增控件
    spinSpeedY->setRange(100, 2000);
    //spinSpeedY->setValue(300);
    spinSpeedY->setValue(currentMotionParams.speedY);
    spinSpeedY->setSuffix(" mm/s");
    spinSpeedY->setFixedWidth(150);
    paramLayout->addWidget(spinSpeedY, 1, 1);

    // Z轴速度
    paramLayout->addWidget(new QLabel("Z轴速度:", this), 2, 0);
    spinSpeedZ = new QDoubleSpinBox(this);
    spinSpeedZ->setRange(10, 200);
    //spinSpeedZ->setValue(50);
    spinSpeedZ->setValue(currentMotionParams.speedZ);
    spinSpeedZ->setSuffix(" mm/s");
    spinSpeedZ->setFixedWidth(150);
    paramLayout->addWidget(spinSpeedZ, 2, 1);

    // 加速度
    paramLayout->addWidget(new QLabel("加速度:", this), 3, 0);
    spinAcceleration = new QDoubleSpinBox(this);
    spinAcceleration->setRange(100, 5000);
    //spinAcceleration->setValue(1000);
    spinAcceleration->setValue(currentMotionParams.acceleration);
    spinAcceleration->setSuffix(" mm/s²");
    spinAcceleration->setFixedWidth(150);
    paramLayout->addWidget(spinAcceleration, 3, 1);

    // Jerk
    paramLayout->addWidget(new QLabel("Jerk:", this), 4, 0);
    spinJerk = new QDoubleSpinBox(this);
    spinJerk->setRange(1, 20);
    //spinJerk->setValue(5);
    spinJerk->setValue(currentMotionParams.jerk);
    spinJerk->setSuffix(" mm/s³");
    spinJerk->setFixedWidth(150);
    paramLayout->addWidget(spinJerk, 4, 1);

    // 层厚
    paramLayout->addWidget(new QLabel("层厚:", this), 5, 0);
    spinLayerHeight = new QDoubleSpinBox(this);
    spinLayerHeight->setRange(0.02, 0.3);
    spinLayerHeight->setSingleStep(0.01);
    //spinLayerHeight->setValue(0.1);
    spinLayerHeight->setValue(currentMotionParams.layerHeight);
    spinLayerHeight->setSuffix(" mm");
    spinLayerHeight->setFixedWidth(150);
    paramLayout->addWidget(spinLayerHeight, 5, 1);

    leftLayout->addWidget(paramGroup);

    btnSetMotionParams = new QPushButton("应用运动参数", this);
    btnSetMotionParams->setMinimumHeight(35);
    btnSetMotionParams->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
    connect(btnSetMotionParams, &QPushButton::clicked, this, &MainWindow::on_btnSetMotionParams_clicked);
    leftLayout->addWidget(btnSetMotionParams);

    leftLayout->addStretch();
    mainLayout->addLayout(leftLayout, 1);       // 添加空间到mainLayout (leftLayout, 1) （要添加的布局对象，拉伸系数（Stretch Factor））

    // ===== 中间：手动控制 =====
    QVBoxLayout *centerLayout = new QVBoxLayout();  // (QVBoxLayout) ── 垂直布局（中间手动控制）
    centerLayout->setSpacing(10);

    QGroupBox *manualGroup = new QGroupBox("手动控制", this);
    manualGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 1px solid #ccc; border-radius: 5px; margin-top: 10px; }"
                               "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QGridLayout *manualLayout = new QGridLayout(manualGroup);
    manualLayout->setSpacing(8);
    manualLayout->setContentsMargins(15, 20, 15, 15);

    // X轴 手动
    manualLayout->addWidget(new QLabel("X:", this), 0, 0);
    spinPosX = new QDoubleSpinBox(this);
    spinPosX->setRange(-500, 500);
    spinPosX->setValue(0);
    spinPosX->setSuffix(" mm");
    spinPosX->setFixedWidth(150);
    manualLayout->addWidget(spinPosX, 0, 1);

    // Y轴 手动
    manualLayout->addWidget(new QLabel("Y:", this), 1, 0);
    spinPosY = new QDoubleSpinBox(this);
    spinPosY->setRange(-500, 500);
    spinPosY->setValue(0);
    spinPosY->setSuffix(" mm");
    spinPosY->setFixedWidth(150);
    manualLayout->addWidget(spinPosY, 1, 1);

    // Z轴 手动
    manualLayout->addWidget(new QLabel("Z:", this), 2, 0);
    spinPosZ = new QDoubleSpinBox(this);
    spinPosZ->setRange(0, 400);
    spinPosZ->setValue(0);
    spinPosZ->setSuffix(" mm");
    spinPosZ->setFixedWidth(150);
    manualLayout->addWidget(spinPosZ, 2, 1);

    // 控制按钮
    QHBoxLayout *moveBtnLayout = new QHBoxLayout();
    moveBtnLayout->setSpacing(8);

    btnMoveTo = new QPushButton("移动到", this);
    btnMoveTo->setMinimumHeight(30);
    connect(btnMoveTo, &QPushButton::clicked, this, &MainWindow::on_btnMoveTo_clicked);
    moveBtnLayout->addWidget(btnMoveTo);

    btnHomeAll = new QPushButton("归零", this);
    btnHomeAll->setMinimumHeight(30);
    connect(btnHomeAll, &QPushButton::clicked, this, &MainWindow::on_btnHomeAll_clicked);
    moveBtnLayout->addWidget(btnHomeAll);

    btnSetOrigin = new QPushButton("设原点", this);
    btnSetOrigin->setMinimumHeight(30);
    connect(btnSetOrigin, &QPushButton::clicked, this, &MainWindow::on_btnSetOrigin_clicked);
    moveBtnLayout->addWidget(btnSetOrigin);

    manualLayout->addLayout(moveBtnLayout, 3, 0, 1, 2);

    centerLayout->addWidget(manualGroup);

    // 当前位置显示
    QGroupBox *posGroup = new QGroupBox("当前位置", this);
    posGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 1px solid #ccc; border-radius: 5px; margin-top: 10px; }"
                            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QVBoxLayout *posLayout = new QVBoxLayout(posGroup);
    posLayout->setContentsMargins(15, 20, 15, 15);

    labelCurrentPos = new QLabel("X: 0.00  Y: 0.00  Z: 0.00  mm", this);
    labelCurrentPos->setFont(QFont("Monospace", 12));
    labelCurrentPos->setStyleSheet("font-weight: bold;");
    posLayout->addWidget(labelCurrentPos);

    centerLayout->addWidget(posGroup);

    centerLayout->addStretch();
    mainLayout->addLayout(centerLayout, 1);

    // ===== 右侧：预留 =====
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->addStretch();
    mainLayout->addLayout(rightLayout, 1);  // 调用顺序 = 从左到右的排列顺序
}

void MainWindow::createStatusBar()
{
    QStatusBar *bar = statusBar();

    // 温度显示
    QLabel *tempLabel = new QLabel("温度:", this);
    bar->addWidget(tempLabel);

    lcdTemperature = new QLCDNumber(this);
    lcdTemperature->setDigitCount(5);
    lcdTemperature->display(25.0);
    bar->addWidget(lcdTemperature);

    QLabel *tempUnit = new QLabel("°C", this);
    bar->addWidget(tempUnit);

    bar->addPermanentWidget(new QLabel("   "));

    // 负压显示
    QLabel *pressureLabel = new QLabel("负压:", this);
    bar->addPermanentWidget(pressureLabel);

    QLCDNumber *statusPressure = new QLCDNumber(this);
    statusPressure->setDigitCount(6);
    statusPressure->display(-50.0);
    bar->addPermanentWidget(statusPressure);

    QLabel *pressureUnit = new QLabel("kPa", this);
    bar->addPermanentWidget(pressureUnit);

    bar->addPermanentWidget(new QLabel("   "));

    // 打印进度
    QLabel *progressLabel = new QLabel("进度:", this);
    bar->addPermanentWidget(progressLabel);
}
// ==================== USB连接槽函数 ====================

//void MainWindow::on_btnConnect_clicked()
//{
//    btnConnect->setEnabled(false);
//    appendLog("正在连接FX3 USB设备...", "blue");
//    usbManager->connectDevice();
//}

//void MainWindow::on_btnDisconnect_clicked()
//{
//    usbManager->disconnectDevice();
//    appendLog("已断开设备连接", "orange");
//}

//void MainWindow::on_btnRefresh_clicked()
//{
//    const QStringList devices = usbManager->getAvailableDevices();
//    appendLog(devices.isEmpty() ? "未检测到FX3 USB设备" :
//              QString("发现FX3设备: %1").arg(devices.join(", ")), devices.isEmpty() ? "orange" : "green");
//}

//void MainWindow::onUsbStatusChanged(bool connected)
//{
//    updateUsbStatus(connected);
//    if (connected) {
//        QString info = usbManager->getDeviceInfo();
//        appendLog(QString("设备就绪: %1").arg(info), "green");
//    }
//}
void MainWindow::onUsbStatusChanged(bool connected)
{
    updateUsbStatus(connected);   // ← 加这行，更新按钮状态

    if (connected) {
        appendLog("USB连接成功", "green");

        // ===== 如果用户已经点了板卡上电，自动启动初始化序列 =====
        if (boardPowered && m_fpgaInitStep == FPGA_INIT_IDLE) {
            labelUsbStatus->setText("● 板卡初始化中...");
            labelUsbStatus->setStyleSheet("color: orange; font-weight: bold; font-size: 12px;");
            appendLog("USB连接成功，500ms后开始FX3初始化...", "blue");
            m_fpgaInitTimer->start();
        } else {
            labelUsbStatus->setText("● USB已连接");
            labelUsbStatus->setStyleSheet("color: green; font-weight: bold; font-size: 12px;");
        }
    } else {
        appendLog("USB已断开", "orange");
        labelUsbStatus->setText("● USB已断开");
        labelUsbStatus->setStyleSheet("color: red; font-weight: bold; font-size: 12px;");

        // USB断了，重置初始化状态
        m_fpgaInitStep = FPGA_INIT_IDLE;
        if (boardPowered) {
            boardPowered = false;
            btnBoardPower->setChecked(false);
        }
    }
}


// TODO: 解析实际数据包
void MainWindow::onUsbDataReceived(const QByteArray &data)
{
    // 先打印收到的hex，方便调试协议 // USB接收数据：只写日志文件，不显示到界面（hex太多会刷屏）
    appendLog(QString("RX FX3: %1字节, hex=%2")
              .arg(data.size())
              .arg(QString(data.toHex(' ')).toUpper()),
              "darkCyan", LOG_FILE_ONLY);
    // ===== 如果正在等点火ACK，收到的第一帧就是点火回复 =====
    if (m_waitFireAck) {
        m_waitFireAck = false;
        appendLog("→ 收到点火指令ACK", "green");

        // 重新启动查询
        m_queryEnabled = true;
        m_printQueryTimer->start();
        appendLog("恢复状态查询...", "blue", LOG_FILE_ONLY);
        // 不 return，继续往下走（万一这个帧还有别的信息）
    }
    // ===== 初始化序列状态机：收到任何回复就认为是ACK，发下一条 =====
    switch (m_fpgaInitStep) {
    case FPGA_INIT_WAIT_ACK1:
        appendLog("→ 收到命令1 ACK，发送命令2...", "blue");
        m_fpgaInitStep = FPGA_INIT_SEND_CMD2;
        sendNextFpgaInitCommand();
        break;

    case FPGA_INIT_WAIT_ACK2:
        appendLog("→ 收到命令2 ACK，发送命令3...", "blue");
        m_fpgaInitStep = FPGA_INIT_SEND_CMD3;
        sendNextFpgaInitCommand();
        break;

    case FPGA_INIT_WAIT_ACK3:
        appendLog("→ 收到命令3 ACK，发送命令4...", "blue");
        m_fpgaInitStep = FPGA_INIT_SEND_CMD4;
        sendNextFpgaInitCommand();
        break;
    case FPGA_INIT_WAIT_ACK4:
        appendLog("→ 收到命令4 ACK，初始化完成！", "green");
        labelUsbStatus->setText("● 板卡就绪");
        labelUsbStatus->setStyleSheet("color: green; font-weight: bold; font-size: 12px;");
        m_fpgaInitStep = FPGA_INIT_DONE;
        break;

    default:
        break;
    }
    // ===== 打印数据发送状态机 =====
    switch (m_printStep) {
    case PRINT_WAIT_HEADER_ACK:
        appendLog("→ 收到首帧ACK，开始发送位图数据...", "blue");
        m_printStep = PRINT_SEND_DATA;
        // 发送全部位图数据
        usbManager->sendData(m_currentLayerData);
        appendLog(QString("已发送位图数据: %1字节").arg(m_currentLayerData.size()), "blue");

        // ===== 关键：如果数据大小是1024的整数倍，补一个零长度包(ZLP) =====
        // FX3收到满包会以为还有更多数据，不触发传输完成
        // 发个空包告诉它：这批数据结束了
        if (m_currentLayerData.size() % 1024 == 0) {
            QByteArray zlp;   // 空的，0字节
            usbManager->sendData(zlp);
            appendLog("已发送ZLP零包", "darkCyan", LOG_FILE_ONLY);
        }
        m_printStep = PRINT_WAIT_DATA_ACK;
        break;
    case PRINT_WAIT_DATA_ACK:
        appendLog(QString("→ 第%1层数据发送完成！").arg(m_currentLayerNum), "green");
        m_printStep = PRINT_DONE;
        // ===== 启动200ms周期查询 =====
        m_queryEnabled = true;
        m_printQueryTimer->start();
        appendLog("启动状态查询（200ms/次）...", "blue");
        break;

    default:
        break;
    }
    // ===== 解析查询回复 =====
    // 判断是不是查询回复：第3个uint32是AABR的回复？
    // 判断是不是查询回复：前4字节是 "AABR"
    if (data.size() >= 4 &&
        (quint8)data[0] == 0x41 && (quint8)data[1] == 0x41 &&
        (quint8)data[2] == 0x42 && (quint8)data[3] == 0x52) {
        parseQueryResponse(data);
    }

}


void MainWindow::onUsbError(const QString &error)
{
    appendLog(QString("USB错误: %1").arg(error), "red");
}

//void MainWindow::onUsbTestFinished(bool success)
//{
//    //btnUsbTest->setEnabled(usbManager->isConnected());
//    appendLog(success ? "USB回环通信测试通过" : "USB回环通信测试失败", success ? "green" : "red");
//}

void MainWindow::onVacuumConnectionChanged(bool connected)
{
    appendLog(connected ? "负压控制器TCP已连接 (192.168.2.6:1030)"
                        : "负压控制器TCP已断开", connected ? "green" : "orange");
}

void MainWindow::onVacuumFrameReceived(qint16 cmd, qint16 state, qint16 sdata, float fdata)
{
    Q_UNUSED(state);
    Q_UNUSED(sdata);
    // 收到负压指令解析
    switch (cmd) {
    case 0x32:  // 一级负压
        m_primaryCurrent = fdata;
        //appendLog(QString("一级负压: %1 kPa").arg(fdata, 0, 'f', 2), "darkCyan");
        break;
    case 0x33:  // 二级负压
        m_secondaryCurrent = fdata;
        //appendLog(QString("二级负压: %1 kPa").arg(fdata, 0, 'f', 2), "darkCyan");
        break;
    case 0x37:  // 循环负压
        m_cyclicCurrent = fdata;
        //appendLog(QString("循环负压: %1 kPa").arg(fdata, 0, 'f', 2), "darkCyan");
        break;
    // ===== 负压报警 =====
    case 0x46:  // 二级负压失压报警
        appendLog("⚠ 报警：二级负压失压！请检查负压管路，必要时点击[负压复位]按钮恢复。", "red");
        break;
    case 0x44:  // 循环负压失压报警
        appendLog("⚠ 报警：循环负压失压！请检查负压管路，必要时点击[负压复位]按钮恢复。", "red");
        break;
    case 0x49:  // 二级保护墨盒报警
        appendLog("⚠ 报警：二级保护墨盒进墨！请清理二级保护墨盒，清理完成后点击[负压复位]按钮恢复供墨泵。", "red");
        break;
    case 0x50:  // 循环保护墨盒报警
        appendLog("⚠ 报警：循环保护墨盒进墨！请清理循环保护墨盒，清理完成后点击[负压复位]按钮恢复供墨泵。", "red");
        break;
    case 0x61:  // 一级负压
        spinPrimaryTarget->setValue(fdata);
        //appendLog(QString("一级负压: %1 kPa").arg(fdata, 0, 'f', 2), "darkCyan");
        break;
    case 0x62:  // 二级负压
        spinSecondaryTarget->setValue(fdata);
        //appendLog(QString("二级负压: %1 kPa").arg(fdata, 0, 'f', 2), "darkCyan");
        break;
    case 0x63:  // 循环负压
        spinCyclicTarget->setValue(fdata);
        //appendLog(QString("循环负压: %1 kPa").arg(fdata, 0, 'f', 2), "darkCyan");
        break;
    // ===== 泵转速回读 =====
    case 0x80:  // 供墨泵实时转速
        if (labelInkSupplySpeed) labelInkSupplySpeed->setText(QString::number(fdata, 'f', 0));
        break;
    case 0x81:  // 回墨泵实时转速
        if (labelReturnPumpSpeed) labelReturnPumpSpeed->setText(QString::number(fdata, 'f', 0));
        break;

    default:
        // 未识别的命令码：忽略
        break;
    }

    // 更新UI
    updateNegativeUI();
//    Q_UNUSED(cmd);
//    Q_UNUSED(sdata);
//    simulatedPressure = fdata;
//    lcdNegativePressure->display(QString::number(fdata, 'f', 1));
//    appendLog(QString("负压反馈: 状态=%1, 压力=%2 kPa").arg(state).arg(fdata, 0, 'f', 2), "darkCyan");

}

void MainWindow::onVacuumError(const QString &error)
{
    appendLog(error, "red");
}

// ==================== 操控界面槽函数 ====================

//void MainWindow::on_btnStartPrint_clicked()
//{
//    if (!usbManager->isConnected()) {
//        QMessageBox::warning(this, "警告", "请先连接设备！");
//        return;
//    }

//    if (currentState == IDLE || currentState == STOPPED) {
//        usbManager->sendData("START_PRINT");
//        updatePrintState(PRINTING);
//        appendLog("开始打印任务", "green");
//        simulatedProgress = 0;
//    } else {
//        QMessageBox::information(this, "提示", "设备当前状态无法开始打印");
//    }
//}
void MainWindow::sendLayerHeader(int layerNum, int dataSize)
{
    // dataSizeInWords: 本层数据大小，单位是32位字（不是字节！）
    // 首帧命令：32字节
    // [0:4]   3DDY
    // [4:8]   DIPG  ← 注意：不是CIPG了
    // [8:12]  FDIP  ← First Data In?
    // [12:16] dataSize  ← 本层数据大小（大端）
    // [16:20] DIPG
    // [20:24] DIPG
    // [24:28] dataSize  ← 又是大小
    // [28:32] JUMP

    QByteArray frame;
    frame.resize(32);
    quint8 *b = reinterpret_cast<quint8*>(frame.data());

    // 帧头
    b[0] = 0x33; b[1] = 0x44; b[2] = 0x44; b[3] = 0x59;
    // DIPG
    b[4] = 0x44; b[5] = 0x49; b[6] = 0x50; b[7] = 0x47;
    // FDIP
    b[8] = 0x46; b[9] = 0x44; b[10] = 0x49; b[11] = 0x50;
    // dataSize (大端)
    b[12] = (dataSize >> 24) & 0xFF;
    b[13] = (dataSize >> 16) & 0xFF;
    b[14] = (dataSize >> 8) & 0xFF;
    b[15] = dataSize & 0xFF;
    // DIPG
    b[16] = 0x44; b[17] = 0x49; b[18] = 0x50; b[19] = 0x47;
    // DIPG
    b[20] = 0x44; b[21] = 0x49; b[22] = 0x50; b[23] = 0x47;
    // dataSize (大端，再来一次)
    b[24] = (dataSize >> 24) & 0xFF;
    b[25] = (dataSize >> 16) & 0xFF;
    b[26] = (dataSize >> 8) & 0xFF;
    b[27] = dataSize & 0xFF;
    // JUMP
    b[28] = 0x4A; b[29] = 0x55; b[30] = 0x4D; b[31] = 0x50;

    m_printStep = PRINT_SEND_HEADER;
    appendLog(QString("发送第%1层首帧命令: 数据大小=%2字节").arg(layerNum).arg(dataSize), "blue");
    appendLog(QString("首帧hex: %1").arg(QString(frame.toHex(' ')).toUpper()), "darkCyan", LOG_FILE_ONLY);
    usbManager->sendData(frame);
    m_printStep = PRINT_WAIT_HEADER_ACK;
}

void MainWindow::on_btnStartPrint_clicked()
{
    appendLog("=== 点击了开始打印按钮 ===", "purple");
    appendLog(QString("USB已连接: %1").arg(usbManager->isConnected()), "purple");
    appendLog(QString("初始化状态: %1 (DONE=%2)").arg(m_fpgaInitStep).arg(FPGA_INIT_DONE), "purple");

    /*
    if (!usbManager->isConnected()) {
        QMessageBox::warning(this, "警告", "请先连接设备！");
        return;
    }
    if (m_fpgaInitStep != FPGA_INIT_DONE) {
        QMessageBox::warning(this, "警告", "板卡初始化未完成，请先板卡上电！");
        return;
    }
    */
    updatePrintState(PRINTING);
    appendLog("开始打印（1024列测试）...", "green");

    // ===== 读取“处理好的数据”：layer_001.tmp.bin（排偏移 + 90°旋转后的后处理结果）=====
    if (m_sliceDir.isEmpty() || previewWidth <= 0 || previewHeight <= 0) {
        appendLog("未加载切片数据（请先打开 CLI 文件），无法发送位图", "red");
        updatePrintState(IDLE);
        return;
    }
    if (!usbManager->isConnected()) {
        appendLog("USB未连接，无法发送位图", "red");
        updatePrintState(IDLE);
        return;
    }

    int maxOffset = 0;
    const QVector<int> &offs = xingguangRankOffsets();
    for (int o : offs) maxOffset = qMax(maxOffset, o);

    // 后处理 .tmp.bin 宽 = 排偏移补0后的行数（每1024行一组，末节不足补0）
    // 需与 binrowoffset.cpp applyRowOffsetBin 的 swathRows 默认值(1024)一致。
    // 注意：不能用 SWATH_ROWS 命名——下方已有同名常量(=tmpH 打印发送行数)。
    const int GROUP_ROWS = 1024;
    const int tmpW = ((previewHeight + GROUP_ROWS - 1) / GROUP_ROWS) * GROUP_ROWS;
    const int tmpH = previewWidth + maxOffset;    // 后处理 .tmp.bin 高 = 原宽 + 416
    // 源文件位深：1 = 每像素 1bit（ceil(w/8) 字节/行），2 = 每像素 2bit（ceil(w/4) 字节/行）
    // 与切片时用的位深一致，文件尺寸校验同时就是"缓存位深是否匹配"的兜底检查
    const int srcBpp      = (previewBpp == 1) ? 1 : 2;
    const int tmpRowBytes = bin_row_bytes(tmpW, srcBpp);

    const QString binPath = m_sliceDir + "/layer_001.tmp.bin";
    QFile f(binPath);
    if (!f.open(QIODevice::ReadOnly)) {
        appendLog("无法打开后处理数据: " + binPath, "red");
        updatePrintState(IDLE);
        return;
    }
    const QByteArray src = f.readAll();
    f.close();
    if (src.size() != tmpRowBytes * tmpH) {
        appendLog(QString("后处理数据尺寸不匹配: 按 %3bit/px 期望 %1 字节, 实际 %2 字节（缓存位深与当前设置不一致，请重新打开 CLI 切片）")
                  .arg(tmpRowBytes * tmpH).arg(src.size()).arg(srcBpp), "red");
        updatePrintState(IDLE);
        return;
    }
    appendLog(QString("源数据位深: %1bit/px, 每行 %2 字节, %3 x %4 px")
              .arg(srcBpp).arg(tmpRowBytes).arg(tmpW).arg(tmpH), "darkCyan");

//    // ===== 按列发送：喷头喷孔沿 X（= .tmp.bin 的行），喷头沿 Y（= .tmp.bin 的列）运动 =====
//    // 行起点：每条火线从左边的喷孔开始（左 = .tmp.bin 最大行号 dy=tmpH-1），从左往右取 HEAD_PIXELS 行

//    const int HEAD_PIXELS = qMin(1024, tmpH);   // 一条火线能取的喷孔数（≤ 行数）
//    const int SWATH_COLS  = qMin(1024, tmpW);   // 本次发送的列数（≤ 列数）
//    //const int R0 = 0;                           // 行起点：row0 = 最右 X（从右下角取）
//    const int C0 = (tmpW > 1024) ? (tmpW - 1024) : 0;
//    const int lineBytes = (HEAD_PIXELS + 7) / 8;   // 1bpp：1024 喷孔 = 128 字节/列

//    m_currentPassPixels = SWATH_COLS;   // 记录本次条带的列数，供 FIRE 计算 1/2PASS 起止

//    m_currentLayerData.clear();
//    m_currentLayerData.reserve(lineBytes * SWATH_COLS);
//    for (int k = 0; k < SWATH_COLS; ++k) {              // 外层 = 列（运动步进）
//        const int dx = C0 + (SWATH_COLS - 1 - k);       // 从最底下列(dx=Wr-1)往上逐列
//        QByteArray line(lineBytes, 0);
//        char *lp = line.data();
//        for (int r = 0; r < HEAD_PIXELS; ++r) {         // 内层 = 行（一条火线，1024 喷孔）
//            //const int dy = R0 + r;
//            const int dy = tmpH - 1 - r;                // 从左（dy=tmpH-1）往右取喷孔
//            const int srcIdx = dy * tmpRowBytes + dx / 4;
//            const uchar v = (static_cast<uchar>(src.at(srcIdx))
//                             >> (6 - (dx % 4) * 2)) & 0x03;
//            if (v != 0) {                               // 喷印 → 置 1
//                const int shift = 7 - (r % 8);
//                lp[r / 8] = static_cast<char>(lp[r / 8] | (1 << shift));
//            }
//        }
//        m_currentLayerData.append(line);
//    }
    // 关键：每次开始打印前必须清空，否则上一次的条带会累积（字节数一次比一次大）
    m_currentLayerData.clear();

    // ===== 去程：一条火线 = tmp.bin 的一行，沿 dx(横向) 取 1024 像素；运动沿 dy(纵向) 从底行(tmpH-1)往上到顶行(0) =====
    const int HEAD_PIXELS = qMin(1024, tmpW);   // 一条火线取的像素数（一行，沿 dx 横向，≤ 列数）
    const int SWATH_ROWS  = tmpH;               // 本次发送的行数（从底行到顶行，覆盖全部）
    //const int C0 = (tmpW > 1024) ? (tmpW - 1024) : 0;   // 行宽>1024 时取右侧窗口
    const int C0 = (tmpW > 2048) ? (tmpW - 2048) : 0;    // 去程+回程整体2048像素窗口，右对齐
    // ===== 打印数据是 1bit/像素，每行字节数取决于窗口内像素数 =====
    const int lineBytes = (HEAD_PIXELS + 7) / 8;        // 1bpp：1024 像素 = 128 字节/行（与源文件位深无关）
    m_currentPassPixels = SWATH_ROWS;                   // 供 FIRE 计算 1/2PASS 起止

    // ===== 去程数据：从最底行(tmpH-1)向上，x 取右侧窗口 [C0, C0+1023] =====
    for (int k = 0; k < SWATH_ROWS; ++k) {              // 外层=行（运动步进）
        const int dy = tmpH - 1 - k;                    // 从最底行(tmpH-1)向上到顶行(0) → 右下→右上
        QByteArray line(lineBytes, 0);
        char *lp = line.data();
        // 该行在源 .tmp.bin 里的首字节；位深由 srcBpp 决定，取像素统一走 binformat.h
        const unsigned char *srcRow =
            reinterpret_cast<const unsigned char *>(src.constData()) + (size_t)dy * tmpRowBytes;
        for (int r = 0; r < HEAD_PIXELS; ++r) {
            const int dx = (C0 + 1024) + r;                      // 起点靠左，向右取 1024 像素
            if (dx >= tmpW) continue;                            // 防越界（tmpW 理论上恒 = C0+2048）
            const uchar v = bin_pixel_get(srcRow, dx, srcBpp);
            // 2bpp：非 0 即喷（1/2/3 都算）；1bpp：bit=1 即喷。最终都压成 1bit 下发
            if (bin_pixel_jet(v, srcBpp)) {
                const int shift = 7 - (r % 8);
                lp[r / 8] = static_cast<char>(lp[r / 8] | (1 << shift));
            }
        }
        m_currentLayerData.append(line);
    }

    // ===== 回程数据：从图像顶部(dy=0)开始，x 在去程窗口右边紧挨着取 1024 [C0, C0+1023]，拼接到去程数据后 =====
    for (int k = 0; k < SWATH_ROWS; ++k) {              // 外层=行（回程运动步进）
        const int dy = k;                               // 从顶行(0)向下到底行(tmpH-1) → 回程方向
        QByteArray line(lineBytes, 0);
        char *lp = line.data();
        const unsigned char *srcRow =
            reinterpret_cast<const unsigned char *>(src.constData()) + (size_t)dy * tmpRowBytes;
        for (int r = 0; r < HEAD_PIXELS; ++r) {
            //const int dx = C0 - 1 - r;                  // 向左取：去程窗口左边紧挨的 1024 像素
            // 改后：还是向右取，起点 C0
            const int dx = C0 + r;     // 去程窗口右边紧挨的 1024
            if (dx >= tmpW) continue;          // 防越界
            if (dx < 0) continue;                       // 防越界
            const uchar v = bin_pixel_get(srcRow, dx, srcBpp);
            if (bin_pixel_jet(v, srcBpp)) {              // 喷印 → 置 1
                const int shift = 7 - (r % 8);
                lp[r / 8] = static_cast<char>(lp[r / 8] | (1 << shift));
            }
        }
        m_currentLayerData.append(line);                // 回程直接拼到去程数据后面
    }

    // FX3 DDR3 要求整体(去程+回程)数据量为 2048bit(256字节) 的整数倍，不足补0
    const int FX3_ALIGN_BYTES = 2048 / 8;      // = 256 字节
    const int fx3Rem = m_currentLayerData.size() % FX3_ALIGN_BYTES;
    if (fx3Rem != 0)
        m_currentLayerData.append(QByteArray(FX3_ALIGN_BYTES - fx3Rem, 0x00));


    appendLog(QString("已提取喷头条带: 去程%1行 + 回程%1行 x %2喷孔, 共 %3 字节（源 %4bit/px → 下发 1bit/px，每线 %5 字节）")
              .arg(SWATH_ROWS).arg(HEAD_PIXELS).arg(m_currentLayerData.size())
              .arg(srcBpp).arg(lineBytes), "blue");

    m_currentLayerNum = 1;
    int sizeInWords = m_currentLayerData.size() / 4;
    sendLayerHeader(m_currentLayerNum, sizeInWords);



}


void MainWindow::on_btnPausePrint_clicked()
{
    if (currentState == PRINTING) {
        usbManager->sendData("PAUSE_PRINT");
        updatePrintState(PAUSED);
        appendLog("打印已暂停", "orange");
    } else if (currentState == PAUSED) {
        usbManager->sendData("RESUME_PRINT");
        updatePrintState(PRINTING);
        appendLog("打印已恢复", "green");
    } else {
        QMessageBox::information(this, "提示", "当前状态无法暂停/继续");
    }
}

void MainWindow::on_btnStopPrint_clicked()
{
    if (currentState == PRINTING || currentState == PAUSED) {
        usbManager->sendData("STOP_PRINT");
        updatePrintState(STOPPED);
        appendLog("打印已停止", "red");
        simulatedProgress = 0;
    }
}

void MainWindow::on_btnScan_clicked()
{
    if (!usbManager->isConnected()) {
        QMessageBox::warning(this, "警告", "请先连接设备！");
        return;
    }

    appendLog("正在扫描设备状态...", "blue");
    usbManager->sendData("SCAN_DEVICE");
    // 模拟扫描结果
    appendLog("扫描完成: 设备运行正常", "green");
}

void MainWindow::on_btnOpenCli_clicked()          // 第1951行：只保留选文件
{
    QString fileName = QFileDialog::getOpenFileName(this,
        "选择CLI切片文件", QString(), "CLI文件 (*.cli);;所有文件 (*)");

    if (fileName.isEmpty()) return;

    openCliFile(fileName);
}

// 打开cli文件
void MainWindow::openCliFile(const QString &fileName)   // 第1961行：新函数，含原主体
{
    appendLog(QString("打开CLI文件: %1").arg(fileName), "blue");
    labelCliFile->setText(QString("模型: %1").arg(QFileInfo(fileName).fileName()));

    // ===== 记住上次打开的CLI文件，下次启动自动打开 =====
    saveParams->saveLastCliFile(fileName);
    // ===== 切片缓存目录：按文件名+大小+DPI+位深命名，避免重复切片、也避免拿到位深不匹配的旧缓存 =====
    // 末尾 _binv4：位深可切换后，缓存名追加 _bppN（旧 _binv3 缓存不会命中，会重切一次）
    QFileInfo fi(fileName);
    QString cacheDir = QDir::homePath() + "/.cache/BJPrinter/slices/";
    //QString cacheName = fi.completeBaseName() + "_" + QString::number(fi.size());
    QString cacheName = fi.completeBaseName() + "_" + QString::number(fi.size())
                        + "_dpi" + QString::number(spinDpi->value())
                        + "_binv4_bpp" + QString::number(currentPrintParams.bitsPerPixel);
    QString sliceDir = cacheDir + cacheName;

    // ===== 缓存命中且带元数据 → 直接恢复界面，不用重切 =====
    QDir dir(sliceDir);
    if (dir.exists("layer_001.bin") && dir.exists("meta.txt")) {
        int layerCount = 0, width = 0, height = 0, rowBytes = 0, metaBpp = 2;
        int origW = 0, origH = 0, procW = 0, procH = 0;   // ← 新元数据长宽
        QFile metaFile(sliceDir + "/meta.txt");
        if (metaFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&metaFile);
            in >> layerCount >> width >> height >> rowBytes >> metaBpp;   // 第5个数=位深
            if (!in.atEnd()) in >> origW >> origH >> procW >> procH;   // ← 新增行2
        }
        // 位深只可能是 1/2；行字节数必须与位深吻合，否则说明元数据坏了 → 重切
        metaBpp = (metaBpp == 1) ? 1 : 2;
        const bool metaOk = (layerCount > 0 && width > 0 && height > 0 &&
                             rowBytes == bin_row_bytes(width, metaBpp));
        if (metaOk) {
            appendLog(QString("发现切片缓存，直接加载（%1bit/px）").arg(metaBpp), "green");
            currentCliFile = fileName;
            m_sliceDir = sliceDir;
            applySliceResult(layerCount, width, height, rowBytes, metaBpp);
            return;
        }
        // 元数据损坏 → 落到下面重新切片
    }

    // ===== 无缓存 / 缓存缺元数据 → 重新切片 =====
    QDir().mkpath(sliceDir);
    appendLog("开始切片...", "blue");

    //cliWorker = new CliFillWorker(fileName, sliceDir, currentPrintParams.dpi);   // dpi修改
    cliWorker = new CliFillWorker(fileName, sliceDir, spinDpi->value(),
                                  currentPrintParams.bitsPerPixel);        //  不用先点 "应用打印参数" 也生效；位深随设置走

    cliThread = new QThread;
    cliWorker->moveToThread(cliThread);
    connect(cliThread, &QThread::started, cliWorker, &CliFillWorker::run);
    connect(cliWorker, &CliFillWorker::finished, this, &MainWindow::onCliConversionFinished);
    connect(cliWorker, &CliFillWorker::finished, cliThread, &QThread::quit);
    connect(cliThread, &QThread::finished, cliWorker, &QObject::deleteLater);
    connect(cliThread, &QThread::finished, cliThread, &QObject::deleteLater);
    cliThread->start();

    currentCliFile = fileName;
    m_sliceDir = sliceDir;
}

// ==================== 切片完成：落盘元数据 + 恢复界面 ====================
void MainWindow::applySliceResult(int layerCount, int width, int height, int rowBytes, int bpp)
{
    previewLayerCount = layerCount;
    previewWidth      = width;
    previewHeight     = height;
    previewBpp        = (bpp == 1) ? 1 : 2;   // 预览/发送都按这个位深读 layer_NNN.bin
    currentPreviewLayer = 1;

    // 更新界面
    // 原图 = 原始切片画布 width x height；处理 = 排偏移(每1024行分组,末节补0)+旋转后 procH x (width+416)
     const QVector<int> &offs = xingguangRankOffsets();   // 新增变量块
     int maxOffset = 0;
     for (int o : offs) maxOffset = qMax(maxOffset, o);
     // 处理宽度 = 补0后的行数：ceil(height/1024)*1024（与 applyRowOffsetBin 的 swathRows 一致）
     const int GROUP_ROWS = 1024;
     const int procW = ((height + GROUP_ROWS - 1) / GROUP_ROWS) * GROUP_ROWS;
     labelCliFile->setText(QString("模型: %1\n原图: %2 x %3 px\n处理: %4 x %5 px\n位深: %6bit/px，每行 %7 字节")   // 改写
                           .arg(QFileInfo(currentCliFile).fileName())
                           .arg(width).arg(height)
                            .arg(procW).arg(width + maxOffset)
                            .arg(previewBpp)
                            .arg(bin_row_bytes(width, previewBpp)));

    labelLayerInfo->setText(QString("层数: %1/%2").arg(1).arg(layerCount));

    // 启用上一层/下一层
    btnPreviousLayer->setEnabled(true);
    btnNextLayer->setEnabled(layerCount > 1);

    // 显示第 1 层预览
    updatePrintPreview(currentPreviewLayer);

    appendLog(QString("CLI切片完成: %1层，画布%2 x %3像素，每行%4字节，%5bit/pixel")
              .arg(layerCount).arg(width).arg(height).arg(rowBytes)
              .arg(previewBpp), "green");
}

void MainWindow::onCliConversionFinished(int status, int layerCount, int width,
                                         int height, int rowBytes, int bpp)
{
    statusBar()->clearMessage();
    btnOpenCli->setEnabled(true);

    if (status != 0) {
        const QString detail = status == -4
            ? "切片文件写入失败，请检查磁盘剩余空间和缓存目录权限"
            : QString("CLI文件转换失败，错误码: %1").arg(status);
        QMessageBox::critical(this, "切片失败", detail);
        appendLog(detail, "red");
        return;
    }
    // ===== 把切片元数据落盘，供下次"缓存命中"恢复界面 =====
    // 格式（纯数字，流式读取，向后兼容旧缓存）：
    //   layerCount / width / height / rowBytes / bpp     ← 第5个是位深（1 或 2）
    //   origW origH
    //   procW procH
    int maxOffset = 0;                                    //新增变量块
    const QVector<int> &offs = xingguangRankOffsets();
    for (int o : offs) maxOffset = qMax(maxOffset, o);
    // 处理宽 = 排偏移补0后的行数（每1024行一组，末节不足补0）
    const int GROUP_ROWS = 1024;
    const int procW = ((height + GROUP_ROWS - 1) / GROUP_ROWS) * GROUP_ROWS;
    const int procH = width + maxOffset;   // 高 = 原宽 + 416
    const int outBpp = (bpp == 1) ? 1 : 2;

    QFile meta(m_sliceDir + "/meta.txt");
    if (meta.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&meta);
        out << layerCount << "\n" << width << "\n" << height << "\n" << rowBytes << "\n" << outBpp << "\n";
        out << width << " " << height << "\n";            // ← 新增：原图尺寸
        out << procW << " " << procH << "\n";             // ← 新增：处理后尺寸
    }


    applySliceResult(layerCount, width, height, rowBytes, outBpp);
}

void MainWindow::on_btnPreviousLayer_clicked()
{
    if (currentPreviewLayer > 1) updatePrintPreview(currentPreviewLayer - 1);
}

void MainWindow::on_btnNextLayer_clicked()
{
    if (currentPreviewLayer < previewLayerCount) updatePrintPreview(currentPreviewLayer + 1);
}

void MainWindow::on_btnEmergencyStop_clicked()
{
    if (usbManager->isConnected()) {
        usbManager->sendData("EMERGENCY_STOP");
    }
    updatePrintState(ERROR);
    appendLog("紧急停止！", "red");
    QMessageBox::critical(this, "紧急停止", "系统已紧急停止！");
}


// ==================== 波形控制槽函数 ====================

void MainWindow::on_btnApplyWaveform_clicked()
{
    currentWaveform.voltage = spinVoltage->value();
    currentWaveform.riseTime = spinRiseTime->value();
    currentWaveform.dwellTime = spinDwellTime->value();
    currentWaveform.fallTime = spinFallTime->value();
    currentWaveform.frequency = spinFrequency->value();

    // 保存波形参数
    saveParams->saveWaveformParams(currentWaveform);
    appendLog(QString("应用波形: V=%1V, Rise=%2μs, Dwell=%3μs, Fall=%4μs, Freq=%5kHz")
              .arg(currentWaveform.voltage)
              .arg(currentWaveform.riseTime)
              .arg(currentWaveform.dwellTime)
              .arg(currentWaveform.fallTime)
              .arg(currentWaveform.frequency), "blue");

    labelWaveformStatus->setText("● 已加载");
    labelWaveformStatus->setStyleSheet("color: green; font-weight: bold;");

    if (usbManager->isConnected()) {
        QByteArray data;
        data.append(static_cast<char>(0x20));
        data.append(reinterpret_cast<const char*>(&currentWaveform), sizeof(WaveformData));
        usbManager->sendData(data);
    }

    drawWaveformPreview();
}

void MainWindow::on_btnLoadWaveform_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    "加载波形配置文件",
                                                    QString(),
                                                    "波形文件 (*.json);;所有文件 (*)");
    if (!fileName.isEmpty()) {
        appendLog(QString("加载波形文件: %1").arg(fileName), "blue");
        // TODO: 解析波形文件并更新参数
    }
}

void MainWindow::on_btnSaveWaveform_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    "保存波形配置",
                                                    QString(),
                                                    "波形文件 (*.json);;所有文件 (*)");
    if (!fileName.isEmpty()) {
        appendLog(QString("保存波形文件: %1").arg(fileName), "blue");
        // TODO: 保存当前波形参数
    }
}

void MainWindow::on_btnTestJet_clicked()
{
    if (!usbManager->isConnected()) {
        QMessageBox::warning(this, "警告", "请先连接设备！");
        return;
    }

    appendLog(QString("执行喷头测试: %1").arg(comboTestPattern->currentText()), "purple");
    usbManager->sendData("TEST_JET");
}

// ==================== 运动参数槽函数 ====================

void MainWindow::on_btnSetMotionParams_clicked()
{
    currentMotionParams.speedX = spinSpeedX->value();      // ← 修改
    currentMotionParams.speedY = spinSpeedY->value();
    currentMotionParams.speedZ = spinSpeedZ->value();
    currentMotionParams.acceleration = spinAcceleration->value();
    currentMotionParams.jerk = spinJerk->value();
    currentMotionParams.layerHeight = spinLayerHeight->value();

    // 保存到文件
    saveParams->saveMotionParams(currentMotionParams);
    appendLog(QString("运动参数已保存: X速度=%1mm/s, Y速度=%2mm/s, Z速度=%3mm/s, 加速度=%4mm/s², Jerk=%5, 层厚=%6mm")
                  .arg(currentMotionParams.speedX)
                  .arg(currentMotionParams.speedY)
                  .arg(currentMotionParams.speedZ)
                  .arg(currentMotionParams.acceleration)
                  .arg(currentMotionParams.jerk)
                  .arg(currentMotionParams.layerHeight), "blue");
    // 这里应该发送至plc或运动控制单元，不能发送至打印板卡
    if (usbManager->isConnected()) {
        QByteArray data;
        data.append(static_cast<char>(0x30));
        data.append(reinterpret_cast<const char*>(&currentMotionParams), sizeof(MotionParams));
        usbManager->sendData(data);
    }
}

void MainWindow::on_btnHomeAll_clicked()
{
    if (!usbManager->isConnected()) {
        QMessageBox::warning(this, "警告", "请先连接设备！");
        return;
    }

    appendLog("正在归零所有轴...", "purple");
    usbManager->sendData("HOME_ALL");
}

void MainWindow::on_btnMoveTo_clicked()
{
    if (!usbManager->isConnected()) {
        QMessageBox::warning(this, "警告", "请先连接设备！");
        return;
    }

    double x = spinPosX->value();
    double y = spinPosY->value();
    double z = spinPosZ->value();

    appendLog(QString("移动到: X=%1, Y=%2, Z=%3").arg(x).arg(y).arg(z), "purple");

    QByteArray data;
    data.append(static_cast<char>(0x32));
    data.append(reinterpret_cast<const char*>(&x), sizeof(double));
    data.append(reinterpret_cast<const char*>(&y), sizeof(double));
    data.append(reinterpret_cast<const char*>(&z), sizeof(double));
    usbManager->sendData(data);

    labelCurrentPos->setText(QString("X: %1  Y: %2  Z: %3  mm").arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(z, 0, 'f', 2));
}

void MainWindow::on_btnSetOrigin_clicked()
{
    if (!usbManager->isConnected()) {
        QMessageBox::warning(this, "警告", "请先连接设备！");
        return;
    }

    appendLog("设置当前位置为原点", "purple");
    usbManager->sendData("SET_ORIGIN");
}

// ==================== 状态更新 ====================



//void MainWindow::updateStatus()
//{
//    // 模拟温度变化
//    simulatedTemp += (QRandomGenerator::global()->generateDouble() - 0.5) * 0.5;
//    if (simulatedTemp < 20) simulatedTemp = 20;
//    if (simulatedTemp > 60) simulatedTemp = 60;

//    // 负压控制器在线时，当前值由 TCP 反馈更新；离线时保留界面演示值。
//    if (!vacuumController->isConnected()) {
//        if (currentNegativePressure.enabled) {
//            simulatedPressure += (QRandomGenerator::global()->generateDouble() - 0.5) * 2.0;
//            if (simulatedPressure > -30) simulatedPressure = -35;
//            if (simulatedPressure < -70) simulatedPressure = -65;
//        } else {
//            simulatedPressure = -10.0;
//        }
//    }

//    // 模拟打印进度
//    if (currentState == PRINTING) {
//        simulatedProgress += QRandomGenerator::global()->bounded(2);
//        if (simulatedProgress > 100) {
//            simulatedProgress = 100;
//            updatePrintState(IDLE);
//            appendLog("打印完成！", "green");
//        }
//    }

//    // 更新UI
//    lcdTemperature->display(QString::number(simulatedTemp, 'f', 1));
//    lcdNegativePressure->display(QString::number(simulatedPressure, 'f', 1));
//    progressPrint->setValue(simulatedProgress);
//    labelProgress->setText(QString("%1%").arg(simulatedProgress));
//}

// ==================== 辅助函数 ====================

void MainWindow::updatePrintState(PrintState state)
{
    currentState = state;

    btnStartPrint->setEnabled(state == IDLE || state == STOPPED);
    btnPausePrint->setEnabled(state == PRINTING || state == PAUSED);
    btnStopPrint->setEnabled(state == PRINTING || state == PAUSED);

    switch (state) {
    case IDLE:
        labelPrintStateText->setText("空闲");
        labelPrintStateText->setStyleSheet("color: gray; font-weight: bold; font-size: 14px;");
        btnPausePrint->setText("⏸ 暂停");
        break;
    case PRINTING:
        labelPrintStateText->setText("打印中");
        labelPrintStateText->setStyleSheet("color: green; font-weight: bold; font-size: 14px;");
        btnPausePrint->setText("⏸ 暂停");
        break;
    case PAUSED:
        labelPrintStateText->setText("已暂停");
        labelPrintStateText->setStyleSheet("color: orange; font-weight: bold; font-size: 14px;");
        btnPausePrint->setText("▶ 继续");
        break;
    case STOPPED:
        labelPrintStateText->setText("已停止");
        labelPrintStateText->setStyleSheet("color: red; font-weight: bold; font-size: 14px;");
        btnPausePrint->setText("⏸ 暂停");
        break;
    case ERROR:
        labelPrintStateText->setText("错误");
        labelPrintStateText->setStyleSheet("color: red; font-weight: bold; font-size: 14px;");
        break;
    }
}

void MainWindow::updateUsbStatus(bool connected)
{

    btnScan->setEnabled(connected);
    btnStartPrint->setEnabled(connected && (currentState == IDLE || currentState == STOPPED));

//    QString status = connected ? "● 已连接" : "○ 未连接";
//    QString color = connected ? "green" : "gray";
//    labelUsbStatus->setText(status);
//    labelUsbStatus->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 12px;").arg(color));
}

void MainWindow::appendLog(const QString &message, const QString &color,
                           LogTarget target)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    QString textLine = QString("[%1] %2").arg(timestamp).arg(message);

    // 颜色名 → ANSI 颜色码
    QString ansiColor;
    if (color == "red")         ansiColor = "\033[31m";      // 红
    else if (color == "green")  ansiColor = "\033[32m";      // 绿
    else if (color == "yellow") ansiColor = "\033[33m";      // 黄
    else if (color == "blue")   ansiColor = "\033[34m";      // 蓝
    else if (color == "magenta") ansiColor = "\033[35m";     // 紫
    else if (color == "darkCyan") ansiColor = "\033[36m";    // 青
    else if (color == "orange") ansiColor = "\033[33m";      // 橙（用黄色代替）
    else if (color == "gray" || color == "darkGray") ansiColor = "\033[90m";  // 灰
    else ansiColor = "\033[0m";                               // 默认
    // 1. 写文件（带 ANSI 颜色码）
    if (target == LOG_BOTH || target == LOG_FILE_ONLY) {
        *m_logStream << ansiColor << textLine << "\033[0m" << Qt::endl;
        m_logStream->flush();
    }

    // 2. 显示界面
    if (target == LOG_BOTH || target == LOG_UI_ONLY) {
        QString html = QString("<span style='color:%1;'>[%2] %3</span>")
                       .arg(color).arg(timestamp).arg(message);
        textLog->append(html);
        textLog->moveCursor(QTextCursor::End);
    }
}


void MainWindow::updatePrintPreview(int layer)
{

    if (layer < 1 || layer > previewLayerCount || m_sliceDir.isEmpty()) return;
    const QString binPath = m_sliceDir + QString("/layer_%1.bin").arg(layer, 3, 10, QChar('0'));

    QFile binFile(binPath);
    const int maxPreviewSize = 1600;
    const double scale = qMin(1.0, maxPreviewSize /
                              static_cast<double>(qMax(previewWidth, previewHeight)));
    const int imageWidth = qMax(1, qRound(previewWidth * scale));
    const int imageHeight = qMax(1, qRound(previewHeight * scale));
    QImage image(imageWidth, imageHeight, QImage::Format_Grayscale8);
    image.fill(255);
    const int bpp = (previewBpp == 1) ? 1 : 2;
    const int rowBytes = bin_row_bytes(previewWidth, bpp);       // 1bpp=ceil(w/8)，2bpp=ceil(w/4)
    const int expectedSize = rowBytes * previewHeight;
    if (previewWidth <= 0 || previewHeight <= 0 ||
        !binFile.open(QIODevice::ReadOnly) || binFile.size() != expectedSize) {
        appendLog(QString("无法加载第%1层预览（期望 %2 x %3 px, %4bit/px = %5 字节）")
                  .arg(layer).arg(previewWidth).arg(previewHeight).arg(bpp).arg(expectedSize), "red");
        return;
    }
    const QByteArray data = binFile.read(expectedSize);
    for (int y = 0; y < imageHeight; ++y) {
        const int sourceRow = qMin(previewHeight - 1,
                                   qRound(y / scale));
        uchar *destination = image.scanLine(imageHeight - y - 1);
        const unsigned char *srcRow = reinterpret_cast<const unsigned char *>(data.constData())
                                      + (size_t)sourceRow * rowBytes;
        for (int x = 0; x < imageWidth; ++x) {
            const int sourceX = qMin(previewWidth - 1, qRound(x / scale));
            const uchar value = bin_pixel_get(srcRow, sourceX, bpp);
            destination[x] = (bpp == 1)
                             ? (value ? 0 : 255)                       // 1bpp：喷=黑
                             : (value == 0 ? 255 : static_cast<uchar>(255 - value * 70));
        }
    }
    // [开发用] 同目录导出一份可双击预览的 PNG，后期可删
    image.save(m_sliceDir + QString("/layer_%1.png").arg(layer, 3, 10, QChar('0')));
    const QPixmap pixmap = QPixmap::fromImage(image);

    currentPreviewLayer = layer;
    printScene->clear();
    printScene->addPixmap(pixmap);
    printScene->setSceneRect(pixmap.rect());
    printPreview->fitInView(printScene->sceneRect(), Qt::KeepAspectRatio);
    labelLayerInfo->setText(QString("%1/%2").arg(layer).arg(previewLayerCount));
    btnPreviousLayer->setEnabled(layer > 1);
    btnNextLayer->setEnabled(layer < previewLayerCount);
}

void MainWindow::drawWaveformPreview()
{
    waveformScene->clear();
    QPen pen(Qt::blue);
    pen.setWidth(2);

    int width = waveformPreview->width() - 20;
    int height = waveformPreview->height() - 20;
    if (width < 100) width = 600;
    if (height < 100) height = 350;

    // 绘制波形
    QPainterPath path;
    double centerY = height / 2;
    double amplitude = height * 0.4;
    double period = width / 5;

    for (int i = 0; i <= width; i += 2) {
        double x = i;
        double y = centerY - amplitude * std::sin(2 * 3.14159 * i / period);
        if (i == 0) {
            path.moveTo(x + 10, y + 10);
        } else {
            path.lineTo(x + 10, y + 10);
        }
    }

    waveformScene->addPath(path, pen);

    // 添加坐标轴
    QPen axisPen(Qt::gray);
    axisPen.setWidth(1);
    waveformScene->addLine(10, height / 2 + 10, width + 10, height / 2 + 10, axisPen);
}

void MainWindow::drawPressureChart()
{
    if (!pressureScene || !pressureChart) return;
    pressureScene->clear();

    // 获取当前视图的有效尺寸
    int width = pressureChart->width() - 40;
    int height = pressureChart->height() - 40;
    // 确保最小尺寸
    if (width < 50) width = 500;
    if (height < 50) height = 180;
    // 调试输出
     qDebug() << "drawPressureChart: width=" << width << "height=" << height;
    // 获取当前值
    double primaryVal = m_primaryCurrent;
    double secondaryVal = m_secondaryCurrent;
    double cyclicVal = m_cyclicCurrent;

    // 如果没有历史数据，使用当前值初始化
    if (m_primaryHistory.isEmpty()) {
        for (int i = 0; i < MAX_HISTORY; ++i) {
            m_primaryHistory.append(primaryVal);
            m_secondaryHistory.append(secondaryVal);
            m_cyclicHistory.append(cyclicVal);
        }
    }
    // 确保历史数据大小一致
        while (m_primaryHistory.size() > MAX_HISTORY) {
            m_primaryHistory.removeFirst();
            m_secondaryHistory.removeFirst();
            m_cyclicHistory.removeFirst();
        }
        while (m_primaryHistory.size() < MAX_HISTORY) {
            m_primaryHistory.append(primaryVal);
            m_secondaryHistory.append(secondaryVal);
            m_cyclicHistory.append(cyclicVal);
        }

    // ----- 绘制网格和坐标轴 -----
    QPen gridPen(Qt::gray, 1, Qt::DashLine);
    gridPen.setStyle(Qt::DashLine);

    // 水平网格线 (0, -20, -40, -60, -80 kPa)
    for (int p = 0; p >= -10; p -= 2) {
        // 映射压力值到Y坐标 (范围: -80 ~ 0 kPa)
        double y = height - ((p - (-10)) / 10.0 * height) + 20;
        if (y > 20 && y < height + 20) {
            pressureScene->addLine(20, y, width + 20, y, gridPen);
            // 添加标签
            QGraphicsSimpleTextItem *text = new QGraphicsSimpleTextItem(QString("%1").arg(p));
            text->setPos(2, y - 6);
            text->setFont(QFont("Arial", 8));
            pressureScene->addItem(text);
        }
    }

    // ----- 绘制三条曲线 -----
//    QPen pen1(QColor(33, 150, 243));   // 蓝色 - 一级负压
//    pen1.setWidth(2);
    QPen pen2(QColor(255, 152, 0));    // 橙色 - 二级负压
    pen2.setWidth(2);
    QPen pen3(QColor(76, 175, 80));    // 绿色 - 循环负压
    pen3.setWidth(2);

    QPainterPath path2, path3;



    int dataSize = m_primaryHistory.size();
    if (dataSize < 2) return;

    double xStep = static_cast<double>(width) / (dataSize - 1);

    for (int i = 0; i < dataSize; ++i) {
        double x = 20 + i * xStep;

        // 映射压力值到Y坐标 (范围: -80 ~ 0 kPa)
        //double y1 = height - ((m_primaryHistory[i] - (-20)) / 20 * height) + 20;
        double y2 = height - ((m_secondaryHistory[i] - (-10)) / 10 * height) + 20;
        double y3 = height - ((m_cyclicHistory[i] - (-10)) / 10 * height) + 20;

        // 限制范围
        //y1 = qBound(20.0, y1, height + 20.0);
        y2 = qBound(20.0, y2, height + 20.0);
        y3 = qBound(20.0, y3, height + 20.0);

        if (i == 0) {
            //path1.moveTo(x, y1);
            path2.moveTo(x, y2);
            path3.moveTo(x, y3);
        } else {
            //path1.lineTo(x, y1);
            path2.lineTo(x, y2);
            path3.lineTo(x, y3);
        }
    }

    //pressureScene->addPath(path1, pen1);
    pressureScene->addPath(path2, pen2);
    pressureScene->addPath(path3, pen3);

    // 添加坐标轴
    QPen axisPen(Qt::black);
    axisPen.setWidth(1);
    pressureScene->addLine(20, 20, 20, height + 20, axisPen);  // Y轴
    pressureScene->addLine(20, height + 20, width + 20, height + 20, axisPen);  // X轴

    // Y轴标签

    QGraphicsSimpleTextItem *label0 = new QGraphicsSimpleTextItem("0");
        label0->setPos(22, 8);
        label0->setFont(QFont("Arial", 8));
        pressureScene->addItem(label0);

        QGraphicsSimpleTextItem *label80 = new QGraphicsSimpleTextItem("-10");
        label80->setPos(15, height + 12);
        label80->setFont(QFont("Arial", 8));
        pressureScene->addItem(label80);

        m_firstDraw = false;

}

//QGroupBox* MainWindow::createGroupBox(const QString &title)
//{
//    QGroupBox *group = new QGroupBox(title, this);
//    group->setStyleSheet("QGroupBox { font-weight: bold; border: 1px solid #ccc; border-radius: 5px; margin-top: 10px; }"
//                         "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
//    return group;
//}
QGroupBox* MainWindow::createGroupBox(const QString &title)
{
    QGroupBox *group = new QGroupBox(title, this);
    group->setStyleSheet("QGroupBox { font-weight: bold; border: 1px solid #ccc; border-radius: 5px; margin-top: 10px; }"
                         "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    return group;
}

void MainWindow::addWidgetToLayout(QLayout *layout, QWidget *widget, int row, int col, int rowSpan, int colSpan)
{
    Q_UNUSED(layout);
    Q_UNUSED(widget);
    Q_UNUSED(row);
    Q_UNUSED(col);
    Q_UNUSED(rowSpan);
    Q_UNUSED(colSpan);
}

// ==================== 硬件控制槽函数 ====================
void MainWindow::on_btnMotorPower_toggled(bool checked)
{
    if (checked) {
        motorPowered = true;
        btnMotorPower->setText("⚡ 电机断电");
        btnMotorPower->setStyleSheet(
            "QPushButton {"
            "    background-color: #4CAF50;"
            "    color: white;"
            "    font-weight: bold;"
            "    font-size: 12px;"
            "    border-radius: 4px;"
            "}"
            "QPushButton:hover {"
            "    background-color: #388E3C;"
            "}"
        );
        appendLog("电机已上电", "green");

        // 发送上电命令到驱动板
        if (usbManager->isConnected()) {
            usbManager->sendData("MOTOR_POWER_ON");
        }
    } else {
        motorPowered = false;
        btnMotorPower->setText("⚡ 电机上电");
        btnMotorPower->setStyleSheet(
            "QPushButton {"
            "    background-color: #FF9800;"
            "    color: white;"
            "    font-weight: bold;"
            "    font-size: 12px;"
            "    border-radius: 4px;"
            "}"
            "QPushButton:hover {"
            "    background-color: #F57C00;"
            "}"
        );
        appendLog("电机已断电", "orange");

        if (usbManager->isConnected()) {
            usbManager->sendData("MOTOR_POWER_OFF");
        }
    }
}

//void MainWindow::on_btnBoardPower_toggled(bool checked)
//{
//    if (checked) {
//        boardPowered = true;
//        btnBoardPower->setText("🔌 板卡断电");
//        btnBoardPower->setStyleSheet(
//            "QPushButton {"
//            "    background-color: #4CAF50;"
//            "    color: white;"
//            "    font-weight: bold;"
//            "    font-size: 12px;"
//            "    border-radius: 4px;"
//            "}"
//            "QPushButton:hover {"
//            "    background-color: #388E3C;"
//            "}"
//        );
//        appendLog("板卡已上电", "green");

//        if (usbManager->isConnected()) {
//            usbManager->sendData("BOARD_POWER_ON");
//        }
//    } else {
//        boardPowered = false;
//        btnBoardPower->setText("🔌 板卡上电");
//        btnBoardPower->setStyleSheet(
//            "QPushButton {"
//            "    background-color: #2196F3;"
//            "    color: white;"
//            "    font-weight: bold;"
//            "    font-size: 12px;"
//            "    border-radius: 4px;"
//            "}"
//            "QPushButton:hover {"
//            "    background-color: #1976D2;"
//            "}"
//        );
//        appendLog("板卡已断电", "orange");

//        if (usbManager->isConnected()) {
//            usbManager->sendData("BOARD_POWER_OFF");
//        }
//    }
//}


//void MainWindow::on_btnBoardPower_toggled(bool checked)
//{
//    if (checked) {
//        boardPowered = true;
//        btnBoardPower->setText("🔌 板卡断电");
//        btnBoardPower->setStyleSheet(
//            "QPushButton {"
//            "    background-color: #4CAF50;"
//            "    color: white;"
//            "    font-weight: bold;"
//            "    font-size: 12px;"
//            "    border-radius: 4px;"
//            "}"
//            "QPushButton:hover {"
//            "    background-color: #388E3C;"
//            "}"
//        );
//        appendLog("板卡已上电，等待FX3稳定...", "green");

//        if (usbManager->isConnected()) {
//            // 先不发字符串了，启动定时器延迟500ms后开始初始化序列
//            m_fpgaInitStep = FPGA_INIT_IDLE;
//            m_fpgaInitTimer->start();
//        } else {
//            appendLog("USB未连接，板卡上电失败", "red");
//        }
//    } else {
//        boardPowered = false;
//        btnBoardPower->setText("🔌 板卡上电");
//        btnBoardPower->setStyleSheet(
//            "QPushButton {"
//            "    background-color: #2196F3;"
//            "    color: white;"
//            "    font-weight: bold;"
//            "    font-size: 12px;"
//            "    border-radius: 4px;"
//            "}"
//            "QPushButton:hover {"
//            "    background-color: #1976D2;"
//            "}"
//        );
//        appendLog("板卡已断电", "orange");
//        m_fpgaInitStep = FPGA_INIT_IDLE;

//        if (usbManager->isConnected()) {
//            usbManager->sendData("BOARD_POWER_OFF");
//        }
//    }
//}

void MainWindow::on_btnBoardPower_toggled(bool checked)
{
    if (checked) {
        boardPowered = true;
        btnBoardPower->setText("🔌 板卡断电");
        btnBoardPower->setStyleSheet(
            "QPushButton {"
            "    background-color: #4CAF50;"
            "    color: white;"
            "    font-weight: bold;"
            "    font-size: 12px;"
            "    border-radius: 4px;"
            "}"
            "QPushButton:hover {"
            "    background-color: #388E3C;"
            "}"
        );
        labelUsbStatus->setText("● 正在连接板卡...");
        labelUsbStatus->setStyleSheet("color: orange; font-weight: bold; font-size: 12px;");
        appendLog("板卡上电：正在连接USB设备...", "green");

        // ===== 自动连接USB =====
        if (!usbManager->isConnected()) {
            usbManager->connectDevice();
            // 连接成功后会在 onUsbStatusChanged 里启动初始化序列
        } else {
            // 已经连了，直接启动初始化
            appendLog("USB已连接，500ms后开始FX3初始化...", "blue");
            m_fpgaInitStep = FPGA_INIT_IDLE;
            m_fpgaInitTimer->start();
        }
    } else {
        boardPowered = false;
        btnBoardPower->setText("🔌 板卡上电");
        btnBoardPower->setStyleSheet(
            "QPushButton {"
            "    background-color: #2196F3;"
            "    color: white;"
            "    font-weight: bold;"
            "    font-size: 12px;"
            "    border-radius: 4px;"
            "}"
            "QPushButton:hover {"
            "    background-color: #1976D2;"
            "}"
        );
        labelUsbStatus->setText("● 板卡未上电");
        labelUsbStatus->setStyleSheet("color: gray; font-weight: bold; font-size: 12px;");
        appendLog("板卡已断电", "orange");
        m_fpgaInitStep = FPGA_INIT_IDLE;
        m_fpgaInitTimer->stop();

        // 断开USB
        usbManager->disconnectDevice();
    }
}




void MainWindow::on_btnReset_clicked()
{
    appendLog("执行系统复位...", "purple");

    // 复位操作：先断电再上电
    if (usbManager->isConnected()) {
        usbManager->sendData("SYSTEM_RESET");
    }

    // 重置状态
    if (btnMotorPower->isChecked()) {
        btnMotorPower->setChecked(false);
    }
    if (btnBoardPower->isChecked()) {
        btnBoardPower->setChecked(false);
    }

    // 重置运动参数到默认值
    spinSpeedX->setValue(300);
    spinSpeedY->setValue(300);
    spinSpeedZ->setValue(50);
    spinAcceleration->setValue(1000);
    spinJerk->setValue(5);
    spinLayerHeight->setValue(0.1);

    appendLog("系统复位完成", "green");
}
/**
 * @brief 设置一级负压目标值
 */
void MainWindow::on_btnSetPrimaryPressure_clicked()
{
    currentNegativeParams.primary.target = spinPrimaryTarget->value();
    saveParams->saveNegativePressure(currentNegativeParams);
    // 下发设置命令：cmd=0x01 设置一级目标值（fdata=目标值）
    vacuumController->sendCommand(0x01, 0, 0, currentNegativeParams.primary.target);
    appendLog(QString("一级负压目标值: %1 kPa").arg(currentNegativeParams.primary.target), "blue");
}

/**
 * @brief 设置二级负压目标值
 */
void MainWindow::on_btnSetSecondaryPressure_clicked()
{
    currentNegativeParams.secondary.target = spinSecondaryTarget->value();
    saveParams->saveNegativePressure(currentNegativeParams);
    // 下发设置命令：cmd=0x04 设置二级目标值（fdata=目标值）
    vacuumController->sendCommand(0x04, 0, 0, currentNegativeParams.secondary.target);
    appendLog(QString("二级负压目标值: %1 kPa").arg(currentNegativeParams.secondary.target), "blue");

}

/**
 * @brief 设置循环负压目标值
 */
void MainWindow::on_btnSetCyclicPressure_clicked()
{
    currentNegativeParams.cyclic.target = spinCyclicTarget->value();
    saveParams->saveNegativePressure(currentNegativeParams);
    // 下发设置命令：cmd=0x06 设置循环目标值（fdata=目标值）
    vacuumController->sendCommand(0x06, 0, 0, currentNegativeParams.cyclic.target);
    appendLog(QString("循环负压目标值: %1 kPa").arg(currentNegativeParams.cyclic.target), "blue");

}
// 废弃
void MainWindow::sendNegativeCommand(int channel, bool enable, double target)
{
    // cmd: 0x01=一级负压, 0x02=二级负压, 0x03=循环负压
    qint16 cmd = static_cast<qint16>(0x01 + channel);
    qint16 state = enable ? 1 : 0;
    qint16 sdata = static_cast<qint16>(target * 10);  // 保留一位小数
    float fdata = static_cast<float>(target);

    vacuumController->sendCommand(cmd, state, sdata, fdata);
}
/**
 * @brief 二级压墨：cmd=0x20, state=0, sdata=压墨时间, fdata=释放时间
 */
void MainWindow::on_btnSecondaryInkPress_clicked()
{
    qint16 inkTime = static_cast<qint16>(spinSecondaryInkTime->value());
    float releaseTime = static_cast<float>(spinSecondaryReleaseTime->value());

    // 保存压墨/释放时间到配置
    currentNegativeParams.secondaryInkTime = spinSecondaryInkTime->value();
    currentNegativeParams.secondaryReleaseTime = spinSecondaryReleaseTime->value();
    saveParams->saveNegativePressure(currentNegativeParams);

    vacuumController->sendCommand(0x20, 0, inkTime, releaseTime);
    appendLog(QString("二级压墨: 压墨时间=%1 秒, 释放时间=%2 秒")
              .arg(inkTime).arg(releaseTime, 0, 'f', 1), "blue");
}

/**
 * @brief 负压复位：cmd=0x55, 其它为 0
 */
void MainWindow::on_btnNegativeReset_clicked()
{
    vacuumController->sendCommand(0x55, 0, 0, 0.0f);
    appendLog("负压系统复位", "green");
}
/**
 * @brief 排废液：cmd=0x47, state=1开 / 0关（按钮开关切换）
 */
void MainWindow::on_btnWasteDischarge_toggled(bool checked)
{
    qint16 state = checked ? 1 : 0;   // checked=开(1), 未checked=关(0)
    vacuumController->sendCommand(0x47, state, 0, 0.0f);

    btnWasteDischarge->setText(checked ? "排废液关" : "排废液开");
    btnWasteDischarge->setStyleSheet(checked
        ? "background-color: #4CAF50; color: white;"
        : "background-color: #FF9800; color: white;");

    appendLog(checked ? "排废液: 开 (state=1)" : "排废液: 关 (state=0)",
              checked ? "green" : "orange");
}
/**
 * @brief 设置供墨泵转速：cmd=0x82, fdata=转速
 */
void MainWindow::on_btnSetInkSupplySpeed_clicked()
{
    float speed = static_cast<float>(spinInkSupplySpeedSet->value());
    vacuumController->sendCommand(0x82, 0, 0, speed);
    // 保存到配置
    currentNegativeParams.inkSupplySpeed = spinInkSupplySpeedSet->value();
    saveParams->saveNegativePressure(currentNegativeParams);
    appendLog(QString("设置供墨泵转速: %1 r/min").arg(speed), "blue");
}

/**
 * @brief 设置回墨泵转速：cmd=0x83, fdata=转速
 */
void MainWindow::on_btnSetReturnPumpSpeed_clicked()
{
    float speed = static_cast<float>(spinReturnPumpSpeedSet->value());
    vacuumController->sendCommand(0x83, 0, 0, speed);
    // 保存到配置
    currentNegativeParams.returnPumpSpeed = spinReturnPumpSpeedSet->value();
    saveParams->saveNegativePressure(currentNegativeParams);
    appendLog(QString("设置回墨泵转速: %1 r/min").arg(speed), "blue");
}
/**
 * @brief 更新负压UI状态
 */
void MainWindow::updateNegativeUI()
{
    // ===== 更新当前值显示 =====
    if (lcdPrimaryCurrent) {
        lcdPrimaryCurrent->display(QString::number(m_primaryCurrent, 'f', 2));
    }
    if (lcdSecondaryCurrent) {
        lcdSecondaryCurrent->display(QString::number(m_secondaryCurrent, 'f', 2));
    }
    if (lcdCyclicCurrent) {
        lcdCyclicCurrent->display(QString::number(m_cyclicCurrent, 'f', 2));
    }

   // ===== 更新压力曲线数据 =====
   m_primaryHistory.append(m_primaryCurrent);
   m_secondaryHistory.append(m_secondaryCurrent);
   m_cyclicHistory.append(m_cyclicCurrent);

   // 限制历史数据大小
   while (m_primaryHistory.size() > MAX_HISTORY) {
       m_primaryHistory.removeFirst();
       m_secondaryHistory.removeFirst();
       m_cyclicHistory.removeFirst();
   }

   // ===== 更新状态栏负压显示 =====
   // 在状态栏显示综合负压值（取平均值或主要值）
   QStatusBar *bar = statusBar();
   if (bar) {
       // 查找状态栏中的负压显示并更新
       QList<QLCDNumber*> lcds = bar->findChildren<QLCDNumber*>();
       for (QLCDNumber *lcd : lcds) {
           // 如果找到负压显示，更新它
           if (lcd->digitCount() == 6) {
               lcd->display(QString::number(m_primaryCurrent, 'f', 1));
               break;
           }
       }
   }

   // ===== 重新绘制压力曲线 =====
   drawPressureChart();
}


void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // 延迟绘制，避免频繁重绘
    QTimer::singleShot(50, this, [this]() {
        drawPressureChart();
    });
}

// ==================== FX3 初始化序列 ====================

QByteArray MainWindow::buildFpgaCmd(const char cmd[4], uint32_t p1, uint32_t p2,
                                    uint32_t p3, uint32_t p4, uint32_t p5)
{
    // 帧结构：32字节，大端字节序
    // [0:4]   帧头 0x33444459 = "3DDY"
    // [4:8]   命令组 0x43495047 = "CIPG"
    // [8:12]  命令字 cmd (ASCII 4字节)
    // [12:16] 参数1 p1
    // [16:20] 参数2 p2
    // [20:24] 参数3 p3
    // [24:28] 参数4 p4
    // [28:32] 参数5 p5

    QByteArray frame;
    frame.resize(32);
    quint8 *b = reinterpret_cast<quint8*>(frame.data());

    // 帧头 "3DDY"
    b[0] = 0x33; b[1] = 0x44; b[2] = 0x44; b[3] = 0x59;
    // 命令组 "CIPG"
    b[4] = 0x43; b[5] = 0x49; b[6] = 0x50; b[7] = 0x47;
    // 命令字
    b[8]  = cmd[0]; b[9]  = cmd[1]; b[10] = cmd[2]; b[11] = cmd[3];

    // 5个参数，全部大端字节序
    auto putU32BE = [](quint8 *p, uint32_t v) {
        p[0] = (v >> 24) & 0xFF;
        p[1] = (v >> 16) & 0xFF;
        p[2] = (v >> 8)  & 0xFF;
        p[3] = v & 0xFF;
    };
    putU32BE(b + 12, p1);
    putU32BE(b + 16, p2);
    putU32BE(b + 20, p3);
    putU32BE(b + 24, p4);
    putU32BE(b + 28, p5);

    return frame;
}

void MainWindow::startFpgaInit()
{
    if (!usbManager->isConnected()) {
        appendLog("FX3初始化失败：USB未连接", "red");
        return;
    }
    appendLog("========== 开始FX3初始化序列 ==========", "blue");
    m_fpgaInitStep = FPGA_INIT_SEND_CMD1;
    sendNextFpgaInitCommand();
}

void MainWindow::sendNextFpgaInitCommand()
{
    QByteArray cmd;
    QString logName;

    switch (m_fpgaInitStep) {
    case FPGA_INIT_SEND_CMD1:
        // 命令1：喷头个数设置 NOSH —— 取自界面"喷头个数"参数 (headCount, 1~8)
        // hex(以 headCount=8 为例): 33444459 43495047 4E4F5348 00000008 44444C43 00000000 50534943 00000000
        cmd = buildFpgaCmd("NOSH",
                           (uint32_t)currentPrintParams.headCount,   // 界面喷头个数（默认1，需在界面设为8）
                           0x44444C43,   // "DDLC"
                           0x00000000,
                           0x50534943,   // "PSIC"
                           0x00000000);
        logName = "命令1: 喷头个数设置(NOSH)";
        m_fpgaInitStep = FPGA_INIT_WAIT_ACK1;
        break;

    case FPGA_INIT_SEND_CMD2:
        // 命令2：电压温度设置 HVST
        // hex: 33444459 43495047 48565354 00000003 00000088 00000080 00000080 00000000
        cmd = buildFpgaCmd("HVST",
                           0x00000000,   // 喷头索引3 (第4号喷头)
                           0x00000088,   // 电压1 = 0x88
                           0x00000080,   // 电压2 = 0x80
                           0x00000080,   // 电压3 = 0x80
                           0x00000000);  // 温度 = 0
        logName = "命令2: 电压温度设置(HVST)";
        m_fpgaInitStep = FPGA_INIT_WAIT_ACK2;
        break;

    case FPGA_INIT_SEND_CMD3:
        // 命令3：零点定位 ZERO
        // hex: 33444459 43495047 5A45524F 00000000 00000000 00000000 00000000 00000000
        cmd = buildFpgaCmd("ZERO",
                           0x00000000,
                           0x00000000,
                           0x00000000,
                           0x00000000,
                           0x00000000);
        logName = "命令3: 零点定位(ZERO)";
        m_fpgaInitStep = FPGA_INIT_WAIT_ACK3;
        break;
    case FPGA_INIT_SEND_CMD4: {
        // 命令4：DPI设置 STPI
        // 两喷点间距 = 25.4mm / DPI；磁栅尺分辨率 0.5um
        // 下发脉冲数 = (25400/DPI um) / 0.5um = 50800 / DPI，四舍五入
        // 例：400DPI -> 127 (0x7F)；600DPI -> 85 (0x55)
        const int dpi = spinDpi->value();
        const uint32_t stpi = (uint32_t)qRound(50800.0 / dpi);
        cmd = buildFpgaCmd("STPI",
                           stpi,
                           0x00000000,
                           0x00000000,
                           0x00000000,
                           0x00000000);
        logName = "命令4: DPI设置(STPI)";
        m_fpgaInitStep = FPGA_INIT_WAIT_ACK4;
        break;
    }
    case FPGA_INIT_DONE:
        appendLog("========== FX3初始化序列完成 ==========", "green");
        labelUsbStatus->setText("● 板卡就绪");
        labelUsbStatus->setStyleSheet("color: green; font-weight: bold; font-size: 12px;");
        return;

    default:
        return;
    }

    appendLog(QString("发送%1: %2字节, hex=%3")
              .arg(logName).arg(cmd.size()).arg(QString(cmd.toHex(' ')).toUpper()),
              "blue", LOG_FILE_ONLY);
    usbManager->sendData(cmd);
}

// ==================== 位图分块发送 ====================

void MainWindow::sendLayerBitmap(const QByteArray &bitmapData)
{
    if (!usbManager->isConnected()) {
        appendLog("发送位图失败：USB未连接", "red");
        return;
    }

    const int CHUNK_SIZE = 4 * 1024 * 1024;  // 4MB 每块
    const int total = bitmapData.size();
    int offset = 0;
    int blockIndex = 0;

    appendLog(QString("开始发送位图: 总大小=%1字节 (%2 MB)")
              .arg(total)
              .arg(total / 1024.0 / 1024.0, 0, 'f', 2),
              "blue");

    while (offset < total) {
        int chunkSize = qMin(CHUNK_SIZE, total - offset);
        QByteArray chunk = bitmapData.mid(offset, chunkSize);

        usbManager->sendData(chunk);
        blockIndex++;

        appendLog(QString("  块%1: 偏移=%2, 大小=%3字节 (%4 KB)")
                  .arg(blockIndex)
                  .arg(offset)
                  .arg(chunkSize)
                  .arg(chunkSize / 1024.0, 0, 'f', 1),
                  "darkCyan");

        offset += chunkSize;
    }

    appendLog(QString("位图发送完成，共%1块").arg(blockIndex), "green");
}
// 查询指令发送
void MainWindow::sendQueryCmd()
{
    if (!usbManager->isConnected() || !m_queryEnabled)
        return;

    // 查询指令：33444459 43495047 41414241 00000000 * 5
    QByteArray cmd;
    cmd.resize(32);
    quint8 *b = reinterpret_cast<quint8*>(cmd.data());

    b[0] = 0x33; b[1] = 0x44; b[2] = 0x44; b[3] = 0x59;   // 3DDY
    b[4] = 0x43; b[5] = 0x49; b[6] = 0x50; b[7] = 0x47;   // CIPG
    b[8] = 0x41; b[9] = 0x41; b[10] = 0x42; b[11] = 0x41; // AABA
    // 后面全0
    memset(b + 12, 0, 20);

    usbManager->sendData(cmd);
}

//void MainWindow::parseQueryResponse(const QByteArray &data)
//{
//    if (data.size() != 32) return;

//    const quint8 *b = reinterpret_cast<const quint8*>(data.constData());

//    // 大端读 uint32
//    auto readU32BE = [](const quint8 *p) -> uint32_t {
//        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
//               ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
//    };

//    uint32_t val1 = readU32BE(b + 12);   // 第二个值（第一个不用解析）
//    uint32_t val2 = readU32BE(b + 16);   // 第三个值
//    uint32_t val3 = readU32BE(b + 20);   // 第四个值

//    // 记录状态变化，只在变化时打日志
//    bool newFirstReady = (val1 == 0x11111111);
//    bool newCanFire = (val2 == 0x11111111);
//    bool newFinished = (val3 == 0x11111111);

//    if (newFirstReady != m_firstFrameReady) {
//        m_firstFrameReady = newFirstReady;
//        appendLog(QString("查询: 第一帧接收完成=%1").arg(newFirstReady ? "是" : "否"), "darkGreen", LOG_FILE_ONLY);
//    }
//    if (newCanFire != m_canFire) {
//        m_canFire = newCanFire;
//        appendLog(QString("查询: 可以发点火指令=%1").arg(newCanFire ? "是" : "否"), "darkGreen", LOG_FILE_ONLY);
//    }
//    if (newFinished != m_printFinished) {
//        m_printFinished = newFinished;
//        appendLog(QString("查询: 打印完成=%1").arg(newFinished ? "是" : "否"), "green", LOG_FILE_ONLY);
//    }
//}

void MainWindow::parseQueryResponse(const QByteArray &data)
{
    if (data.size() != 32) return;

    const quint8 *b = reinterpret_cast<const quint8*>(data.constData());

    auto readU32BE = [](const quint8 *p) -> uint32_t {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
    };

    uint32_t val1 = readU32BE(b + 4);
    uint32_t val2 = readU32BE(b + 8);
    uint32_t val3 = readU32BE(b + 12);

    bool newFirstReady = (val1 == 0x11111111);
    bool newCanFire = (val2 == 0x11111111);
    bool newFinished = (val3 == 0x11111111);

    // ===== 如果正在等点火回复，收到的第一帧就是点火ACK =====
    if (m_waitFireAck) {
        m_waitFireAck = false;
        appendLog("→ 收到点火指令ACK", "green");

        // 重新启动查询
        m_queryEnabled = true;
        m_printQueryTimer->start();
        appendLog("恢复状态查询...", "blue", LOG_FILE_ONLY);
        return;
    }

    // ===== 状态变化日志 =====
    if (newFirstReady != m_firstFrameReady) {
        m_firstFrameReady = newFirstReady;
        appendLog(QString("查询: 第一帧接收完成=%1").arg(newFirstReady ? "是" : "否"), "darkGreen", LOG_FILE_ONLY);
    }

    if (newFinished != m_printFinished) {
        m_printFinished = newFinished;
        if (newFinished) {
            // 打印完成，停止查询
            m_queryEnabled = false;
            m_printQueryTimer->stop();
            appendLog("🎉 打印完成！停止查询", "green");
            updatePrintState(IDLE);
        }
    }

    // ===== 检测到可以点火：停止查询 → 发点火 → 等ACK → 恢复查询 =====
    if (newCanFire && !m_canFire) {
        m_canFire = true;
        appendLog("查询: 可以发点火指令", "green");

        // 1. 停止查询定时器
        m_queryEnabled = false;
        m_printQueryTimer->stop();
        appendLog("暂停查询，准备发送点火...", "blue", LOG_FILE_ONLY);

        // 2. 标记正在等点火ACK
        m_waitFireAck = true;

        // 3. 发送点火指令
        sendFireCmd();
    }

    m_canFire = newCanFire;
    m_firstFrameReady = newFirstReady;
    m_printFinished = newFinished;
}

// TODO: 点火指令，等用户给具体格式后实现
void MainWindow::sendFireCmd()
{
    // 点火指令：FIRE
    // 33444459 43495047 46495245 p1 p2 p3 p4 p5
    QByteArray cmd;
    cmd.resize(32);
    quint8 *b = reinterpret_cast<quint8*>(cmd.data());

    // 帧头
    b[0] = 0x33; b[1] = 0x44; b[2] = 0x44; b[3] = 0x59;   // 3DDY
    b[4] = 0x43; b[5] = 0x49; b[6] = 0x50; b[7] = 0x47;   // CIPG
    b[8] = 0x46; b[9] = 0x49; b[10] = 0x52; b[11] = 0x45; // FIRE

    // 参数（大端）
    auto putU32BE = [](quint8 *p, uint32_t v) {
        p[0] = (v >> 24) & 0xFF;
        p[1] = (v >> 16) & 0xFF;
        p[2] = (v >> 8)  & 0xFF;
        p[3] = v & 0xFF;
    };

    // 1/2PASS 起止点：按"两个像素间距 127 个脉冲"计算（结束点 = 绝对位置 = 起点 + 步数）
    //   1pass 起点 = 1000（固定）
    //   1pass 结束点 = 起点 1000 + 本层条带行数(火线数) × 127
    //   2pass 反过来：起点 = 1pass结束点，结束点 = 1pass起点
    //const uint32_t PULSE_PER_PIXEL = 127;
    const uint32_t PULSE_PER_PIXEL = (uint32_t)qRound(50800.0 / spinDpi->value());
    uint32_t pass1Start = 1000;     // 1pass 起点（固定）
    uint32_t pass1End   = pass1Start + static_cast<uint32_t>(m_currentPassPixels) * PULSE_PER_PIXEL; // 绝对结束位置
    uint32_t pass2Start = pass1End; // 2pass 反过来
    uint32_t pass2End   = pass1Start;
    uint32_t reserved   = 0x15;     // 第8个字段，先按示例填


    putU32BE(b + 12, pass1Start);
    putU32BE(b + 16, pass1End);
    putU32BE(b + 20, pass2Start);
    putU32BE(b + 24, pass2End);
    putU32BE(b + 28, reserved);

    appendLog(QString("发送点火指令: 1pass[%1~%2], 2pass[%3~%4]")
              .arg(pass1Start).arg(pass1End)
              .arg(pass2Start).arg(pass2End), "purple");
    appendLog(QString("点火hex: %1").arg(QString(cmd.toHex(' ')).toUpper()), "darkCyan", LOG_FILE_ONLY);

    usbManager->sendData(cmd);
}

// ==================== 日志文件 ====================

//void MainWindow::initLogFile()
//{
//    QString logDir = QDir::homePath() + "/.config/BJPrinter/logs";
//    QDir().mkpath(logDir);

//    QString fileName = QString("print_%1.log")
//                       .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
//    m_logFilePath = logDir + "/" + fileName;

//    m_logFile = new QFile(m_logFilePath, this);
//    if (m_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
//        m_logStream = new QTextStream(m_logFile);
//        // 直接写文件，不调 appendLog（这个时候 UI 已经好了，调也没事）
//        writeToLogFile("========== 程序启动 ==========");
//        appendLog(QString("日志文件: %1").arg(m_logFilePath), "blue");
//    } else {
//        m_logStream = nullptr;
//        appendLog("日志文件打开失败，不写文件", "red");
//    }
//}
void MainWindow::initLogFile()
{
    QString logDir = QDir::homePath() + "/.config/BJPrinter/logs";
    QDir().mkpath(logDir);

    // 按整小时命名：print_20261023_080000.log
    m_currentLogHour = QDateTime::currentDateTime().toString("yyyyMMdd_hh") + "0000";
    m_logFilePath = logDir + "/print_" + m_currentLogHour + ".log";

    m_logFile = new QFile(m_logFilePath, this);
    if (m_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        m_logStream = new QTextStream(m_logFile);
        writeToLogFile("========== 程序启动 ==========");
        appendLog(QString("日志文件: %1").arg(m_logFilePath), "blue");
    } else {
        m_logStream = nullptr;
        appendLog("日志文件打开失败，不写文件", "red");
    }

    // 启动时先清理一次旧日志
    cleanupOldLogs();

    // 每10分钟检查一次是否到了新的小时
    m_logRotateTimer = new QTimer(this);
    m_logRotateTimer->setInterval(10 * 60 * 1000);  // 10分钟
    connect(m_logRotateTimer, &QTimer::timeout, this, &MainWindow::checkLogRotate);
    m_logRotateTimer->start();
}

void MainWindow::checkLogRotate()
{
    QString nowHour = QDateTime::currentDateTime().toString("yyyyMMdd_hh") + "0000";

    // 到了新的小时，切分日志
    if (nowHour != m_currentLogHour) {
        m_currentLogHour = nowHour;
        QString logDir = QDir::homePath() + "/.config/BJPrinter/logs";
        QString newPath = logDir + "/print_" + m_currentLogHour + ".log";

        // 关闭旧文件，打开新文件
        if (m_logFile) {
            m_logStream->flush();
            m_logFile->close();
            delete m_logStream;
            delete m_logFile;
        }

        m_logFile = new QFile(newPath, this);
        if (m_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            m_logStream = new QTextStream(m_logFile);
            writeToLogFile("========== 小时切分 ==========");
            appendLog(QString("日志切分: 新文件 %1").arg(newPath), "blue");
        } else {
            m_logStream = nullptr;
        }

        // 切分后顺便清理旧日志
        cleanupOldLogs();
    }
}

void MainWindow::cleanupOldLogs()
{
    QString logDir = QDir::homePath() + "/.config/BJPrinter/logs";
    QDir dir(logDir);
    QStringList filters;
    filters << "print_*.log";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Time);  // 按时间新→旧

    if (files.isEmpty()) return;

    // 计算近7天内的文件数量
    QDateTime weekAgo = QDateTime::currentDateTime().addDays(-7);
    int recentCount = 0;
    for (const QFileInfo &fi : files) {
        if (fi.lastModified() >= weekAgo)
            recentCount++;
        else
            break;  // files是按时间排好的，后面都是更早的
    }

    // ===== 清理规则 =====
    // 规则：如果近一周的文件数 > 48个，就删除7天前的旧文件
    //       如果近一周的文件数 ≤ 48个，那就不删了（不受时间限制）
    if (recentCount > 48) {
        int deleted = 0;
        for (const QFileInfo &fi : files) {
            if (fi.lastModified() < weekAgo) {
                QFile::remove(fi.absoluteFilePath());
                deleted++;
            }
        }
        if (deleted > 0) {
            appendLog(QString("清理旧日志: 删除 %1 个超过7天的文件").arg(deleted), "blue", LOG_FILE_ONLY);
        }
    }
}


void MainWindow::writeToLogFile(const QString &msg)
{
    if (m_logStream) {
        *m_logStream << msg << Qt::endl;
        m_logStream->flush();
    }
}


