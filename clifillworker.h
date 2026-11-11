#ifndef CLIFILLWORKER_H
#define CLIFILLWORKER_H

#include <QObject>
#include <QString>

class CliFillWorker : public QObject
{
    Q_OBJECT
public:
    /*
     * bitsPerPixel：生成的 layer_NNN.bin / layer_NNN_tmp.bin 每像素位数
     *   1 = 1bit/pixel（单色，体积减半）
     *   2 = 2bit/pixel（原行为，保留 1/2/3 灰度级）
     * 传其它值按 2 处理。
     */
    CliFillWorker(const QString &inputPath, const QString &outputPath,
                  double dpi, int bitsPerPixel = 2, QObject *parent = nullptr);

public slots:
    void run();

signals:
    // 末位 bpp 是本轮实际用到的位深，主界面据此写 meta.txt / 按对应格式读预览
    void finished(int status, int layerCount, int width, int height, int rowBytes, int bpp);

private:
    QString inputPath;
    QString outputPath;
    double dpi;
    int bitsPerPixel;
};

#endif
