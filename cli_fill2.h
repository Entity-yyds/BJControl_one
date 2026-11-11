#ifndef CLI_FILL2_H
#define CLI_FILL2_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int layer_count;
    int width;
    int height;
    int row_bytes;
    int bits_per_pixel;   /* 本次生成的 .bin 每像素位数：1 或 2 */
    double x_min;
    double x_max;
    double y_min;
    double y_max;
} CliFillResult;

/*
 * Converts every non-empty layer to a packed layer_NNN.bin file.
 *
 * bits_per_pixel:
 *   2 = 2bit/pixel（4 像素/字节，0 背景、1 黑、2/3 灰度），row_bytes = (w+3)/4；
 *   1 = 1bit/pixel（8 像素/字节，1 喷印、0 背景），row_bytes = (w+7)/8。
 *   传其它值按 2 处理（兼容旧调用）。
 *
 * result->row_bytes / result->bits_per_pixel 与实际写出的 .bin 一致，
 * 下游（排偏移、预览、发送）必须用这两个值，不能再按硬编码的 (w+3)/4 推断。
 */
int cli_fill2_convert(const char *input_path, const char *outdir, double dpi,
                      int bits_per_pixel, CliFillResult *result);

#ifdef __cplusplus
}
#endif

#endif
