#include "clifillworker.h"
#include "cli_fill2.h"
#include "binrowoffset.h"
#include <QFile>
#include <QImage>

CliFillWorker::CliFillWorker(const QString &inputPath, const QString &outputPath,
                             double dpi, QObject *parent)
    : QObject(parent)
    , inputPath(inputPath)
    , outputPath(outputPath)
    , dpi(dpi)
{
}

// 从 2bpp 数据生成灰度 png（8 位灰度：0=白背景, 1=黑, 2/3=灰阶）
static bool save2bppPng(const QByteArray &rot, int width, int height, int rowBytes,
                        const QString &pngPath)
{
    if (width <= 0 || height <= 0 || rowBytes <= 0) return false;
    QImage img(width, height, QImage::Format_Grayscale8);
    img.fill(255);
    const int dataSize = rowBytes * height;
    for (int y = 0; y < height; ++y) {
        uchar *line = img.scanLine(y);
        const int rowStart = y * rowBytes;
        for (int x = 0; x < width; ++x) {
            const int byteIdx = rowStart + x / 4;
            const uchar v = (byteIdx < dataSize && byteIdx < rot.size())
                ? (static_cast<uchar>(rot.at(byteIdx)) >> (6 - (x % 4) * 2)) & 0x03
                : 0;
            line[x] = v == 0 ? 255 : static_cast<uchar>(255 - v * 85);
        }
    }
    return img.save(pngPath);
}

/*
 * 对单层 .bin 做星光喷头后处理：排偏移(applyRowOffset2bpp) → 90°逆时针旋转(rotate90ccw2bpp)。
 *
 * 命名规则：
 *   layer_XXX.bin       = CLI 填充后的原始切片（保留不变，UI 层预览读它）
 *   layer_XXX.tmp.bin   = 后处理结果（偏移+旋转，供打印发送）
 *   layer_XXX.tmp.png   = 后处理结果的可视化预览图
 *
 * 后处理不覆盖/不改名原始 .bin，两者独立并存。
 * dir ：切片输出目录
 * ayerIdx ：第几层，从 1 开始
 * cli_fill2_convert： 返回的原始画布尺寸 width/height/rowBytes
 */
static bool postProcessLayerBin(const QString &dir, int layerIdx,
                                int width, int height, int rowBytes)
{   // CLI 输出文件命名的规则
    const QString binPath = dir + QString("/layer_%1.bin").arg(layerIdx, 3, 10, QChar('0'));    // 拼出要读的原始切片路径 `layerIdx`：要格式化的数字 `3`：**最少宽度 3 位** `10`：十进制 `QChar('0')`：不足位用 `'0'` 补

    QFile in(binPath);      // 打开原始 `.bin`，打不开直接 false
    if (!in.open(QIODevice::ReadOnly)) return false;
    const QByteArray src = in.readAll();        // 一次性把整个 `.bin` 读进内存。这一层 2bpp 数据可能几 MB，一次读完最简单。
    in.close();                                 //     读完立刻 `close()`，不占着文件句柄。
    if (src.size() < rowBytes * height) return false;   // **尺寸校验**：读到的字节数至少要够 `rowBytes × height` 多了不管（可能有尾部填充

    // 1) 排偏移 + 每1024行分组重排（末节补0）：
    //    统一画布宽 = maxOffset + width；高 = ceil(height/1024)*1024
    int maxOffset = 0;              // 遍历一遍取最大值 → `maxOffset = 416`
    const QVector<int> &offs = xingguangRankOffsets();          // `xingguangRankOffsets()` 返回那个硬编码数组 `{0, 24, 88, 112, 306, 328, 392, 416}`；
    for (int o : offs) maxOffset = qMax(maxOffset, o);

    QByteArray off;
    int offRowBytes = 0;
    // 输入：原始数据 `src` + 原始尺寸 + 8 个排偏移
    // 输出：新的 2bpp 数据 `off` + 新的每行字节数 `offRowBytes`
    // 这一步做的事情：每 1024 行一组，节内按喷孔排把行重新排列，行首补该排偏移像素
    if (!applyRowOffset2bpp(src, width, height, rowBytes, offs, off, offRowBytes))  // 输入：原始数据 `src` + 原始尺寸 + 8 个排偏移
        return false;

    // 补0后的实际行数（applyRowOffset 的输出高度，不再等于原 height）
    const int offHeight = off.size() / offRowBytes;

    // 2) 90° 逆时针旋转：宽 = offHeight，高 = 原宽 + maxOffset
    QByteArray rot;
    int rotRowBytes = 0;
    if (!rotate90ccw2bpp(off, width + maxOffset, offHeight, offRowBytes, rot, rotRowBytes))
        return false;

    // 3) 后处理结果独立命名 layer_XXX.tmp.bin（不覆盖原始 .bin）
    const QString tmpPath = dir + QString("/layer_%1_tmp.bin").arg(layerIdx, 3, 10, QChar('0'));
    QFile out(tmpPath);
    if (!out.open(QIODevice::WriteOnly)) return false;
    out.write(rot);
    out.close();

    // 4) 由后处理结果生成 png 预览图（宽=offHeight，高=原宽+maxOffset）
    save2bppPng(rot, offHeight, width + maxOffset, rotRowBytes,
                dir + QString("/layer_%1_tmp.png").arg(layerIdx, 3, 10, QChar('0')));
    return true;
}

void CliFillWorker::run()
{
    CliFillResult result = {};      // 是 C++ 值初始化，把它们全部清
    const QByteArray input = QFile::encodeName(inputPath);      // 转成 `const char*` 给 C 函数用 防止误改
    const QByteArray output = QFile::encodeName(outputPath);
    const int status = cli_fill2_convert(input.constData(), output.constData(), dpi, &result);

    // ===== .bin 后处理：星光喷头排偏移 + 90°逆时针旋转 =====
    // 命名规则：
    //   layer_XXX.bin     = CLI 填充后的原始切片（保留不变，UI 层预览读它）
    //   layer_XXX.tmp.bin = 后处理结果（偏移+旋转，供打印发送）
    //   layer_XXX.tmp.png = 后处理结果的可视化预览图
    if (status == 0 && result.layer_count > 0 && result.width > 0 && result.height > 0) {   // 只有 "切片真的成功了" 才做后处理
        const int rawW = result.width;      // 原始画布宽（像素）
        const int rawH = result.height;     // 原始画布高（像素）
        const int rawRB = result.row_bytes; // 原始每行字节数 = ceil(rawW/4)

        // 逐层生成 tmp.bin + tmp.png（原始 layer_XXX.bin 保持不变，供 UI 预览）
        for (int i = 1; i <= result.layer_count; ++i) {
            postProcessLayerBin(outputPath, i, rawW, rawH, rawRB);
        }
        // result 保持 cli_fill2 返回的原始尺寸：预览读的是 layer_XXX.bin（原始切片）
    }
    // 接收端是 onCliConversionFinished `status != 0` → 弹窗报错； `status == 0` → 写 `meta.txt`，`applySliceResult` 刷新预览
    // 同时 触发 `QThread::finished` → `quit` → 线程退出
    emit finished(status, result.layer_count, result.width, result.height, result.row_bytes);
}



