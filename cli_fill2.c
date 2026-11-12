/*
 * cli_fill.c — 读取 CLI 切片文件，将每一层轮廓扫描线填充为 1bit / 2bit 位图
 *
 * 功能：
 *   - 自动检测 ASCII / 二进制 CLI 格式
 *   - 解析所有层 (LAYER) 和多边形 (POLYLINE)
 *   - 内外轮廓统一用奇偶规则填充（自动挖洞）
 *   - 每层输出打包数据 (.bin)：位深由 G_bpp 决定（1bit 或 2bit 每像素）
 *
 * 编译：gcc -O2 -o cli_fill cli_fill.c -lm
 * 用法：./cli_fill input.cli [--dpi 400] [--outdir layers] [--bpp 1|2]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <sys/stat.h>
#include "cli_fill2.h"
#include "binformat.h"      /* 1bit/2bit 打包公式的唯一来源，见 binformat.h */

/* ===================================================================
 *  全局配置
 * =================================================================== */
 /* 输出位图分辨率，单位 DPI（每英寸点数）。默认 400 DPI ≈ 15.75 像素/mm */
static double G_dpi   = 400.0;
/* CLI 文件中的坐标单位 → mm 的换算系数。例如 $$UNITS/0.001 表示坐标单位是 μm */
static double G_units = 1.0;   /* CLI 坐标单位 → mm */
/* 输出 .bin 的每像素位数：1 或 2。由 cli_fill2_convert 的入参 / main 的 --bpp 决定 */
static int    G_bpp   = 2;

/* 全局包围盒（运行时从所有层扫描得到） */
/* 全局包围盒：运行时从所有层的所有顶点扫描得到，初始化为极端值 */
static double G_xmin =  1e30, G_xmax = -1e30;
static double G_ymin =  1e30, G_ymax = -1e30;
/* 每毫米像素数 = DPI / 25.4（1 英寸 = 25.4 mm） */  // 运算结果是 每mm有多少个像素点
#define PX_PER_MM  (G_dpi / 25.4)
/* 画布宽高（像素）：物理尺寸 × 像素密度，+1 保证边界像素不被裁掉 */
#define G_WIDTH    ((int)ceil((G_xmax - G_xmin) * PX_PER_MM) + 1)
#define G_HEIGHT   ((int)ceil((G_ymax - G_ymin) * PX_PER_MM) + 1)
/* 每行字节数：位深相关，1bpp=(w+7)/8，2bpp=(w+3)/4（公式统一取自 binformat.h） */
#define ROW_BYTES  (bin_row_bytes(G_WIDTH, G_bpp))
#define MAX_POLYGON_POINTS 1000000U
#define MAX_IMAGE_PIXELS 200000000LL

/* ===================================================================
 *  打包位图（1bit 或 2bit 每像素）
 * =================================================================== */
typedef struct {
    int width, height;
    int row_bytes;
    int bpp;               /* 1 或 2：每像素位数 */
    unsigned char *data;   /* 打包像素, MSB 优先, row0=y_min(底部) */
} BitImage;

/* 创建位图：按全局画布尺寸分配并清零（calloc 保证初始全 0 = 全背景） */
static int img_create(BitImage *img)
{
    img->width     = G_WIDTH;
    img->height    = G_HEIGHT;
    img->bpp       = G_bpp;
    img->row_bytes = ROW_BYTES;
    img->data      = calloc((size_t)ROW_BYTES * G_HEIGHT, 1);
    return img->data ? 0 : -1;
}
/* 清空位图：全部置 0（背景色） */
static void img_clear(BitImage *img)
{
    memset(img->data, 0, (size_t)img->row_bytes * img->height);
}
/**
 * 设置单个像素的值（带边界保护）
 *
 * @param x,y  像素坐标（x 从左到右，y 从下到上）
 * @param val  像素值：2bpp 下 0~3（超出被 &0x03 截断）；1bpp 下非 0 即视为 1
 *
 * 位操作公式统一在 binformat.h 里（bin_pixel_set），这里只做寻行和边界检查。
 */

static inline void img_setpixel(BitImage *img, int x, int y, unsigned char val)
{
    if (x < 0 || x >= img->width || y < 0 || y >= img->height) return;
    bin_pixel_set(img->data + (size_t)y * img->row_bytes, x, val, img->bpp);
}

/*
 * 填充一段连续像素 [x0, x1]（同一行）。
 * 为了不每像素都算一次位偏移，把首尾不足"整字节"的部分逐点写，中间整字节直接 memset：
 *   1bpp：8 像素 = 1 字节，整字节填充值 = 全 1(0xFF) 或全 0(0x00)
 *   2bpp：4 像素 = 1 字节，整字节填充值 = val * 0x55（每 2bit 复制一遍）
 */
static void img_fill_span(BitImage *img, int x0, int x1, int y, unsigned char val)      // 当前填充固定传 `0x01`
{
    const int bpp     = img->bpp;
    const int px_per  = (bpp == BIN_BPP_1) ? 8 : 4;         // 一个字节装几个像素，1bpp 是 8，2bpp 是 4
    const int px_mask = px_per - 1;                         // 用来判 "x 是否落在字节边界上
    const unsigned char packed = (bpp == BIN_BPP_1)
        ? (unsigned char)(val ? 0xFF : 0x00)
        : (unsigned char)((val & 0x03) * 0x55);             // 2^6 + 2^4 + 2^2 + 2^0 = 0x55
    unsigned char *row = img->data + (size_t)y * img->row_bytes;        // 按 `row_bytes` 定位这一行的首字节

    while (x0 <= x1 && (x0 & px_mask))                      // 从 `x0` 开始逐像素写，直到 `x0` 落在字节边界上  字节对齐
    {
        img_setpixel(img, x0++, y, val);
    }
    if (x0 <= x1) {
        int full_bytes = (x1 - x0 + 1) / px_per;            // 还能整字节覆盖多少个字节
        if (full_bytes > 0) {
            memset(row + x0 / px_per, packed, (size_t)full_bytes);
            x0 += full_bytes * px_per;
        }                                                   // 因为 `x0` 已对齐，`x0/px_per` 是整除 剩下的不足一字节的尾巴（最多 7 个像素 / 3 个像素）逐点
    }
    while (x0 <= x1) img_setpixel(img, x0++, y, val);
}
/* 物理坐标(mm) → 像素列号：相对 xmin 的偏移 × 像素密度 */
static inline int phys_to_col(double x) { return (int)((x - G_xmin) * PX_PER_MM); }
/* 物理坐标(mm) → 像素行号：相对 ymin 的偏移 × 像素密度 */
static inline int phys_to_row(double y) { return (int)((y - G_ymin) * PX_PER_MM); }

/* ===================================================================
 *  扫描线填充（支持任意数量多边形，奇偶规则自动挖洞）
 * =================================================================== */
 /* 一条边的两个端点（物理坐标 mm） */
typedef struct { double x1, y1, x2, y2; } Edge;

static int compare_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}
/**
 * fill_edges — 扫描线多边形填充核心算法
 *
 * @param edges       所有边的数组（内外轮廓混在一起，不区分方向）
 * @param edge_count  边的总数
 * @param val         填充用的像素值（这里固定 0x01 = 喷印）
 *
 * 【算法步骤】
 *   对每一行扫描线（像素中心高度）：
 *     1. 遍历所有边，找出与扫描线相交的边（端点在扫描线两侧）
 *     2. 用线性插值算出交点的 x 坐标
 *     3. 对交点 x 从小到大排序
 *     4. 按奇偶规则成对填充：[x0,x1]、[x2,x3]、[x4,x5]...
 *        这意味着穿过奇数条边后进入实体，穿过偶数条边后离开实体，
 *        因此内孔（洞）会被自动跳过，无需单独判断内外轮廓。
 *
 * 【注意】
 *   - 交点数组 xs[4096] 是固定上限，极端复杂层可能溢出（cnt 被截断） 已修改
 *   - 用冒泡排序，交点数小时性能可接受；大规模建议换 qsort
 *   - 水平边（y1==y2）不会被选中（(y1>scan_y)!=(y2>scan_y) 为 false），
 *     这是正确的，因为水平边不影响扫描线交点
 */

static void fill_edges(BitImage *img, const Edge *edges, int edge_count, unsigned char val)
{
    double min_y = edges[0].y1, max_y = edges[0].y1;        // `min_y` / `max_y` 用 `edges[0].y1` 初始化，然后遍历每条边的两个端点取极值 求出本层的Y范围 因为往往图形只占画布的一小部分
    for (int e = 0; e < edge_count; ++e) {
        if (edges[e].y1 < min_y) min_y = edges[e].y1;
        if (edges[e].y2 < min_y) min_y = edges[e].y2;
        if (edges[e].y1 > max_y) max_y = edges[e].y1;
        if (edges[e].y2 > max_y) max_y = edges[e].y2;
    }
    int first_row = phys_to_row(min_y);                 // 算出最小值离y 下边界 有多少个像素点 得出像素行好
    int last_row = phys_to_row(max_y);                  // 算出最大值离y 上边界 有多少个像素点
    if (first_row < 0) first_row = 0;
    if (last_row >= img->height) last_row = img->height - 1;

    double *xs = malloc((size_t)edge_count * sizeof(double));       // 一次性分配交点数组 因为**一条边和一条扫描线最多只有一个交点**，所以 `cnt <= edge_count` 一定成立
    if (!xs) return;
    /*
     *一行像素实际占据的 y 区间是 `[G_ymin + row/PX, G_ymin + (row+1)/PX]`，
     *如果直接拿行的下边界当扫描线高度，那么 "顶点正好落在像素边界上" 时，
     *这个像素算不算被覆盖就有歧义 —— 不同实现会给出不同结果，
     *同一份 CLI 在不同软件里会差一像素。
     *取像素中心（`row + 0.5`）就把归属问题变成 "交点在不在这一行的中间"，语义上唯一
     */
    for (int row = first_row; row <= last_row; row++) { /* 扫描线取像素中心高度，避免边界像素的歧义 */
        double scan_y = G_ymin + (row + 0.5) / PX_PER_MM;  /* 像素中心 */   // (row + 0.5) / PX_PER_MM 这个运行结果 当前像素行号 + 0.5 / 每mm多少个点 =  具体 有多少个mm
        int cnt = 0;
		/* 第一步：求扫描线与所有边的交点 */
        for (int e = 0; e < edge_count; e++) {
            double y1 = edges[e].y1, y2 = edges[e].y2;
            if ((y1 > scan_y) != (y2 > scan_y)) {                           // 两个端点是否分居扫描线两侧
                double x1 = edges[e].x1, x2 = edges[e].x2;
                double xi = x1 + (scan_y - y1) * (x2 - x1) / (y2 - y1);     // 线性插值公式
                xs[cnt++] = xi;
            }
        }

        qsort(xs, (size_t)cnt, sizeof(double), compare_double);             // `compare_double` 是 `(da > db) - (da < db)` 的写法 —— 返回 -1/0/1

        /* 奇偶段填充：0-1, 2-3, 4-5 ... */
        for (int k = 0; k + 1 < cnt; k += 2) {                              // `k + 1 < cnt` 保证只处理成对的交点，`cnt` 为奇数时最后一个丢掉
            int c0 = phys_to_col(xs[k]);
            int c1 = phys_to_col(xs[k + 1]);
            if (c0 < 0) c0 = 0;
            if (c1 >= img->width)  c1 = img->width - 1;
            if (c0 > c1) continue;
            img_fill_span(img, c0, c1, row, val);
        }
    }
    free(xs);
}

/* ===================================================================
 *  CLI 数据结构
 * =================================================================== */
 /* 二维点（物理坐标 mm，已乘以 units 换算） */
typedef struct { double x, y; } PointD;
/**
 * Polygon — 一个多边形（轮廓）
 *
 * @param dir  方向标记（CLI 规范定义）：
 *               0 = 顺时针（通常表示内部/孔）
 *               1 = 逆时针（通常表示外部轮廓）
 *               2 = 开放折线（不闭合，不参与填充）
 *             注意：本程序的填充算法不依赖 dir 判断内外，统一用奇偶规则，
 *             所以 dir 只用于过滤开放线（dir==2）。
 * @param n    顶点数
 * @param pts  顶点数组
 */

typedef struct {
    int dir;       /* 0=顺时针(内部/孔), 1=逆时针(外部), 2=开放线 */
    int n;
    PointD *pts;
} Polygon;
/* 一层：包含 Z 高度和该层的所有多边形 */
typedef struct {
    double z;
    int n_poly;
    Polygon *polys;
} Layer;
/* 整个 CLI 文档 */
typedef struct {
    int is_binary;	/* 1=二进制格式, 0=ASCII 格式 */
    double units;	/* 坐标单位 → mm 的换算系数 */
    int n_layers;	/* 层数 */
    Layer *layers;	/* 层数组 */
} CLI_Doc;
/* 释放整个 CLI 文档的所有动态内存（逐层、逐多边形、逐顶点） */
static void cli_free(CLI_Doc *doc)
{
    for (int i = 0; i < doc->n_layers; i++) {
        for (int j = 0; j < doc->layers[i].n_poly; j++)
            free(doc->layers[i].polys[j].pts);
        free(doc->layers[i].polys);
    }
    free(doc->layers);
    doc->layers   = NULL;
    doc->n_layers = 0;
}

/* 动态添加一层 */ /* 动态添加一层（realloc 扩容层数组），返回新层指针 */
static Layer *doc_add_layer(CLI_Doc *doc, double z)
{
    Layer *layers = realloc(doc->layers, (size_t)(doc->n_layers + 1) * sizeof(Layer));
    if (!layers) return NULL;
    doc->layers = layers;
    Layer *L    = &doc->layers[doc->n_layers++];
    L->z        = z;
    L->n_poly   = 0;
    L->polys    = NULL;
    return L;
}

/* 动态添加一个多边形 并分配顶点数组*/
static Polygon *layer_add_poly(Layer *L, int dir, int n)
{
    if (!L || n < 2 || (unsigned int)n > MAX_POLYGON_POINTS) return NULL;
    L->polys = realloc(L->polys, (size_t)(L->n_poly + 1) * sizeof(Polygon));
    if (!L->polys) return NULL;
    Polygon *P = &L->polys[L->n_poly++];
    P->dir  = dir;
    P->n    = n;
    P->pts  = malloc((size_t)n * sizeof(PointD));
    if (!P->pts) {
        L->n_poly--;
        return NULL;
    }
    return P;
}

/* ===================================================================
 *  二进制读取（自动字节序：CLI 规范是大端，但部分软件输出小端）
 * =================================================================== */
 /* 运行时检测到的字节序：0=大端(规范), 1=小端(部分 x86 工具) */
static int g_bin_little_endian = 0;   /* 运行时检测 */
/* 读取 16 位无符号整数，根据检测到的字节序组装 */
static unsigned short rd_u16(FILE *f)
{
    int b1 = fgetc(f), b2 = fgetc(f);
    if (b1 < 0 || b2 < 0) return 0;
    return g_bin_little_endian ?
        (unsigned short)((b2 << 8) | b1) :
        (unsigned short)((b1 << 8) | b2);
}
/* 读取 32 位无符号整数 */
static unsigned int rd_u32(FILE *f)
{
    int b1 = fgetc(f), b2 = fgetc(f), b3 = fgetc(f), b4 = fgetc(f);
    if (b1 < 0 || b2 < 0 || b3 < 0 || b4 < 0) return 0;
    return g_bin_little_endian ?
        ((unsigned int)b4 << 24) | ((unsigned int)b3 << 16) |
        ((unsigned int)b2 << 8)  |  (unsigned int)b1 :
        ((unsigned int)b1 << 24) | ((unsigned int)b2 << 16) |
        ((unsigned int)b3 << 8)  |  (unsigned int)b4;
}
/* 读取 32 位浮点数（IEEE 754）：先读 4 字节整数，再 memcpy 到 float */
static float rd_f32(FILE *f)
{
    unsigned int u = rd_u32(f);
    float v;
    memcpy(&v, &u, 4);
    return v;
}

/* ===================================================================
 *  ASCII 辅助
 * =================================================================== */
 /**
 * strip_comments — 去除行内的 // 注释
 *
 * CLI 规范中注释用 // ... // 成对包裹（不是 C 风格的行注释）。
 * 本函数处理成对注释：找到第一个 //，再找下一个 //，把中间内容删掉。
 * 如果只有一个 //（行尾注释），则截断到 // 处。
 * 循环处理是因为一行可能有多对注释。
 */
static void strip_comments(char *line)
{
    for (;;) {
        char *s = strstr(line, "//");
        if (!s) break;
        char *e = strstr(s + 2, "//");
        if (e) memmove(s, e + 2, strlen(e + 2) + 1);
        else   { *s = '\0'; break; }
    }
}
/* 去除字符串首尾空白，返回处理后的字符串指针（原地修改） */
static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) e--;
    *e = '\0';
    return s;
}

/* 从字符串中按逗号/空白解析下一个数字 */
/**
 * next_num — 从字符串中解析下一个数字
 *
 * 跳过逗号和空白，然后用 strtod 解析一个浮点数。
 * 解析后把指针推进到数字之后，供下次调用。
 * 用于解析 $$POLYLINE/ 后面的一串数字。
 */

static double next_num(char **s)
{
    while (**s && (**s == ',' || isspace((unsigned char)**s))) (*s)++;
    char *end;
    double v = strtod(*s, &end);
    *s = end;
    return v;
}

/* ===================================================================
 *  解析 CLI 文件（自动检测 ASCII / 二进制）
 * =================================================================== */
 /* ===================================================================
 *  解析 CLI 文件（自动检测 ASCII / 二进制）
 * =================================================================== */

/**
 * cli_parse — 解析 CLI 文件，填充 CLI_Doc 结构
 *
 * 【CLI 文件格式概述】
 *   CLI 文件分为两部分：
 *     1. 头部（$$HEADERSTART ... $$HEADEREND）：声明格式（ASCII/BINARY）、
 *        单位、版本等元信息。头部始终是 ASCII 文本。
 *     2. 几何部分（$$GEOMETRYSTART ... $$GEOMETRYEND）：
 *        - ASCII 格式：文本行，$$LAYER/z、$$POLYLINE/id,dir,n,x1,y1,...
 *        - 二进制格式：紧跟 $$HEADEREND 之后（注意不是 $$GEOMETRYSTART），
 *          由一系列命令标识（CI）+ 数据组成，CI 是 127~132 的整数。
 *
 * 【二进制命令标识（CI）】
 *   127 = Start Layer long   (z 为 float32)
 *   128 = Start Layer short  (z 为 uint16)
 *   129 = PolyLine short     (id,dir,n 为 uint16，坐标为 uint16)
 *   130 = PolyLine long      (id,dir,n 为 uint32，坐标为 float32)
 *   131 = Hatches short
 *   132 = Hatches long
 *
 * @return 0=成功, -1=失败
 */
static int cli_parse(const char *path, CLI_Doc *doc)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "无法打开文件: %s\n", path); return -1; }

    memset(doc, 0, sizeof(*doc));
    doc->units = 1.0;

    char line[65536];				/* 行缓冲区，足够容纳超长的 POLYLINE 行 */
    int in_header = 0;              // 当前是否位于 `$$HEADERSTART ... $$HEADEREND` 之间
    int in_geometry = 0;            // 当前是否位于 `$$GEOMETRYSTART ... $$GEOMETRYEND` 之间
    Layer *cur_layer = NULL;        // 当前正在往哪一层里加多边形（`$$LAYER` 命令切层）
    long geom_offset = 0;           /* 二进制几何部分的文件偏移 */
    long pos_before;                // 当前行的文件起始偏移（下面要用）

    /* ---- 第一阶段：解析 ASCII 头部 （以及 ASCII 格式的几何部分）---- */
    while (1) {
        pos_before = ftell(f);					 /* 记录当前行起始偏移，用于计算 geom_offset */ // 表示当前位置相对于文件开头的偏移量（字节数）
        if (!fgets(line, sizeof(line), f)) break;       // `fgets` 会一直读到 `\n` 或 EOF，读完后文件指针已经跳到行尾了
        strip_comments(line);                           // 删掉 `// ... //` 成对注释（CLI 规范注释语法），单行尾注释也删
        char *p = trim(line);                           // 去首尾空白，拿到 "干净行" 指针 `p`
        if (!*p) continue;                              // 空行直接跳过
        /* 头部开始标记 */                                // 见到 `$$HEADERSTART`（13 个字符）→ 进入头部。`continue` 是因为这一行本身不含数据，处理完就下一行。
        if (strncmp(p, "$$HEADERSTART", 13) == 0) { in_header = 1; continue; }
		/* 头部结束标记：记录二进制数据的起始偏移 */
        if (strncmp(p, "$$HEADEREND", 11) == 0) {
            in_header = 0;          // 见到 `$$HEADEREND`（11 个字符）→ 退出头部段
            /* 精确计算 $$HEADEREND 之后的文件偏移（二进制数据可能直接紧跟，含 0x00/0x0A） */
            char *he = strstr(line, "$$HEADEREND");         // 在一个字符串中查找另一个子串第一次出现的位置   返回 第一次出现位置的指针
            geom_offset = pos_before + (long)(he - line) + 11;
            continue;
        }
		/* 几何部分开始标记（仅 ASCII 格式有这个标记；二进制格式几何数据直接跟在 HEADEREND 后） */
        if (strncmp(p, "$$GEOMETRYSTART", 15) == 0) { in_geometry = 1; continue; }
		/* 几何部分结束标记 */
        if (strncmp(p, "$$GEOMETRYEND", 13) == 0) { in_geometry = 0; break; }       // 见到 `$$GEOMETRYEND` → 退出几何段，**直接 break 整个 while 循环**，第一阶段结束

        if (!in_header && !in_geometry) continue;       // **两个段都不在**（既不在 HEADER 里，也不在 GEOMETRY 里）的行 比如头部结束后、几何段开始前的空行 / 杂行，一律忽略

        /* ---- 头部命令解析 ---- */
        if (in_header) {
            if (strncmp(p, "$$BINARY", 8) == 0) { doc->is_binary = 1; continue; }       // 这个文件是二进制格式
            if (strncmp(p, "$$ASCII", 7) == 0)  { doc->is_binary = 0; continue; }
            /* $$UNITS/0.001 — 坐标单位换算系数，例如 0.001 表示坐标值单位是 μm */  // 后边所以的多边形顶点，都要乘以这个系数
            if (strncmp(p, "$$UNITS/", 8) == 0) {
                char *q = p + 8;
                doc->units = next_num(&q);                                  // 先跳过逗号和空白，再用 `strtod` 读出一个 double，同时把 `q` 指针往前推到这个数字之后
                continue;                                                   // 头部剩下的东西一律跳过
            }
            /* VERSION / DATE / DIMENSION / LAYERS / LABEL 等暂不解析，跳过 */
            continue;
        }

        /* ---- ASCII 几何部分解析 ---- */        //  这个 CLI 是 ASCII 文本格式 && 这个 CLI 是 ASCII 文本格式
        if (!doc->is_binary && in_geometry) {
            /* $$LAYER/z — 开始新的一层，z 为高度（乘以 units 换算为 mm） */ //
            if (strncmp(p, "$$LAYER/", 8) == 0) {
                char *q = p + 8;
                double z = next_num(&q) * doc->units;
                cur_layer = doc_add_layer(doc, z);      // 内部 `realloc` 把层数组扩一层，返回新层指针 把这个新层指针赋给 `cur_layer`—— 后面读到的所有 `$$POLYLINE` 都挂在 `cur_layer` 上
                if (!cur_layer) {                       // 如果 `doc_add_layer` 返回 NULL（内存不够），走统一的失败三件套
                    fclose(f);
                    cli_free(doc);
                    return -1;
                }
                continue;
            }
			/* $$POLYLINE/id,dir,n,x1,y1,x2,y2,... — 一个多边形/折线 */
            if (strncmp(p, "$$POLYLINE/", 11) == 0) {
                if (!cur_layer) {
                    fprintf(stderr, "警告: POLYLINE 出现在 LAYER 之前，已忽略\n");
                    continue;
                }
                char *q = p + 11;
                int id  = (int)next_num(&q);	/* 多边形 ID（本程序不使用） */
                int dir = (int)next_num(&q);	/* 方向：0=顺时针, 1=逆时针, 2=开放线 */
                int n   = (int)next_num(&q);	/* 顶点数 */
                if (n < 2) continue;			/* 至少 2 个点才有意义 */
                Polygon *P = layer_add_poly(cur_layer, dir, n);     // 在当前层里 `realloc` 一个多边形出来，再 `malloc` 一个 `n` 个点的 `PointD` 数组
                if (!P) {
                    fclose(f);
                    cli_free(doc);
                    return -1;
                }
                (void)id;
				/* 读取 n 个顶点的 x,y 坐标，均乘以 units 换算为 mm */
                for (int i = 0; i < n; i++) {
                    P->pts[i].x = next_num(&q) * doc->units;
                    P->pts[i].y = next_num(&q) * doc->units;
                }
                continue;
            }
            /* HATCHES 暂不处理，跳过 */
            continue;
        }
    }

    /* ---- 第二阶段：如果是二进制格式，解析二进制几何部分 ---- */
    if (doc->is_binary) {
        fseek(f, geom_offset, SEEK_SET);
        /* 跳过可能的换行/空白（规范说不应该有，但容错） */
        int c;
        while ((c = fgetc(f)) != EOF) {
            if (c >= 127) { ungetc(c, f); break; }	/* 遇到合法 CI，回退并开始解析 */
            if (!isspace(c)) { ungetc(c, f); break; }	/* 遇到非空白非 CI，回退（可能是数据） */   // `ungetc(c, f)`：把刚读的这个字节**塞回文件缓冲区**
        }

        /* 自动检测字节序：CLI 规范是大端，但部分软件(如x86工具)输出小端 
		*  检测方法：读前 2 字节，分别按大端和小端解释，看哪个落在合法 CI 范围 127~132*/
        {
            int b1 = fgetc(f), b2 = fgetc(f);
            if (b1 >= 0 && b2 >= 0) {
                unsigned short ci_be = (unsigned short)((b1 << 8) | b2);
                unsigned short ci_le = (unsigned short)((b2 << 8) | b1);
                if (ci_be >= 127 && ci_be <= 132) {
                    g_bin_little_endian = 0;  /* 大端 */
                } else if (ci_le >= 127 && ci_le <= 132) {
                    g_bin_little_endian = 1;  /* 小端 */
                } else {
                    g_bin_little_endian = 0;  /* 默认大端 */
                }
                fseek(f, -2, SEEK_CUR);  /* 回退2字节，让循环重新读取CI */
            }
        }
        printf("二进制字节序: %s\n", g_bin_little_endian ? "小端(自动检测)" : "大端(规范)");
		/* 二进制命令循环：读 CI → 根据 CI 类型读取对应数据 */
        for (;;) {
            unsigned short ci = rd_u16(f);
            if (feof(f) || ci == 0) break;	/* CI=0 或 EOF 表示结束 */

            if (ci == 127) {          /* Start Layer long */	 /* Start Layer long：z 为 float32 */
                double z = rd_f32(f) * doc->units;
                cur_layer = doc_add_layer(doc, z);
                if (!cur_layer) { fclose(f); cli_free(doc); return -1; }
            } else if (ci == 128) {   /* Start Layer short */
                double z = rd_u16(f) * doc->units;				 /* Start Layer short：z 为 uint16 */
                cur_layer = doc_add_layer(doc, z);
            } else if (ci == 129) {   /* PolyLine short */       /* PolyLine short：id,dir,n 为 uint16，坐标为 uint16 */
                if (!cur_layer) continue;
                unsigned short id  = rd_u16(f);
                unsigned short dir = rd_u16(f);
                unsigned short n   = rd_u16(f);
                (void)id;
                if (n < 2) continue;
                Polygon *P = layer_add_poly(cur_layer, dir, n);
                if (!P) {
                    fclose(f);
                    cli_free(doc);
                    return -1;
                }
                for (int i = 0; i < n; i++) {
                    P->pts[i].x = rd_u16(f) * doc->units;
                    P->pts[i].y = rd_u16(f) * doc->units;
                }
            } else if (ci == 130) {   /* PolyLine long */    	 /* PolyLine long：id,dir,n 为 uint32，坐标为 float32 */
                if (!cur_layer) continue;
                unsigned int id  = rd_u32(f);
                unsigned int dir = rd_u32(f);
                unsigned int n   = rd_u32(f);
                (void)id;
                if (n < 2) continue;
                Polygon *P = layer_add_poly(cur_layer, (int)dir, (int)n);
                if (!P) {
                    fclose(f);
                    cli_free(doc);
                    return -1;
                }
                for (unsigned int i = 0; i < n; i++) {
                    P->pts[i].x = rd_f32(f) * doc->units;
                    P->pts[i].y = rd_f32(f) * doc->units;
                }
            } else if (ci == 131 || ci == 132) {
                /* Hatches short / long：跳过，按格式计算偏移 */
                /* 简化处理：读取 id 和 n，然后跳过 n*4 个坐标 */
                if (ci == 131) {
                    rd_u16(f); unsigned short n = rd_u16(f);
                    for (int i = 0; i < n * 4; i++) rd_u16(f);
                } else {
                    rd_u32(f); unsigned int n = rd_u32(f);
                    for (unsigned int i = 0; i < n * 4; i++) rd_u32(f);
                }
            } else {
                /* 未知命令，停止解析 */
                break;
            }
        }
    }

    fclose(f);
    G_units = doc->units;
    return 0;
}

/* ===================================================================
 *  计算全局包围盒
 * =================================================================== */

/* 扫描所有层的所有闭合多边形顶点，得到全局 XY 范围
 * 开放线（dir==2）不参与，因为它不构成填充区域 */
static void compute_bbox(const CLI_Doc *doc)
{
    for (int i = 0; i < doc->n_layers; i++) {
        for (int j = 0; j < doc->layers[i].n_poly; j++) {
            Polygon *P = &doc->layers[i].polys[j];
            if (P->dir == 2) continue;   /* 开放线不参与填充和包围盒 */
            for (int k = 0; k < P->n; k++) {
                if (P->pts[k].x < G_xmin) G_xmin = P->pts[k].x;
                if (P->pts[k].x > G_xmax) G_xmax = P->pts[k].x;
                if (P->pts[k].y < G_ymin) G_ymin = P->pts[k].y;
                if (P->pts[k].y > G_ymax) G_ymax = P->pts[k].y;
            }
        }
    }
    /* 留 1 像素余量 */ /* 四周留 1 像素余量，避免边界上的顶点被裁掉 */
    double pad = 1.0 / PX_PER_MM;
    G_xmin -= pad; G_xmax += pad;
    G_ymin -= pad; G_ymax += pad;
}

/* ===================================================================
 *  导出原始打包数据（1bit 或 2bit 每像素，无文件头）
 * =================================================================== */
 /* 直接把位图的原始数据写入文件，格式由 img->bpp 决定（见 binformat.h） */
static int save_raw_bin(const char *path, const BitImage *img)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t data_size = (size_t)img->row_bytes * (size_t)img->height;
    size_t written = fwrite(img->data, 1, data_size, f);
    int close_result = fclose(f);
    if (written != data_size || close_result != 0) {
        fprintf(stderr, "写入切片失败: %s (需要 %zu 字节，实际 %zu 字节)\n",
                path, data_size, written);
        remove(path);
        return -1;
    }
    return 0;
}

/* ===================================================================
 *  导出 BMP 预览（8位灰度）
 * =================================================================== */
 /* BMP 文件用小端序写入，以下两个辅助函数按小端写入 16/32 位整数 */
static void wr_le16(FILE *f, unsigned short v) { fputc(v&0xff,f); fputc((v>>8)&0xff,f); }
static void wr_le32(FILE *f, unsigned int v)
{
    fputc(v&0xff,f); fputc((v>>8)&0xff,f);
    fputc((v>>16)&0xff,f); fputc((v>>24)&0xff,f);
}
/**
 * save_bmp_preview — 把打包位图（1bit/2bit）导出为 8 位灰度 BMP 预览图
 *
 * BMP 结构：
 *   - 文件头（14 字节）：BM 标识、文件大小、数据偏移
 *   - 信息头（40 字节，BITMAPINFOHEADER）：宽高、位深、压缩等
 *   - 调色板（256 × 4 字节 = 1024 字节）：8 位灰度需要 256 色调色板
 *     本程序只用到前 4 个索引（0~3），其余填 0
 *   - 像素数据：每行 4 字节对齐，自底向上存储
 *
 * 调色板映射：
 *   索引 0 = 白色（255,255,255）→ 背景/不喷
 *   索引 1 = 黑色（0,0,0）      → 喷印
 *   索引 2 = 深灰（64,64,64）   → 预留（仅 2bpp 会出现）
 *   索引 3 = 浅灰（128,128,128）→ 预留（仅 2bpp 会出现）
 */

static int save_bmp_preview(const char *path, const BitImage *img)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int w = img->width, h = img->height;
    int row_size = (w + 3) & ~3;
    int img_size = row_size * h;
    int data_off = 14 + 40 + 1024;

    fputc('B',f); fputc('M',f);
    wr_le32(f, (unsigned int)(data_off + img_size));
    wr_le16(f, 0); wr_le16(f, 0);
    wr_le32(f, data_off);
    wr_le32(f, 40);
    wr_le32(f, (unsigned int)w); wr_le32(f, (unsigned int)h);
    wr_le16(f, 1); wr_le16(f, 8);
    wr_le32(f, 0); wr_le32(f, (unsigned int)img_size);
    wr_le32(f, 2835); wr_le32(f, 2835);
    wr_le32(f, 256); wr_le32(f, 0);

    /* 调色板：0=白(不喷/背景), 1=黑(喷印), 2=深灰, 3=浅灰 */
    unsigned char pal[4][4] = {
        {255,255,255,0}, {0,0,0,0}, {64,64,64,0}, {128,128,128,0}
    };
    fwrite(pal, 4, 4, f);
    unsigned char zero[4] = {0};
    for (int i = 4; i < 256; i++) fwrite(zero, 1, 4, f);

    /* 像素数据：BMP 自底向上，打包像素 → 调色板索引（0/1 或 0~3） */
    unsigned char *line = calloc((size_t)row_size, 1);
    for (int row = 0; row < h; row++) {
        const unsigned char *src = img->data + (size_t)row * img->row_bytes;
        for (int x = 0; x < w; x++) {
            line[x] = bin_pixel_get(src, x, img->bpp);   /* 直接作为调色板索引 */
        }
        fwrite(line, 1, (size_t)row_size, f);
    }
    free(line);
    fclose(f);
    return 0;
}

/* ===================================================================
 *  统计喷印像素（1bpp 看 bit，2bpp 看 v!=0） 空层直接跳过
 * =================================================================== */
static int count_spray(const BitImage *img)
{
    int spray = 0;
    int total = img->row_bytes * img->height;
    for (int i = 0; i < total; i++) {
        unsigned char b = img->data[i];
        if (img->bpp == BIN_BPP_1) {
            /* 1bpp：直接数 popcount */
            b = (unsigned char)(b - ((b >> 1) & 0x55));
            b = (unsigned char)((b & 0x33) + ((b >> 2) & 0x33));
            spray += (b + (b >> 4)) & 0x0F;
        } else {
            unsigned char p0 = (b>>6)&3, p1=(b>>4)&3, p2=(b>>2)&3, p3=b&3;
            spray += (p0!=0) + (p1!=0) + (p2!=0) + (p3!=0);
        }
    }
    return spray;
}

/* ===================================================================
 *  处理一层：收集边 → 填充 → 保存
 * =================================================================== */
static int process_layer(const Layer *L, int layer_idx, const char *outdir)
{
    /* 收集所有闭合多边形的边 */
    int max_edges = 0;
    for (int j = 0; j < L->n_poly; j++) {
        if (L->polys[j].dir == 2) continue;
        max_edges += L->polys[j].n;
    }
    if (max_edges == 0) {
        printf("  层 %d (z=%.3f): 无闭合多边形，跳过\n", layer_idx, L->z);
        return 0;
    }

    Edge *edges = malloc((size_t)max_edges * sizeof(Edge));
    int ec = 0;
    for (int j = 0; j < L->n_poly; j++) {
        Polygon *P = &L->polys[j];
        if (P->dir == 2) continue;
        for (int k = 0; k < P->n; k++) {
            int k2 = (k + 1) % P->n;
            edges[ec].x1 = P->pts[k].x;  edges[ec].y1 = P->pts[k].y;
            edges[ec].x2 = P->pts[k2].x; edges[ec].y2 = P->pts[k2].y;
            ec++;
        }
    }

    BitImage img;
    if (img_create(&img) != 0) { free(edges); return -1; }
    fill_edges(&img, edges, ec, 0x01);
    free(edges);

    int spray = count_spray(&img);
    int total = G_WIDTH * G_HEIGHT;
    if (spray == 0) {
        printf("  层 %03d (z=%8.3f mm): 无有效喷印像素，跳过输出\n", layer_idx, L->z);
        free(img.data);
        return 0;
    }
    printf("  层 %03d (z=%8.3f mm): 喷印 %7d / %7d px (%.2f%%), %dbit/pixel\n",
           layer_idx, L->z, spray, total, 100.0 * spray / total, img.bpp);

    char bin_path[512];
    snprintf(bin_path, sizeof(bin_path), "%s/layer_%03d.bin", outdir, layer_idx);
    int result = save_raw_bin(bin_path, &img);
    free(img.data);
    return result == 0 ? 1 : -1;
}

int cli_fill2_convert(const char *input_path, const char *outdir, double dpi,
                      int bits_per_pixel, CliFillResult *result)
{
    CLI_Doc doc;
    int i;

    if (!input_path || !outdir || dpi <= 0.0) return -1;    // 空指针或 DPI 非正，直接 -1

    G_dpi = dpi;                        // 把所有文件级全局变量重置一遍
    G_bpp = bin_bpp_norm(bits_per_pixel);   // 1 或 2，其它值按 2 处理
    G_units = 1.0;                      // 单位换算系数（`cli_parse` 里会被 `$$UNITS/` 覆盖）
    G_xmin = 1e30; G_xmax = -1e30;      // 包围盒；初始化成 "极端值"（min=+∞、max=-∞），`compute_bbox` 才能第一个顶点就把它们刷新
    G_ymin = 1e30; G_ymax = -1e30;
    g_bin_little_endian = 0;            // 二进制大小端检测结果，默认按规范大端

    /* status,含义（对应 cli_fill2.c 里的 return）
     * 0,成功，result 已填好
     * -1,参数非法或 cli_parse 失败（打不开文件 / 不是合法 CLI）
     * -2,解析出 0 层
     * -3,包围盒退化（xmin>=xmax）
     * -4,process_layer 写 .bin 失败
     * -5,位图太大（>2 亿像素）
     * -6,所有层都是空的，没产出任何 .bin
     */
    if (cli_parse(input_path, &doc) != 0) return -1;        // 解析整个 CLI 文件：ASCII 头部 + ASCII / 二进制几何段
    if (doc.n_layers == 0) {
        cli_free(&doc);
        return -2;
    }

    compute_bbox(&doc);                 // 遍历所有层所有闭合多边形的顶点，刷新 `G_xmin/G_xmax/G_ymin/G_ymax`，最后四周加 1 像素余量
    if (!isfinite(G_xmin) || !isfinite(G_xmax) || G_xmin >= G_xmax ||
        !isfinite(G_ymin) || !isfinite(G_ymax) || G_ymin >= G_ymax) {
        cli_free(&doc);                 // `!isfinite`：浮点异常（NaN/Inf）兜底
        return -3;
    }

    if (G_WIDTH <= 0 || G_HEIGHT <= 0 ||
        (long long)G_WIDTH * (long long)G_HEIGHT > MAX_IMAGE_PIXELS) {          // 防止一个超大模型直接把内存吃爆
        cli_free(&doc);
        return -5;
    }

    int output_layer = 0;
    mkdir(outdir, 0755);
    for (i = 0; i < doc.n_layers; ++i) {
        int layer_status = process_layer(&doc.layers[i], output_layer + 1, outdir);     // **产出编号是压缩的**：第 1、2 层都为空，第 3 层成功，它会写成 `layer_001.bin`
        if (layer_status < 0) {
            cli_free(&doc);
            return -4;
        }
        if (layer_status > 0) ++output_layer;   // `1` 才 `output_layer++`
    }

    if (output_layer == 0) {                    // 所有层都处理完了但一层都没产出（全是空的），返回 -6
        cli_free(&doc);
        return -6;
    }

    if (result) {                               // 把产出层数、画布宽高、行字节数、位深、包围盒 mm 值全部塞进 `CliFillResult`
        result->layer_count = output_layer;
        result->width = G_WIDTH;
        result->height = G_HEIGHT;
        result->row_bytes = ROW_BYTES;
        result->bits_per_pixel = G_bpp;
        result->x_min = G_xmin;
        result->x_max = G_xmax;
        result->y_min = G_ymin;
        result->y_max = G_ymax;
    }
    cli_free(&doc);
    return 0;
}

/* ===================================================================
 *  主函数
 * =================================================================== */
#ifndef CLI_FILL2_NO_MAIN
int main(int argc, char *argv[])
{
    const char *infile = NULL;
    const char *outdir = "layers";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dpi") == 0 && i + 1 < argc) {
            G_dpi = atof(argv[++i]);
        } else if (strcmp(argv[i], "--outdir") == 0 && i + 1 < argc) {
            outdir = argv[++i];
        } else if (strcmp(argv[i], "--bpp") == 0 && i + 1 < argc) {
            G_bpp = bin_bpp_norm(atoi(argv[++i]));   // 只接受 1/2，其它按 2
        } else if (!infile) {
            infile = argv[i];
        }
    }

    if (!infile) {
        fprintf(stderr, "用法: %s <input.cli> [--dpi 400] [--outdir layers] [--bpp 1|2]\n", argv[0]);
        return 1;
    }

    printf("=== CLI 多层填充工具 ===\n");
    printf("输入文件: %s\n", infile);
    printf("分辨率:   %.0f DPI\n", G_dpi);
    printf("位深:     %d bit/pixel\n", G_bpp);

    /* 解析 */
    CLI_Doc doc;
    if (cli_parse(infile, &doc) != 0) return 1;
    printf("格式:     %s\n", doc.is_binary ? "二进制" : "ASCII");
    printf("单位:     %.4f mm/unit\n", doc.units);
    printf("层数:     %d\n", doc.n_layers);

    if (doc.n_layers == 0) {
        fprintf(stderr, "错误: 未找到任何层\n");
        cli_free(&doc);
        return 1;
    }

    /* 计算包围盒 */
    compute_bbox(&doc);
    printf("包围盒:   x=[%.3f, %.3f]  y=[%.3f, %.3f] mm\n", G_xmin, G_xmax, G_ymin, G_ymax);
    printf("画布尺寸: %d x %d px, 每行 %d 字节, 每层 %d 字节\n",
           G_WIDTH, G_HEIGHT, ROW_BYTES, ROW_BYTES * G_HEIGHT);

    /* 创建输出目录 */
    mkdir(outdir, 0755);

    /* 处理每一层 */
    printf("\n--- 开始处理 ---\n");
    for (int i = 0; i < doc.n_layers; i++) {
        process_layer(&doc.layers[i], i + 1, outdir);
    }

    printf("\n完成！输出目录: %s/\n", outdir);
    cli_free(&doc);
    return 0;
}
#endif
