#ifndef BINFORMAT_H
#define BINFORMAT_H

/*
 * binformat.h —— .bin / tmp.bin 的像素打包格式（1bit 或 2bit 每像素）
 *
 * 实现C C++ 共用内容
 *
 * 为什么要单独抽一个头：
 *   cli_fill2.c 是纯 C（qmake 里按 C 编译），clifillworker / binrowoffset /
 *   mainwindow 是 C++。位序、每行字节数、取像素/写像素的公式必须四处完全一致，
 *   否则生成端和发送端会错位一格。这里做成 C/C++ 通用的 static inline 函数，
 *   谁改都只改这一份。
 *
 * 两种格式（都是 MSB 优先、无文件头、行 0 = y_min 底部）：
 *   1bpp：1 字节 = 8 像素，bit=1 表示喷印，bit=0 表示背景；
 *         每行字节数 row_bytes = (width + 7) / 8
 *   2bpp：1 字节 = 4 像素（每像素 2 bit，取值 0~3），0=背景，非 0=喷印（1 黑，2/3 灰度）；
 *         每行字节数 row_bytes = (width + 3) / 4
 *
 * 注意：2bpp 下"喷印"的判定是 v != 0，不是 v == 1，历史上发送端就是这么处理的。
 */

#ifdef __cplusplus
extern "C" {
#endif

#define BIN_BPP_1 1
#define BIN_BPP_2 2

/* 归一化位深：只接受 1/2，其它值一律按 2 处理（保持原工程行为） */
static inline int bin_bpp_norm(int bpp)
{
    return (bpp == BIN_BPP_1) ? BIN_BPP_1 : BIN_BPP_2;
}

/* 每行字节数：1bpp 8 像素/字节，2bpp 4 像素/字节，均向上取整 */
static inline int bin_row_bytes(int width, int bpp)
{
    if (bin_bpp_norm(bpp) == BIN_BPP_1)
        return (width + 7) / 8;
    return (width + 3) / 4;
}

/* 取一个像素，返回 0~1（1bpp）或 0~3（2bpp）。row 指向该行首字节 */
static inline unsigned char bin_pixel_get(const unsigned char *row, int x, int bpp)
{
    if (bin_bpp_norm(bpp) == BIN_BPP_1)
        return (unsigned char)((row[x >> 3] >> (7 - (x & 7))) & 0x01);

    return (unsigned char)((row[x >> 2] >> (6 - (x & 3) * 2)) & 0x03);
}

/* 覆盖写一个像素（先清目标位再写）。1bpp 下 v 非 0 即视为 1 */
static inline void bin_pixel_set(unsigned char *row, int x, unsigned char v, int bpp)
{
    if (bin_bpp_norm(bpp) == BIN_BPP_1) {
        const unsigned char mask = (unsigned char)(1u << (7 - (x & 7)));
        if (v)
            row[x >> 3] = (unsigned char)(row[x >> 3] | mask);
        else
            row[x >> 3] = (unsigned char)(row[x >> 3] & (unsigned char)~mask);
        return;
    }

    const int shift = 6 - (x & 3) * 2;
    const unsigned char mask = (unsigned char)~(0x03u << shift);
    row[x >> 2] = (unsigned char)((row[x >> 2] & mask) | ((v & 0x03) << shift));
}

/*
 * 只置位不清位（OR 语义）。排偏移/旋转这类"目标缓冲区已清零、逐像素落笔"的场景用这个，
 * 比 bin_pixel_set 少一次读改写，也避免把已写入的像素误清。
 */
static inline void bin_pixel_or(unsigned char *row, int x, unsigned char v, int bpp)
{
    if (bin_bpp_norm(bpp) == BIN_BPP_1) {
        if (v != 0)     // 非 0 即置位（与 2bpp 的"非 0 即喷"语义对齐）
            row[x >> 3] = (unsigned char)(row[x >> 3] | (unsigned char)(1u << (7 - (x & 7))));
        return;
    }

    if (v == 0)
        return;
    const int shift = 6 - (x & 3) * 2;
    row[x >> 2] = (unsigned char)(row[x >> 2] | ((v & 0x03) << shift));
}

/*
 * 把像素值规约成"喷 / 不喷"（1bit 语义）。
 *   1bpp：原样返回 bit
 *   2bpp：非 0 一律算喷印
 * 发送端（FPGA 按 1024 喷孔 = 128 字节 1bpp 收数据）统一用它做判定。
 */
static inline unsigned char bin_pixel_jet(unsigned char v, int bpp)
{
    if (bin_bpp_norm(bpp) == BIN_BPP_1)
        return (unsigned char)(v & 0x01);
    return (unsigned char)(v != 0);
}

#ifdef __cplusplus
}
#endif

#endif // BINFORMAT_H
