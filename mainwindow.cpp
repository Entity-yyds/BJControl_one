#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QMessageBox>
#include <QFileDialog>
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

MainWindow::MainWindow(QWidget *parent) // 初始化列表优点：在初始化列表中，C++ 会按声明顺序执行初始化
    : QMainWindow(parent)
    , usbManager(new UsbManager(this)) // UsbManager 会被添加到 Qt 对象树中，当 MainWindow 销毁时，UsbManager 会自动被删除，无需手动 delete
    , currentState(IDLE)            // 将打印状态初始化为"空闲"，表示打印机刚启动，还没有开始任何任务
    , simulatedTemp(25.0)           // 模拟温度初始化为 25°C（室温），后续会通过 updateStatus() 定时器模拟变化
    , simulatedPressure(-50.0)      // 模拟负压初始化为 -50 kPa（典型负压值），负压用于吸附粉床和回收未粘结的粉末
    , simulatedProgress(0)          // 打印进度初始化为 0%，表示还没有开始打印
{
    // 设置窗口属性
    setWindowTitle("Binder Jetting 3D Printer Controller");
    resize(1400, 900);

    // 初始化默认参数
    currentWaveform.voltage = 45.0;
    currentWaveform.riseTime = 2.0;
    currentWaveform.dwellTime = 6.0;
    currentWaveform.fallTime = 2.0;
    currentWaveform.frequency = 20.0;

    currentMotionParams.speedX = 300.0;     // ← 新增
    currentMotionParams.speedY = 300.0;
    currentMotionParams.speedZ = 50.0;
    currentMotionParams.acceleration = 1000.0;
    currentMotionParams.jerk = 5.0;
    currentMotionParams.layerHeight = 0.1;

    currentNegativePressure.pressure = -50.0;
    currentNegativePressure.enabled = false;

    // 创建UI
    setupUI();

    // ====== 初始化USB管理 ======
    connect(usbManager, &UsbManager::statusChanged, // USB插入/拔出 → usbManager 发出 statusChanged(true/false)
            this, &MainWindow::onUsbStatusChanged); // → 自动调用 onUsbStatusChanged(bool connected)
                                                    // → 更新界面状态

    connect(usbManager, &UsbManager::dataReceived,
            this, &MainWindow::onUsbDataReceived);
                                                    // USB收到数据 → usbManager 发出 dataReceived(数据)
                                                    // → 自动调用 onUsbDataReceived(QByteArray data)
                                                    // → 解析数据并更新界面
    connect(usbManager, &UsbManager::errorOccurred,
            this, &MainWindow::onUsbError);         // USB发生错误 → usbManager 发出 errorOccurred("错误信息")
                                                    // → 自动调用 onUsbError(QString error)
                                                    // → 在日志中显示错误信息

    // ====== 初始化状态定时器 ======
    statusTimer = new QTimer(this);
    statusTimer->setInterval(1000);
    connect(statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    statusTimer->start();                           // 定时器启动 → 每隔1秒发出 timeout 信号
                                                    // → 自动调用 updateStatus()
                                                    // → 更新 LCD 显示、进度条等

    // ====== 初始化UI状态 ======
    updatePrintState(IDLE);
    updateUsbStatus(false);

    // ====== 刷新设备列表 ======
    on_btnRefresh_clicked();

    appendLog("程序已启动，等待连接设备...", "blue");
    appendLog("星光喷头 600DPI 控制就绪", "green");
}

MainWindow::~MainWindow()
{
    delete usbManager;
}

// ==================== UI创建 ====================

void MainWindow::setupUI()
{
    // 创建中央部件
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(5);
    mainLayout->setContentsMargins(5, 5, 5, 5);

    // 创建各个部分
    //createConnectionBar();
    QHBoxLayout *connectionLayout = createConnectionBar();
    mainLayout->addLayout(connectionLayout);
    //mainLayout->addLayout(static_cast<QLayout*>(findChild<QHBoxLayout*>("connectionLayout")));

    createMainTab();
    mainLayout->addWidget(tabWidget);

    createStatusBar();
}

QHBoxLayout* MainWindow::createConnectionBar()
{
    qDebug() << "  -> createConnectionBar() 开始";
    QHBoxLayout *layout = new QHBoxLayout();
    //layout->setObjectName("connectionLayout");
    layout->setSpacing(8);
    layout->setContentsMargins(5, 5, 5, 5);

    // USB状态标签
    labelUsbStatus = new QLabel("○ 未连接", this);
    labelUsbStatus->setStyleSheet("color: gray; font-weight: bold; font-size: 12px;");
    labelUsbStatus->setMinimumWidth(120);
    labelUsbStatus->setFixedHeight(30);
    layout->addWidget(labelUsbStatus);
    qDebug() << "    -> USB状态标签已添加";
    layout->addSpacing(10);

    // 设备选择
    QLabel *labelDevice = new QLabel("设备:", this);
    labelDevice->setFixedHeight(30);
    layout->addWidget(labelDevice);

    comboDevice = new QComboBox(this);
    comboDevice->setMinimumWidth(200);
    comboDevice->setFixedHeight(30);
    layout->addWidget(comboDevice);
    qDebug() << "    -> 设备选择已添加";
    layout->addSpacing(5);
    // 按钮
    btnRefresh = new QPushButton("刷新", this);
    //btnRefresh->setFixedWidth(80);
    btnRefresh->setFixedSize(80, 30);
    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::on_btnRefresh_clicked);
    layout->addWidget(btnRefresh);

    btnConnect = new QPushButton("连接", this);
    //btnConnect->setFixedWidth(80);
    btnConnect->setFixedSize(80, 30);
    btnConnect->setStyleSheet("background-color: #4CAF50; color: white;");
    connect(btnConnect, &QPushButton::clicked, this, &MainWindow::on_btnConnect_clicked);
    layout->addWidget(btnConnect);

    btnDisconnect = new QPushButton("断开", this);
    //btnDisconnect->setFixedWidth(80);
    btnDisconnect->setFixedSize(80, 30);
    btnDisconnect->setEnabled(false);
    connect(btnDisconnect, &QPushButton::clicked, this, &MainWindow::on_btnDisconnect_clicked);
    layout->addWidget(btnDisconnect);
    // ===== 右侧：弹簧推到底 =====
    layout->addStretch();

    // 添加紧急停止按钮到连接栏右侧
    QPushButton *emergencyBtn = new QPushButton("⚠ 紧急停止", this);
    //emergencyBtn->setFixedWidth(120);
    emergencyBtn->setFixedSize(130, 36);
    emergencyBtn->setStyleSheet("background-color: #f44336; color: white; font-weight: bold;");
    connect(emergencyBtn, &QPushButton::clicked, this, &MainWindow::on_btnEmergencyStop_clicked);
    layout->addWidget(emergencyBtn);
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
    createWaveformTab();
    createMotionTab();

    tabWidget->addTab(findChild<QWidget*>("controlTab"), "操控界面");
    tabWidget->addTab(findChild<QWidget*>("negativeTab"), "负压设置");
    tabWidget->addTab(findChild<QWidget*>("waveformTab"), "波形控制");
    tabWidget->addTab(findChild<QWidget*>("motionTab"), "运动参数");
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

    // 清空日志
    btnClearLog = new QPushButton("🗑 清空日志", this);
    connect(btnClearLog, &QPushButton::clicked, this, &MainWindow::on_btnClearLog_clicked);
    controlLayout->addWidget(btnClearLog);

    controlLayout->addStretch();
    leftLayout->addWidget(controlGroup);

    // 任务信息
    QGroupBox *taskGroup = createGroupBox("当前任务");
    QVBoxLayout *taskLayout = new QVBoxLayout(taskGroup);
    QLabel *taskLabel = new QLabel("模型: 未加载", this);
    taskLayout->addWidget(taskLabel);
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

    // 控制区域
    QGroupBox *controlGroup = createGroupBox("负压控制");
    QGridLayout *controlLayout = new QGridLayout(controlGroup);

    // 目标负压设置
    controlLayout->addWidget(new QLabel("目标负压:", this), 0, 0);
    spinNegativePressure = new QDoubleSpinBox(this);
    spinNegativePressure->setRange(-80, -20);
    spinNegativePressure->setValue(-50);
    spinNegativePressure->setSuffix(" kPa");
    controlLayout->addWidget(spinNegativePressure, 0, 1);

    btnSetNegativePressure = new QPushButton("设置", this);
    connect(btnSetNegativePressure, &QPushButton::clicked, this, &MainWindow::on_btnSetNegativePressure_clicked);
    controlLayout->addWidget(btnSetNegativePressure, 0, 2);

    // 当前负压显示
    controlLayout->addWidget(new QLabel("当前负压:", this), 1, 0);
    lcdNegativePressure = new QLCDNumber(this);
    lcdNegativePressure->setDigitCount(6);
    lcdNegativePressure->display(-50.0);
    controlLayout->addWidget(lcdNegativePressure, 1, 1, 1, 2);

    // 状态显示
    controlLayout->addWidget(new QLabel("状态:", this), 2, 0);
    labelNegativeStatus = new QLabel("● 已关闭", this);
    labelNegativeStatus->setStyleSheet("color: gray; font-weight: bold;");
    controlLayout->addWidget(labelNegativeStatus, 2, 1, 1, 2);

    // 控制按钮
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnNegativePressureOn = new QPushButton("开启负压", this);
    btnNegativePressureOn->setStyleSheet("background-color: #4CAF50; color: white;");
    connect(btnNegativePressureOn, &QPushButton::clicked, this, &MainWindow::on_btnNegativePressureOn_clicked);
    btnLayout->addWidget(btnNegativePressureOn);

    btnNegativePressureOff = new QPushButton("关闭负压", this);
    btnNegativePressureOff->setEnabled(false);
    btnNegativePressureOff->setStyleSheet("background-color: #f44336; color: white;");
    connect(btnNegativePressureOff, &QPushButton::clicked, this, &MainWindow::on_btnNegativePressureOff_clicked);
    btnLayout->addWidget(btnNegativePressureOff);

    btnNegativePressureReset = new QPushButton("复位", this);
    connect(btnNegativePressureReset, &QPushButton::clicked, this, &MainWindow::on_btnNegativePressureReset_clicked);
    btnLayout->addWidget(btnNegativePressureReset);

    controlLayout->addLayout(btnLayout, 3, 0, 1, 3);

    mainLayout->addWidget(controlGroup);

    // 压力曲线图
    QGroupBox *chartGroup = createGroupBox("压力曲线");
    QVBoxLayout *chartLayout = new QVBoxLayout(chartGroup);

    pressureScene = new QGraphicsScene(this);
    pressureChart = new QGraphicsView(pressureScene, this);
    pressureChart->setMinimumHeight(250);
    pressureChart->setBackgroundBrush(QBrush(Qt::white));
    chartLayout->addWidget(pressureChart);

    mainLayout->addWidget(chartGroup);

    // 绘制初始图表
    drawPressureChart();
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
    spinSpeedX->setValue(300);                  // 程序启动时的初始值
    spinSpeedX->setSuffix(" mm/s");             // 数值后面显示的单位
    spinSpeedX->setFixedWidth(150);             // 控件的宽度（不随窗口变化）
    paramLayout->addWidget(spinSpeedX, 0, 1);   // 将spinSpeedX 控件添加到网格布局 (spinSpeedX, 0, 1) (要添加的控件（浮点数输入框）, 放在第 0 行, 放在第 1 列)  // 输入框放 (0, 1)


    // Y轴速度
    paramLayout->addWidget(new QLabel("Y轴速度:", this), 1, 0);
    spinSpeedY = new QDoubleSpinBox(this);    // ← 新增控件
    spinSpeedY->setRange(100, 2000);
    spinSpeedY->setValue(300);
    spinSpeedY->setSuffix(" mm/s");
    spinSpeedY->setFixedWidth(150);
    paramLayout->addWidget(spinSpeedY, 1, 1);

    // Z轴速度
    paramLayout->addWidget(new QLabel("Z轴速度:", this), 2, 0);
    spinSpeedZ = new QDoubleSpinBox(this);
    spinSpeedZ->setRange(10, 200);
    spinSpeedZ->setValue(50);
    spinSpeedZ->setSuffix(" mm/s");
    spinSpeedZ->setFixedWidth(150);
    paramLayout->addWidget(spinSpeedZ, 2, 1);

    // 加速度
    paramLayout->addWidget(new QLabel("加速度:", this), 3, 0);
    spinAcceleration = new QDoubleSpinBox(this);
    spinAcceleration->setRange(100, 5000);
    spinAcceleration->setValue(1000);
    spinAcceleration->setSuffix(" mm/s²");
    spinAcceleration->setFixedWidth(150);
    paramLayout->addWidget(spinAcceleration, 3, 1);

    // Jerk
    paramLayout->addWidget(new QLabel("Jerk:", this), 4, 0);
    spinJerk = new QDoubleSpinBox(this);
    spinJerk->setRange(1, 20);
    spinJerk->setValue(5);
    spinJerk->setSuffix(" mm/s³");
    spinJerk->setFixedWidth(150);
    paramLayout->addWidget(spinJerk, 4, 1);

    // 层厚
    paramLayout->addWidget(new QLabel("层厚:", this), 5, 0);
    spinLayerHeight = new QDoubleSpinBox(this);
    spinLayerHeight->setRange(0.02, 0.3);
    spinLayerHeight->setSingleStep(0.01);
    spinLayerHeight->setValue(0.1);
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

    // X轴
    manualLayout->addWidget(new QLabel("X:", this), 0, 0);
    spinPosX = new QDoubleSpinBox(this);
    spinPosX->setRange(-500, 500);
    spinPosX->setValue(0);
    spinPosX->setSuffix(" mm");
    spinPosX->setFixedWidth(150);
    manualLayout->addWidget(spinPosX, 0, 1);

    // Y轴
    manualLayout->addWidget(new QLabel("Y:", this), 1, 0);
    spinPosY = new QDoubleSpinBox(this);
    spinPosY->setRange(-500, 500);
    spinPosY->setValue(0);
    spinPosY->setSuffix(" mm");
    spinPosY->setFixedWidth(150);
    manualLayout->addWidget(spinPosY, 1, 1);

    // Z轴
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

void MainWindow::on_btnConnect_clicked()
{
    QString device = comboDevice->currentText();
    if (device == "无可用设备") {
        QMessageBox::warning(this, "警告", "没有可用的USB设备！");
        return;
    }

    if (usbManager->connectDevice(device)) {
        appendLog(QString("已连接到设备: %1").arg(device), "green");
    } else {
        appendLog("连接失败", "red");
    }
}

void MainWindow::on_btnDisconnect_clicked()
{
    usbManager->disconnectDevice();
    appendLog("已断开设备连接", "orange");
}

void MainWindow::on_btnRefresh_clicked()
{
    comboDevice->clear();
    QStringList devices = usbManager->getAvailableDevices();
    if (devices.isEmpty()) {
        comboDevice->addItem("无可用设备");
        appendLog("未检测到USB设备", "orange");
    } else {
        comboDevice->addItems(devices);
        appendLog(QString("发现 %1 个USB设备").arg(devices.size()), "green");
    }
}

void MainWindow::onUsbStatusChanged(bool connected)
{
    updateUsbStatus(connected);
    if (connected) {
        QString info = usbManager->getDeviceInfo();
        appendLog(QString("设备就绪: %1").arg(info), "green");
    }
}

void MainWindow::onUsbDataReceived(const QByteArray &data)
{
    appendLog(QString("收到数据: %1字节").arg(data.size()), "darkCyan");
    // TODO: 解析实际数据包
}

void MainWindow::onUsbError(const QString &error)
{
    appendLog(QString("USB错误: %1").arg(error), "red");
}

// ==================== 操控界面槽函数 ====================

void MainWindow::on_btnStartPrint_clicked()
{
    if (!usbManager->isConnected()) {
        QMessageBox::warning(this, "警告", "请先连接设备！");
        return;
    }

    if (currentState == IDLE || currentState == STOPPED) {
        usbManager->sendData("START_PRINT");
        updatePrintState(PRINTING);
        appendLog("开始打印任务", "green");
        simulatedProgress = 0;
    } else {
        QMessageBox::information(this, "提示", "设备当前状态无法开始打印");
    }
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

void MainWindow::on_btnClearLog_clicked()
{
    textLog->clear();
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

// ==================== 负压设置槽函数 ====================

void MainWindow::on_btnSetNegativePressure_clicked()
{
    currentNegativePressure.pressure = spinNegativePressure->value();
    appendLog(QString("设置负压值: %1 kPa").arg(currentNegativePressure.pressure), "blue");

    if (usbManager->isConnected()) {
        QByteArray data;
        data.append(static_cast<char>(0x10));
        data.append(reinterpret_cast<const char*>(&currentNegativePressure.pressure), sizeof(double));
        usbManager->sendData(data);
    }
}

void MainWindow::on_btnNegativePressureOn_clicked()
{
    currentNegativePressure.enabled = true;
    btnNegativePressureOn->setEnabled(false);
    btnNegativePressureOff->setEnabled(true);
    labelNegativeStatus->setText("● 运行中");
    labelNegativeStatus->setStyleSheet("color: green; font-weight: bold;");
    appendLog("负压已开启", "green");

    if (usbManager->isConnected()) {
        usbManager->sendData("NEGATIVE_ON");
    }
}

void MainWindow::on_btnNegativePressureOff_clicked()
{
    currentNegativePressure.enabled = false;
    btnNegativePressureOn->setEnabled(true);
    btnNegativePressureOff->setEnabled(false);
    labelNegativeStatus->setText("● 已关闭");
    labelNegativeStatus->setStyleSheet("color: gray; font-weight: bold;");
    appendLog("负压已关闭", "orange");

    if (usbManager->isConnected()) {
        usbManager->sendData("NEGATIVE_OFF");
    }
}

void MainWindow::on_btnNegativePressureReset_clicked()
{
    spinNegativePressure->setValue(-50);
    appendLog("负压已复位", "blue");
    if (usbManager->isConnected()) {
        usbManager->sendData("NEGATIVE_RESET");
    }
}

// ==================== 波形控制槽函数 ====================

void MainWindow::on_btnApplyWaveform_clicked()
{
    currentWaveform.voltage = spinVoltage->value();
    currentWaveform.riseTime = spinRiseTime->value();
    currentWaveform.dwellTime = spinDwellTime->value();
    currentWaveform.fallTime = spinFallTime->value();
    currentWaveform.frequency = spinFrequency->value();

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

    appendLog(QString("设置运动参数: X速度=%1mm/s, Y速度=%2mm/s, Z速度=%3mm/s, 加速度=%4mm/s²")
                  .arg(currentMotionParams.speedX)
                  .arg(currentMotionParams.speedY)
                  .arg(currentMotionParams.speedZ)
                  .arg(currentMotionParams.acceleration), "blue");

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

void MainWindow::updateStatus()
{
    // 模拟温度变化
    simulatedTemp += (QRandomGenerator::global()->generateDouble() - 0.5) * 0.5;
    if (simulatedTemp < 20) simulatedTemp = 20;
    if (simulatedTemp > 60) simulatedTemp = 60;

    // 模拟负压变化
    if (currentNegativePressure.enabled) {
        simulatedPressure += (QRandomGenerator::global()->generateDouble() - 0.5) * 2.0;
        if (simulatedPressure > -30) simulatedPressure = -35;
        if (simulatedPressure < -70) simulatedPressure = -65;
    } else {
        simulatedPressure = -10.0;
    }

    // 模拟打印进度
    if (currentState == PRINTING) {
        simulatedProgress += QRandomGenerator::global()->bounded(2);
        if (simulatedProgress > 100) {
            simulatedProgress = 100;
            updatePrintState(IDLE);
            appendLog("打印完成！", "green");
        }
    }

    // 更新UI
    lcdTemperature->display(QString::number(simulatedTemp, 'f', 1));
    lcdNegativePressure->display(QString::number(simulatedPressure, 'f', 1));
    progressPrint->setValue(simulatedProgress);
    labelProgress->setText(QString("%1%").arg(simulatedProgress));
}

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
    btnConnect->setEnabled(!connected);
    btnDisconnect->setEnabled(connected);
    btnRefresh->setEnabled(!connected);
    btnScan->setEnabled(connected);
    btnStartPrint->setEnabled(connected && (currentState == IDLE || currentState == STOPPED));

    QString status = connected ? "● 已连接" : "○ 未连接";
    QString color = connected ? "green" : "gray";
    labelUsbStatus->setText(status);
    labelUsbStatus->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 12px;").arg(color));
}

void MainWindow::appendLog(const QString &message, const QString &color)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    QString html = QString("<span style='color:%1;'>[%2] %3</span>")
                   .arg(color).arg(timestamp).arg(message);
    textLog->append(html);
    textLog->moveCursor(QTextCursor::End);
}

void MainWindow::updatePrintPreview(int layer)
{
    // TODO: 实现打印预览更新
    Q_UNUSED(layer);
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
    pressureScene->clear();
    QPen pen(Qt::red);
    pen.setWidth(2);

    int width = pressureChart->width() - 20;
    int height = pressureChart->height() - 20;
    if (width < 100) width = 500;
    if (height < 100) height = 220;

    // 模拟压力曲线
    QPainterPath path;
    for (int i = 0; i <= width; i += 2) {
        double x = i + 10;
        double y = 10 + 20 * std::sin(i / 50.0) + 10 * std::cos(i / 30.0);  // ← 使用 std::sin 和 std::cos
        if (i == 0) {
            path.moveTo(x, y + 20);
        } else {
            path.lineTo(x, y + 20);
        }
    }

    pressureScene->addPath(path, pen);
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
