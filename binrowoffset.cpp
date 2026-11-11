#include "binrowoffset.h"
#include <QtGlobal>

const QVector<int>& xingguangRankOffsets()
{
    static const QVector<int> offs = {0, 24, 88, 112, 306, 328, 392, 416};
    return offs;
}
/*
 * `src`：原始打包数据（来自 `layer_001.bin`，1bit 或 2bit 每像素）
 * `width/height/srcRowBytes`：原始画布尺寸（srcRowBytes 由生成端给出，别自己推）
 * `rankOffsets`：8 个排的横向偏移像素数 `{0,24,88,112,306,328,392,416}`
 * `out/outRowBytes`：输出参数（引用带回）
 * `swathRows = 1024`：一节多少行，默认 1024
 * `bitsPerPixel = 2`：源数据每像素位数，1 或 2
*/

bool applyRowOffsetBin(const QByteArray &src, int width, int height, int srcRowBytes,
                       const QVector<int> &rankOffsets,
                       QByteArray &out, int &outRowBytes, int swathRows, int bitsPerPixel)
{
    const int bpp = bin_bpp_norm(bitsPerPixel);
    // 入参校验 偏移表不能为空 偏移表不能为空
    if (rankOffsets.isEmpty() || width <= 0 || height <= 0 || srcRowBytes <= 0)
        return false;
    if (swathRows <= 0) swathRows = 1024;
    // 行字节数必须与位深匹配，否则下面的取像素会整体错位，宁可直接失败
    if (srcRowBytes < bin_row_bytes(width, bpp))
        return false;
    if (src.size() < srcRowBytes * height)
        return false;
    // 扫一遍偏移表取最大值 = 416。后面输出画布宽要按这个最大值扩
    int maxOffset = 0;
    for (int o : rankOffsets)
        maxOffset = qMax(maxOffset, o);

    const int outWidth  = maxOffset + width;    // 统一画布宽
    outRowBytes = bin_row_bytes(outWidth, bpp); // 2bpp=ceil/(4)，1bpp=ceil/(8)

    const int rankCount = rankOffsets.size();   // = 8
    // ===== 真实喷孔→排映射（替代原来的 row%8 占位）=====
    // 喷孔号 % 8 决定属于哪一排（每排128孔、间隔8）。
    // 依据：排1=8,16..1024(≡0)  排2=4(≡4)  排3=7(≡7)  排4=3(≡3)
    //       排5=5(≡5)  排6=1(≡1)  排7=6(≡6)  排8=2(≡2)
    static const int rankOfResidue[8] = {0, 5, 7, 3, 1, 4, 6, 2}; // 余数→排号(0基) 喷孔号余数对应数组中第几排
    // 行号与喷孔号的对应：喷孔号是1基(1..1024)。若 .bin 第0行 = 喷孔1 → residueShift=1；
    // 若第0行 = 喷孔0 → residueShift=0。默认按"第0行=喷孔1"。跑图后若第一排对不上，改这个值。
    const int residueShift = 1;

    // ===== 每 swathRows 行一节，节内按偏移分组重排；末节不足补0 =====
    // 默认 swathRows=1024 = 8排×128孔 = 喷头一整幅。
    // 每一节内部：该节全部行按 排号(0..7) 聚成 8 个连续块，
    // 块序反排：偏移0 的块放本组最底部，偏移24 在其上，……偏移416 放最顶部
    // （即从上到下 = 416,392,328,306,112,88,24,0），块内保持原行号从小到大。
    // 输出总行数 = ceil(height/swathRows)*swathRows，末节多出的行 = 背景(0)。
    const int swathCount = (height + swathRows - 1) / swathRows;
    const int outHeight  = swathCount * swathRows;
    out.resize(outRowBytes * outHeight);
    out.fill(0);                                // 全部置背景(0)
    char *raw = out.data();                 // QByteArray 可写指针（QByteRef 不能绑引用，直接用指针改）

    for (int s = 0; s < swathCount; ++s) {
        // 1) 统计本节每排的行数（补0占位行也计入，保证每块完整）
        int cnt[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (int local = 0; local < swathRows; ++local) {
            const int residue = (local + residueShift) % rankCount;
            cnt[rankOfResidue[residue]]++;
        }
        // 2) 每排连续块的起始输出行（前缀和）
        //    块序反排：rank g 的块放在 (rankCount-1-g) 位置，
        //    于是 偏移0(rank0) 落在本组最底部，偏移416(rank7) 落在最顶部。
        int groupStart[8];
        int acc = s * swathRows;
        for (int g = 0; g < rankCount; ++g) { groupStart[rankCount - 1 - g] = acc; acc += cnt[g]; }

        // 3) 逐行放入所属排的块内空位
        int cursor[8];
        for (int g = 0; g < rankCount; ++g) cursor[g] = groupStart[g];

        for (int local = 0; local < swathRows; ++local) {
            const int gRow = s * swathRows + local;   // 全局源行号
            if (gRow >= height) continue;             // 末节补0行：跳过（out 已填0）
            const int residue = (local + residueShift) % rankCount;
            const int rank    = rankOfResidue[residue];
            const int off     = rankOffsets[rank];    // 这排的偏移像素数
            const int srcRow  = gRow * srcRowBytes;   // 源行在src里的字节起点
            const int dstRow  = cursor[rank]++;       // 目标行 = 该排连续块下一个空位

            // 直接拿指针做整行搬运：省掉 QByteArray::at() 的每次边界检查
            const uchar *srcRowPtr = reinterpret_cast<const uchar *>(src.constData()) + srcRow;
            uchar *dstRowPtr = reinterpret_cast<uchar *>(raw) + dstRow * outRowBytes;

            for (int x = 0; x < outWidth; ++x) {
                if (x < off)      continue;         // 行首补的偏移区 = 背景(0)
                const int sx = x - off;
                if (sx >= width)  continue;         // 行尾补足区 = 背景(0)

                const uchar v = bin_pixel_get(srcRowPtr, sx, bpp);
                if (v == 0) continue;               // 背景不落笔（输出已清零）

                bin_pixel_or(dstRowPtr, x, v, bpp); // 1bpp 下非 0 即置 1，2bpp 保留 1/2/3
            }
        }
    }
    return true;
}

bool rotate90ccwBin(const QByteArray &src, int width, int height, int srcRowBytes,
                    QByteArray &out, int &outRowBytes, int bitsPerPixel)
{
    const int bpp = bin_bpp_norm(bitsPerPixel);
    if (width <= 0 || height <= 0 || srcRowBytes <= 0)
        return false;
    if (srcRowBytes < bin_row_bytes(width, bpp))
        return false;
    if (src.size() < srcRowBytes * height)
        return false;

    const int outWidth  = height;   // 旋转后宽 = 原高
    const int outHeight = width;    // 旋转后高 = 原宽
    outRowBytes = bin_row_bytes(outWidth, bpp);
    out.resize(outRowBytes * outHeight);
    out.fill(0);
    uchar *dst = reinterpret_cast<uchar *>(out.data());
    const uchar *srcBase = reinterpret_cast<const uchar *>(src.constData());

    for (int y = 0; y < height; ++y) {          // 原行 → 新列
        const uchar *srcRowPtr = srcBase + (size_t)y * srcRowBytes;
        for (int x = 0; x < width; ++x) {       // 原列 → 新行(翻转)
            const uchar v = bin_pixel_get(srcRowPtr, x, bpp);
            if (v == 0) continue;
            //const int dx = y;                   // 新列 = 原行
            const int dy = width - 1 - x;       // 新行 = 原宽翻转
	    const int dx = height - 1 - y;   // 新列 = 原高翻转：底行(y=0)→最右列
	    //const int dy = x;                // 新行 = 原列：左列(x=0)→底部

            bin_pixel_or(dst + (size_t)dy * outRowBytes, dx, v, bpp);
        }
    }
    return true;
}

#ifdef BINROWOFFSET_STANDALONE
// 独立自测：读入 .bin → 排偏移 → 90°逆时针旋转
// 输出: 前缀_off.bin/_off.png（偏移后）、前缀_rot.bin/_rot.png（旋转后）
// 用法: binrowoffset <输入.bin> <宽度> <高度> [输出前缀] [--bpp 1|2]
//   输入位深默认 2，与生成端保持同一位深才能对上像素；
//   输出前缀缺省 = 去掉输入文件扩展名，例如 layer_001.bin -> layer_001_off/rot.bin/_off/rot.png
#include <QFile>
#include <QTextStream>
#include <QGuiApplication>
#include <QImage>

static int read_bin_pixel(const QByteArray &src, int rowBytes, int x, int y, int bpp)
{
    const unsigned char *row = reinterpret_cast<const unsigned char *>(src.constData())
                               + (size_t)y * rowBytes;
    return bin_pixel_get(row, x, bpp);
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);   // 需要应用实例才能加载 PNG 图像插件

    if (argc < 4) {
        QTextStream(stderr) << "用法: binrowoffset <输入.bin> <宽度> <高度> [输出前缀] [--bpp 1|2]\n";
        return 1;
    }
    const QString inPath  = QString::fromLocal8Bit(argv[1]);
    const int     width   = QString::fromLocal8Bit(argv[2]).toInt();
    const int     height  = QString::fromLocal8Bit(argv[3]).toInt();

    int bpp = BIN_BPP_2;
    QString outBaseArg;
    for (int i = 4; i < argc; ++i) {
        if (strcmp(argv[i], "--bpp") == 0 && i + 1 < argc) {
            bpp = bin_bpp_norm(atoi(argv[++i]));
        } else if (outBaseArg.isEmpty()) {
            outBaseArg = QString::fromLocal8Bit(argv[i]);
        }
    }
    const QString outBase = outBaseArg.isEmpty()
                            ? inPath.left(inPath.lastIndexOf(QLatin1Char('.')))
                            : outBaseArg;

    QFile f(inPath);
    if (!f.open(QIODevice::ReadOnly)) {
        QTextStream(stderr) << "无法打开输入: " << inPath << "\n";
        return 1;
    }
    const QByteArray src = f.readAll();
    f.close();

    const int srcRowBytes = bin_row_bytes(width, bpp);
    QByteArray out;
    int outRowBytes = 0;
    if (!applyRowOffsetBin(src, width, height, srcRowBytes,
                           xingguangRankOffsets(), out, outRowBytes, 1024, bpp)) {
        QTextStream(stderr) << "变换失败（检查数据尺寸/位深）\n";
        return 1;
    }

    // ---- 写 _off.bin ----
    const QString binPath = outBase + QString("_off.bin");
    QFile fo(binPath);
    if (!fo.open(QIODevice::WriteOnly)) {
        QTextStream(stderr) << "无法写出: " << binPath << "\n";
        return 1;
    }
    fo.write(out);
    fo.close();

    // ---- 写 _off.png（8 位灰度：0=白不喷, 1=黑喷印, 2/3=灰）----
    // .bin 自底向上：第0行=物理底部。PNG scanLine(0)=顶部，所以上下翻转，
    // 让 .bin 第0行(排1,偏移0)显示在最底部，与 App 预览一致。
    const int outWidth  = width + 416;
    const int outHeight = out.size() / outRowBytes;   // 补0后的输出行数
    QImage img(outWidth, outHeight, QImage::Format_Grayscale8);
    for (int y = 0; y < outHeight; ++y) {
        uchar *line = img.scanLine(outHeight - 1 - y);   // 底部 = .bin 第0行
        for (int x = 0; x < outWidth; ++x) {
            const int v = read_bin_pixel(out, outRowBytes, x, y, bpp);
            // 映射：0→255(白), 1→0(黑), 2→85, 3→170
            line[x] = v == 0 ? 255
                    : v == 1 ? 0
                    : static_cast<uchar>(255 - v * 85);
        }
    }
    const QString pngPath = outBase + QString("_off.png");
    if (!img.save(pngPath)) {
        QTextStream(stderr) << "PNG 写出失败: " << pngPath << "\n";
        return 1;
    }

    // ---- 方案1：偏移后跟着 90° 逆时针旋转 ----
    // 旋转后：宽 = 补0后的行数(outHeight)，高 = 原宽+416。
    QByteArray rot;
    int rotRowBytes = 0;
    if (!rotate90ccwBin(out, outWidth, outHeight, outRowBytes, rot, rotRowBytes, bpp)) {
        QTextStream(stderr) << "旋转失败\n";
        return 1;
    }

    // ---- 写 _rot.bin ----
    const QString rotBinPath = outBase + QString("_rot.bin");
    QFile fr(rotBinPath);
    if (!fr.open(QIODevice::WriteOnly)) {
        QTextStream(stderr) << "无法写出: " << rotBinPath << "\n";
        return 1;
    }
    fr.write(rot);
    fr.close();

    // ---- 写 _rot.png（直接按存储行序显示：宽=补0后行数，高=原宽+416）----
    const int rotWidth  = outHeight;
    const int rotHeight = outWidth;   // = 原宽+416
    QImage rimg(rotWidth, rotHeight, QImage::Format_Grayscale8);
    for (int y = 0; y < rotHeight; ++y) {
        uchar *line = rimg.scanLine(y);
        for (int x = 0; x < rotWidth; ++x) {
            const int v = read_bin_pixel(rot, rotRowBytes, x, y, bpp);
            line[x] = v == 0 ? 255
                    : v == 1 ? 0
                    : static_cast<uchar>(255 - v * 85);
        }
    }
    const QString rotPngPath = outBase + QString("_rot.png");
    if (!rimg.save(rotPngPath)) {
        QTextStream(stderr) << "PNG 写出失败: " << rotPngPath << "\n";
        return 1;
    }

    QTextStream(stdout) << "完成:\n"
                        << "  " << binPath << " (偏移后 " << bpp << "bpp, " << outWidth << "x" << outHeight
                        << ", 每行" << outRowBytes << "字节)\n"
                        << "  " << pngPath << " (偏移后预览)\n"
                        << "  " << rotBinPath << " (旋转后 " << bpp << "bpp, " << rotWidth << "x" << rotHeight
                        << ", 每行" << rotRowBytes << "字节)\n"
                        << "  " << rotPngPath << " (旋转后预览)\n";
    return 0;
}
#endif
//（注：内容由AI生成）
