#ifndef SAVEPARAMS_H
#define SAVEPARAMS_H

#include <QObject>
#include <QSettings>
#include <QList>

// ========== 运动参数存储 ==========
struct MotionParams {
    double speedX;          // X轴速度 (mm/s)
    double speedY;          // Y轴速度 (mm/s)
    double speedZ;          // Z轴速度 (mm/s)
    double acceleration;    // 加速度  (mm/s²)
    double jerk;            // 加加速度 (mm/s³)
    double layerHeight;     // 层厚    (mm)
};
// ========== 波形数据存储 ==========
struct WaveformData {
    double voltage;     // 电压越高，压电晶体形变越大，喷射的液滴速度越快、体积越大
    double riseTime;    // 上升时间，（波形上升斜率）控制压电晶体形变的速度
    double dwellTime;   // 维持时间/脉冲宽度，电压保持在峰值的时间（单位：微秒 μs） 典型值：6 μs
    double fallTime;    // 下降时间，电压从峰值下降到 0 所需的时间，控制压电晶体恢复形变的速度
    double frequency;   // 每秒喷射的次数（单位：kHz，千赫兹），决定打印速度
};

// 负压参数结构体
//struct NegativePressureParams {
//    double pressure;
//    bool enabled;
//};
/**
 * @brief 单路负压参数
 */
struct NegativePressureChannel {
    double target;      // 目标负压值 (kPa)
    double current;     // 当前负压值 (kPa)
//    bool enabled;       // 是否启用
};

/**
 * @brief 负压系统参数 - 包含三路负压
 */
struct NegativePressureParams {
    NegativePressureChannel primary;    // 一级负压
    NegativePressureChannel secondary;  // 二级负压
    NegativePressureChannel cyclic;     // 循环负压
    double secondaryInkTime;      // 二级压墨时间 (秒)
    double secondaryReleaseTime;  // 二级释放时间 (秒)
    double inkSupplySpeed;        // 供墨泵转速设置 (r/min)
    double returnPumpSpeed;       // 回墨泵转速设置 (r/min)
};

// ========== 打印参数存储 ==========
struct PrintParams {
    int headCount;              // 喷头个数 (1~8)
    int dpi;                    // DPI (200~1600)
    int passCount;              // pass 数 (1~16)
    QList<int> leftRight;       // 左右值（像素，X 方向偏差矫正），N-1 个
    QList<int> overlapNozzle;   // 重叠孔（孔数，Y 方向），N-1 个
    QList<int> bidirectional;   // 双向值（像素，往返 pass 起点对齐），passCount-1 个
};


class SaveParams : public QObject
{
    Q_OBJECT

public:
    explicit SaveParams(QObject *parent = nullptr);
    ~SaveParams();

    // ===== 运动参数 =====
    void saveMotionParams(const MotionParams &params);
    MotionParams loadMotionParams();

    // ===== 波形参数 =====
    void saveWaveformParams(const WaveformData &params);
    WaveformData loadWaveformParams();

    // ===== 负压参数 =====
    void saveNegativePressure(const NegativePressureParams &params);
    NegativePressureParams loadNegativePressure();

    // ===== 打印参数 =====
    void savePrintParams(const PrintParams &params);
    PrintParams loadPrintParams();

    // ===== 通用功能 =====
    QString getConfigFilePath();  // 获取配置文件路径
    void clearAllSettings();      // 清除所有设置
    // ===== 上次打开的CLI文件 =====
    void saveLastCliFile(const QString &path);   // 保存上次打开的CLI文件路径
    QString loadLastCliFile();                   // 加载上次打开的CLI文件路径
private:
    QSettings *settings;

    // 默认值
    MotionParams getDefaultMotionParams();
    WaveformData getDefaultWaveformParams();
    NegativePressureParams getDefaultNegativePressure();
    PrintParams getDefaultPrintParams();
};

#endif // SAVEPARAMS_H


