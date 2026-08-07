#include "saveparams.h"
#include <QDebug>
#include <QDir>

SaveParams::SaveParams(QObject *parent)
    : QObject(parent)
{
    // 初始化 QSettings
    settings = new QSettings("BJPrinter", "SaveParams", this);
    qDebug() << "配置文件路径:" << settings->fileName();
}

SaveParams::~SaveParams()
{
    // QSettings 会被 Qt 自动清理
}

// ==================== 运动参数 ====================

void SaveParams::saveMotionParams(const MotionParams &params)
{
    settings->beginGroup("MotionParams");
    settings->setValue("speedX", params.speedX);
    settings->setValue("speedY", params.speedY);
    settings->setValue("speedZ", params.speedZ);
    settings->setValue("acceleration", params.acceleration);
    settings->setValue("jerk", params.jerk);
    settings->setValue("layerHeight", params.layerHeight);
    settings->endGroup();

    qDebug() << "运动参数已保存";
}

MotionParams SaveParams::loadMotionParams()
{
    MotionParams params = getDefaultMotionParams();

    settings->beginGroup("MotionParams");
    params.speedX = settings->value("speedX", params.speedX).toDouble();
    params.speedY = settings->value("speedY", params.speedY).toDouble();
    params.speedZ = settings->value("speedZ", params.speedZ).toDouble();
    params.acceleration = settings->value("acceleration", params.acceleration).toDouble();
    params.jerk = settings->value("jerk", params.jerk).toDouble();
    params.layerHeight = settings->value("layerHeight", params.layerHeight).toDouble();
    settings->endGroup();

    qDebug() << "运动参数已加载:"
             << "X=" << params.speedX
             << "Y=" << params.speedY
             << "Z=" << params.speedZ
             << "accel=" << params.acceleration
             << "jerk=" << params.jerk
             << "layer=" << params.layerHeight;

    return params;
}

// ==================== 波形参数 ====================

void SaveParams::saveWaveformParams(const WaveformData &params)
{
    settings->beginGroup("WaveformParams");
    settings->setValue("voltage", params.voltage);
    settings->setValue("riseTime", params.riseTime);
    settings->setValue("dwellTime", params.dwellTime);
    settings->setValue("fallTime", params.fallTime);
    settings->setValue("frequency", params.frequency);
    settings->endGroup();

    qDebug() << "波形参数已保存";
}

WaveformData SaveParams::loadWaveformParams()
{
    WaveformData params = getDefaultWaveformParams();

    settings->beginGroup("WaveformParams");
    params.voltage = settings->value("voltage", params.voltage).toDouble();
    params.riseTime = settings->value("riseTime", params.riseTime).toDouble();
    params.dwellTime = settings->value("dwellTime", params.dwellTime).toDouble();
    params.fallTime = settings->value("fallTime", params.fallTime).toDouble();
    params.frequency = settings->value("frequency", params.frequency).toDouble();
    settings->endGroup();

    qDebug() << "波形参数已加载:"
             << "V=" << params.voltage
             << "rise=" << params.riseTime
             << "dwell=" << params.dwellTime
             << "fall=" << params.fallTime
             << "freq=" << params.frequency;

    return params;
}

// ==================== 负压参数 ====================

void SaveParams::saveNegativePressure(const NegativePressureParams &params)
{
    settings->beginGroup("NegativePressure");
    settings->setValue("pressure", params.pressure);
    settings->setValue("enabled", params.enabled);
    settings->endGroup();

    qDebug() << "负压参数已保存";
}

NegativePressureParams SaveParams::loadNegativePressure()
{
    NegativePressureParams params = getDefaultNegativePressure();

    settings->beginGroup("NegativePressure");
    params.pressure = settings->value("pressure", params.pressure).toDouble();
    params.enabled = settings->value("enabled", params.enabled).toBool();
    settings->endGroup();

    qDebug() << "负压参数已加载:"
             << "pressure=" << params.pressure
             << "enabled=" << params.enabled;

    return params;
}

// ==================== 默认值 ====================

MotionParams SaveParams::getDefaultMotionParams()
{
    MotionParams params;
    params.speedX = 300.0;
    params.speedY = 300.0;
    params.speedZ = 50.0;
    params.acceleration = 1000.0;
    params.jerk = 5.0;
    params.layerHeight = 0.1;
    return params;
}

WaveformData SaveParams::getDefaultWaveformParams()
{
    WaveformData params;
    params.voltage = 45.0;
    params.riseTime = 2.0;
    params.dwellTime = 6.0;
    params.fallTime = 2.0;
    params.frequency = 20.0;
    return params;
}

NegativePressureParams SaveParams::getDefaultNegativePressure()
{
    NegativePressureParams params;
    params.pressure = -50.0;
    params.enabled = false;
    return params;
}

// ==================== 通用功能 ====================

QString SaveParams::getConfigFilePath()
{
    return settings->fileName();
}

void SaveParams::clearAllSettings()
{
    settings->clear();
    qDebug() << "所有设置已清除";
}
