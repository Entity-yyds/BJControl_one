#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include "usbmanager.h"
#include "saveparams.h"

#include <QMainWindow>
#include <QTimer>
#include <QThread>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QProgressBar>
#include <QLCDNumber>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTabWidget>
#include <QGroupBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QSettings>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // ========== USB连接 ==========
    void on_btnConnect_clicked();
    void on_btnDisconnect_clicked();
    void on_btnRefresh_clicked();
    void onUsbStatusChanged(bool connected);
    void onUsbDataReceived(const QByteArray &data);
    void onUsbError(const QString &error);

    // ========== 操控界面 ==========
    void on_btnStartPrint_clicked();
    void on_btnPausePrint_clicked();
    void on_btnStopPrint_clicked();
    void on_btnScan_clicked();
    void on_btnClearLog_clicked();
    void on_btnEmergencyStop_clicked();

    // ========== 负压设置 ==========
    void on_btnSetNegativePressure_clicked();
    void on_btnNegativePressureOn_clicked();
    void on_btnNegativePressureOff_clicked();
    void on_btnNegativePressureReset_clicked();

    // ========== 波形控制 ==========
    void on_btnApplyWaveform_clicked();
    void on_btnLoadWaveform_clicked();
    void on_btnSaveWaveform_clicked();
    void on_btnTestJet_clicked();

    // ========== 运动参数 ==========
    void on_btnSetMotionParams_clicked();
    void on_btnHomeAll_clicked();
    void on_btnMoveTo_clicked();
    void on_btnSetOrigin_clicked();

    // ========== 定时器更新 ==========
    void updateStatus();

private:
    // ========== USB管理 ==========
    UsbManager *usbManager;
    // ========== 参数保存 ==========
    SaveParams *saveParams;

    // ========== 打印状态 ==========
    enum PrintState {
        IDLE,
        PRINTING,
        PAUSED,
        STOPPED,
        ERROR
    };
    PrintState currentState;

    // ========== 状态定时器 ==========
    QTimer *statusTimer;


    // ========== 参数结构体（使用 saveparams.h 中定义的） ==========
    MotionParams currentMotionParams;
    WaveformData currentWaveform;
    NegativePressureParams currentNegativePressure;
    // ========== 负压参数 ==========   注意这种声明加定义
//    struct NegativePressureParams {
//        double pressure;
//        bool enabled;
//    } currentNegativePressure;

    // ========== 模拟数据 ==========
    double simulatedTemp;
    double simulatedPressure;
    int simulatedProgress;

    // ========== UI控件指针 ==========
    // 连接栏
    QComboBox *comboDevice;
    QPushButton *btnRefresh;
    QPushButton *btnConnect;
    QPushButton *btnDisconnect;
    QLabel *labelUsbStatus;

    // 主Tab
    QTabWidget *tabWidget;

    // ===== Tab1: 操控界面 =====
    QPushButton *btnStartPrint;
    QPushButton *btnPausePrint;
    QPushButton *btnStopPrint;
    QPushButton *btnScan;
    QPushButton *btnEmergencyStop;
    QPushButton *btnClearLog;
    QLabel *labelPrintState;
    QLabel *labelPrintStateText;
    QProgressBar *progressPrint;
    QLabel *labelProgress;
    QLabel *labelLayerInfo;
    QGraphicsView *printPreview;
    QGraphicsScene *printScene;
    QTextEdit *textLog;

    // ===== Tab2: 负压设置 =====
    QDoubleSpinBox *spinNegativePressure;
    QPushButton *btnSetNegativePressure;
    QPushButton *btnNegativePressureOn;
    QPushButton *btnNegativePressureOff;
    QPushButton *btnNegativePressureReset;
    QLCDNumber *lcdNegativePressure;
    QLabel *labelNegativeStatus;
    QGraphicsView *pressureChart;
    QGraphicsScene *pressureScene;

    // ===== Tab3: 波形控制 =====
    QDoubleSpinBox *spinVoltage;
    QDoubleSpinBox *spinRiseTime;
    QDoubleSpinBox *spinDwellTime;
    QDoubleSpinBox *spinFallTime;
    QDoubleSpinBox *spinFrequency;
    QPushButton *btnApplyWaveform;
    QPushButton *btnLoadWaveform;
    QPushButton *btnSaveWaveform;
    QComboBox *comboTestPattern;
    QPushButton *btnTestJet;
    QLCDNumber *lcdJetTemp;
    QLabel *labelWaveformStatus;
    QGraphicsView *waveformPreview;
    QGraphicsScene *waveformScene;

    // ===== Tab4: 运动参数 =====
    QDoubleSpinBox *spinSpeedX;
    QDoubleSpinBox *spinSpeedY;
    QDoubleSpinBox *spinSpeedZ;
    QDoubleSpinBox *spinAcceleration;
    QDoubleSpinBox *spinJerk;
    QDoubleSpinBox *spinLayerHeight;
    QPushButton *btnSetMotionParams;
    QDoubleSpinBox *spinPosX;
    QDoubleSpinBox *spinPosY;
    QDoubleSpinBox *spinPosZ;
    QPushButton *btnMoveTo;
    QPushButton *btnHomeAll;
    QPushButton *btnSetOrigin;
    QLabel *labelCurrentPos;

    // ===== 状态栏 =====
    QLCDNumber *lcdTemperature;

    // ========== UI函数 ==========
    void setupUI();
    QHBoxLayout* createConnectionBar();
    //void createConnectionBar();
    void createMainTab();
    void createControlTab();
    void createNegativePressureTab();
    void createWaveformTab();
    void createMotionTab();
    void createStatusBar();

    void updatePrintState(PrintState state);
    void appendLog(const QString &message, const QString &color = "black");
    void updatePrintPreview(int layer);
    void updateUsbStatus(bool connected);
    void drawWaveformPreview();
    void drawPressureChart();

    // ========== 数据持久化 ==========
    void loadMotionParams();      // 加载运动参数
    void saveMotionParams();      // 保存运动参数
    void loadWaveformParams();    // 加载波形参数（预留）
    void saveWaveformParams();    // 保存波形参数（预留）
    void loadNegativePressure();  // 加载负压参数（预留）
    void saveNegativePressure();  // 保存负压参数（预留
    // ========== 辅助函数 ==========
    QGroupBox* createGroupBox(const QString &title);
    void addWidgetToLayout(QLayout *layout, QWidget *widget, int row, int col, int rowSpan = 1, int colSpan = 1);
};

#endif // MAINWINDOW_H
