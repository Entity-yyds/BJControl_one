#ifndef SAVEPARAMS_H
#define SAVEPARAMS_H

#include <QObject>
#include <QSettings>

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
struct NegativePressureParams {
    double pressure;
    bool enabled;
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

    // ===== 通用功能 =====
    QString getConfigFilePath();  // 获取配置文件路径
    void clearAllSettings();      // 清除所有设置

private:
    QSettings *settings;

    // 默认值
    MotionParams getDefaultMotionParams();
    WaveformData getDefaultWaveformParams();
    NegativePressureParams getDefaultNegativePressure();
};

#endif // SAVEPARAMS_H


