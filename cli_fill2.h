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
    double x_min;
    double x_max;
    double y_min;
    double y_max;
} CliFillResult;

/* Converts every non-empty layer to a packed 2-bit layer_NNN.bin file. */
int cli_fill2_convert(const char *input_path, const char *outdir, double dpi,
                      CliFillResult *result);

#ifdef __cplusplus
}
#endif

#endif
