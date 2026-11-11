#include "saveparams.h"
#include <QDebug>
#include <QDir>

SaveParams::SaveParams(QObject *parent)
    : QObject(parent)
{
    // 初始化 QSettings
    settings = new QSettings("BJPrinter", "SaveParams", this);
    // 监听 settings 的销毁事件
    // 用 Qt::DirectConnection 确保立即输出
        connect(this, &QObject::destroyed, this, [this]() {
            qDebug() << "🔥 SaveParams 自己被销毁了！" ;
            // 此时 settings 可能还存在，但马上会被删除
            // settings = nullptr; // 如果在这里设置，外面还能看到吗？
        }, Qt::DirectConnection);

    qDebug() << "配置文件路径:" << settings->fileName();
}

SaveParams::~SaveParams()
{
    qDebug() << "=== SaveParams 析构函数被调用 ===";
    // QSettings 会被 Qt 自动清理，不需要手动 delete
    // 但如果你想确认 settings 被删除了，可以检查指针
    // 检查 settings 的状态
    if (settings) {
        qDebug() << "  settings 指针地址:" << settings;
        qDebug() << "  settings 父对象:" << settings->parent();
        //qDebug() << "  settings 是否正在被销毁?" << this->isBeingDestroyed();
    } else {
        qDebug() << "  settings 已经是 nullptr";
    }
    qDebug() << "=== SaveParams 析构完成 ===";
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
    // 一级负压
    settings->setValue("primary/target", params.primary.target);
    // 二级负压
    settings->setValue("secondary/target", params.secondary.target);
    // 循环负压
    settings->setValue("cyclic/target", params.cyclic.target);
    // 二级压墨/释放时间
    settings->setValue("secondary/inkTime", params.secondaryInkTime);
    settings->setValue("secondary/releaseTime", params.secondaryReleaseTime);
    // 转速默认值
    settings->setValue("pump/inkSupplySpeed", params.inkSupplySpeed);
    settings->setValue("pump/returnPumpSpeed", params.returnPumpSpeed);
    settings->endGroup();

    qDebug() << "负压参数已保存";
}

NegativePressureParams SaveParams::loadNegativePressure()
{
    NegativePressureParams params = getDefaultNegativePressure();

    settings->beginGroup("NegativePressure");
    // 一级负压
    params.primary.target = settings->value("primary/target", params.primary.target).toDouble();
    // 二级负压
    params.secondary.target = settings->value("secondary/target", params.secondary.target).toDouble();
    // 循环负压
    params.cyclic.target = settings->value("cyclic/target", params.cyclic.target).toDouble();
    params.secondaryInkTime = settings->value("secondary/inkTime", params.secondaryInkTime).toDouble();
    params.secondaryReleaseTime = settings->value("secondary/releaseTime", params.secondaryReleaseTime).toDouble();
    params.inkSupplySpeed = settings->value("pump/inkSupplySpeed", params.inkSupplySpeed).toDouble();
    params.returnPumpSpeed = settings->value("pump/returnPumpSpeed", params.returnPumpSpeed).toDouble();
    settings->endGroup();

    qDebug() << "负压参数已加载:"
                << "一级=" << params.primary.target
                << "二级=" << params.secondary.target
                << "循环=" << params.cyclic.target;
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
    params.primary.target = -12.0;
    params.primary.current = 0.0;


    params.secondary.target = -4.0;
    params.secondary.current = 0.0;


    params.cyclic.target = -5.0;
    params.cyclic.current = 0.0;

    params.secondaryInkTime = 5.0;      // 默认压墨 5 秒
    params.secondaryReleaseTime = 1.0;  // 默认释放 1 秒
    params.inkSupplySpeed = 0.0;        // 供墨转速
    params.returnPumpSpeed = 0.0;       // 回墨转速
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

// ==================== 上次打开的CLI文件 ====================
void SaveParams::saveLastCliFile(const QString &path)
{
    settings->setValue("LastCliFile/path", path);
}

QString SaveParams::loadLastCliFile()
{
    return settings->value("LastCliFile/path").toString();
}
// ==================== 打印参数 ====================
void SaveParams::savePrintParams(const PrintParams &params)
{
    settings->beginGroup("PrintParams");
    settings->setValue("headCount", params.headCount);
    settings->setValue("dpi", params.dpi);
    settings->setValue("passCount", params.passCount);
    settings->setValue("bitsPerPixel", params.bitsPerPixel);   // 1=单色 2bit=灰度
    // 转 QStringList 存（避免 QVariant 对 QList<int> 的兼容问题）
    QStringList lr, ov, bi;
    for (int v : params.leftRight)      lr << QString::number(v);
    for (int v : params.overlapNozzle)  ov << QString::number(v);
    for (int v : params.bidirectional)  bi << QString::number(v);
    settings->setValue("leftRight", lr);
    settings->setValue("overlapNozzle", ov);
    settings->setValue("bidirectional", bi);
    settings->endGroup();

    qDebug() << "打印参数已保存";
}

PrintParams SaveParams::loadPrintParams()
{
    PrintParams params = getDefaultPrintParams();

    settings->beginGroup("PrintParams");
    params.headCount = settings->value("headCount", params.headCount).toInt();
    params.dpi       = settings->value("dpi", params.dpi).toInt();
    params.passCount = settings->value("passCount", params.passCount).toInt();
    // 只接受 1/2，旧配置里没有这个键时回落到默认 2
    params.bitsPerPixel = settings->value("bitsPerPixel", params.bitsPerPixel).toInt();
    if (params.bitsPerPixel != 1) params.bitsPerPixel = 2;

    params.leftRight.clear();
    const QStringList lr = settings->value("leftRight").toStringList();
    for (const QString &s : lr) params.leftRight << s.toInt();

    params.overlapNozzle.clear();
    const QStringList ov = settings->value("overlapNozzle").toStringList();
    for (const QString &s : ov) params.overlapNozzle << s.toInt();

    params.bidirectional.clear();
    const QStringList bi = settings->value("bidirectional").toStringList();
    for (const QString &s : bi) params.bidirectional << s.toInt();
    settings->endGroup();

    return params;
}

PrintParams SaveParams::getDefaultPrintParams()
{
    PrintParams p;
    p.headCount = 1;        // 单喷头
    p.dpi       = 600;
    p.passCount = 1;        // 单 pass，默认无双向值
    p.bitsPerPixel = 2;     // 默认 2bit/pixel，与原工程行为一致
    p.leftRight.clear();
    p.overlapNozzle.clear();
    p.bidirectional.clear();
    return p;
}

