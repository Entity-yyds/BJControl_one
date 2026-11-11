#ifndef BINROWOFFSET_H
#define BINROWOFFSET_H

#include <QByteArray>
#include <QVector>
#include "binformat.h"      /* 1bit/2bit 每行字节数、取像素/写像素的唯一来源 */

// 星光喷头 8 排的横向偏移（像素）。索引 0 = 第 1 排。
// 排1=0  排2=24  排3=88  排4=112  排5=306  排6=328  排7=392  排8=416
const QVector<int>& xingguangRankOffsets();

/*
 * 对 .bin（1bit 或 2bit 每像素）做"每 swathRows 行一组的分组重排 + 行首补偏移像素"变换。
 *
 * 背景：
 *   星光喷头喷嘴是错排（交错）排列的，每一"排"的喷孔在横向上有固定偏移。
 *   为了把各排的数据在横向上对齐 / 或生成喷头需要的错位数据，
 *   需要在每一行的行首补上该排对应的偏移个像素（背景值 0）。
 *
 * 分组重排（本函数的输出结构）：
 *   输入按 swathRows 行（默认 1024 = 8排 × 128孔 = 喷头一整幅）切成一节一节，
 *   每一节内部：该节所有行按 排号(0..7) 聚成 8 个连续块，
 *   块序反排：偏移0 的块放本组最底部，偏移24 在其上，……偏移416 放最顶部
 *   （即从上到下 = 416,392,328,306,112,88,24,0）。
 *   块内保持原行号从小到大；每块行首仍补该排偏移像素。
 *   一节排完再排下一节，依次类推。
 *   最后一节不足 swathRows 行时，按背景(0)补足到 swathRows 行再分组，
 *   因此输出总行数 = ceil(height / swathRows) * swathRows。
 *
 * 输入：
 *   src         原始 .bin 数据（MSB 优先，无文件头）
 *   width       每行像素数
 *   height      总行数
 *   srcRowBytes 原始每行字节数，必须用生成端给的值：
 *                 2bpp = ceil(width/4)，1bpp = ceil(width/8)
 *   rankOffsets 每"排"的偏移像素数，长度 = 排数（这里为 8）
 *   swathRows   每节行数（建议为排数的整数倍，默认 1024）
 *   bitsPerPixel 源数据每像素位数：1 或 2（其它值按 2 处理）
 *
 * 输出：
 *   out         变换后的同格式数据，统一画布宽 = maxOffset + width，
 *               高 = ceil(height/swathRows)*swathRows，
 *               每一行放在自己偏移处（行首补 0），行尾补 0
 *   outRowBytes 输出每行字节数 = 2bpp 时 ceil((maxOffset+width)/4)，
 *                                 1bpp 时 ceil((maxOffset+width)/8)
 *
 * 行→排的对应关系（真实映射）：
 *   喷孔号 % 8 决定属于哪一排（每排128孔、间隔8）：
 *     余数0→排1  余数1→排6  余数2→排8  余数3→排4
 *     余数4→排2  余数5→排5  余数6→排7  余数7→排3
 *   即 rankOfResidue[8] = {0,5,7,3,1,4,6,2}。
 *   行号与喷孔号按 1 基对应（.bin 第0行=喷孔1），由 residueShift 微调。
 *
 * @return 成功 true
 */
bool applyRowOffsetBin(const QByteArray &src, int width, int height, int srcRowBytes,
                       const QVector<int> &rankOffsets,
                       QByteArray &out, int &outRowBytes,
                       int swathRows = 1024, int bitsPerPixel = 2);

/*
 * 90° 逆时针旋转（把整幅数据旋转 90° CCW），同样支持 1bit / 2bit 每像素。
 *   src          原始打包数据（width × height，MSB 优先）
 *   bitsPerPixel 每像素位数：1 或 2（其它值按 2 处理）
 *   旋转后：宽 = 原高，高 = 原宽
 *   映射：原像素 (x, y) → 新位置 (x'=height-1-y, y'=width-1-x)
 *         即"最底行 → 最右列"、每行从左到右对应列里从下到上。
 *   out          旋转后的同格式数据
 *   outRowBytes  输出每行字节数 = 2bpp 时 ceil(height/4)，1bpp 时 ceil(height/8)
 * @return 成功 true
 */
bool rotate90ccwBin(const QByteArray &src, int width, int height, int srcRowBytes,
                    QByteArray &out, int &outRowBytes, int bitsPerPixel = 2);

#endif // BINROWOFFSET_H
