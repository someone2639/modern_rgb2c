#pragma once

/* poached from n64graphics */
#include <stdint.h>

// SCALE_M_N: upscale/downscale M-bit integer to N-bit
#define SCALE_5_8(VAL_) (((VAL_) * 0xFF) / 0x1F)
#define SCALE_8_5(VAL_) ((((VAL_) + 4) * 0x1F) / 0xFF)
#define SCALE_4_8(VAL_) ((VAL_) * 0x11)
#define SCALE_8_4(VAL_) ((VAL_) / 0x11)
#define SCALE_3_8(VAL_) ((VAL_) * 0x24)
#define SCALE_8_3(VAL_) ((VAL_) / 0x24)
#define CLAMP_1(val_) ((val_) ? 1 : 0)

#define	GPACK_RGBA5551(r, g, b, a)	((((r)<<8) & 0xf800) | 		\
					 (((g)<<3) & 0x7c0) |		\
					 (((b)>>2) & 0x3e) | ((a) & 0x1))

// intermediate formats
typedef struct _rgba
{
   uint8_t red;
   uint8_t green;
   uint8_t blue;
   uint8_t alpha;
} rgba;

typedef struct _ia
{
   uint8_t intensity;
   uint8_t alpha;
} ia;

// CI palette
typedef struct
{
   uint16_t data[256];
   int max; // max number of entries
   int used; // number of entries used
} palette_t;

/* poached from ultratypes.h */

typedef unsigned char       u8;     /* unsigned  8-bit */
typedef unsigned short      u16;    /* unsigned 16-bit */
typedef unsigned long       u32;    /* unsigned 32-bit */
typedef unsigned long long  u64;    /* unsigned 64-bit */

typedef signed char         s8;     /* signed  8-bit */
typedef short               s16;    /* signed 16-bit */
typedef long                s32;    /* signed 32-bit */
typedef long long           s64;    /* signed 64-bit */

typedef volatile unsigned char      vu8;    /* unsigned  8-bit */
typedef volatile unsigned short     vu16;   /* unsigned 16-bit */
typedef volatile unsigned long      vu32;   /* unsigned 32-bit */
typedef volatile unsigned long long vu64;   /* unsigned 64-bit */

typedef volatile signed char        vs8;    /* signed  8-bit */
typedef volatile short              vs16;   /* signed 16-bit */
typedef volatile long               vs32;   /* signed 32-bit */
typedef volatile long long          vs64;   /* signed 64-bit */

typedef float   f32;    /* single prec floating point */
typedef double  f64;    /* double prec floating point */

/* begin original readtex.h */

/* For tex[].fmt: */
#define	RGBA		0
#define	YUV		1
#define	CI 		2
#define	IA		3
#define	I		4
#define	A		5
#define	MASK		6

enum OutputFormats {
	C = 0,
	RAW = 1,
	MIPMAP = 2,
	ASCII = 3, // This is the old RAW format
};

#define COLOR           0
#define INTENSITY       1

typedef struct texture {
	char	name[70];		/* Name of the array		*/
	int	index;			/* Texture index		*/
	int	width;			/* X dimention			*/
	int	height;			/* Y dimention			*/
	int	fmt;			/* RGBA, IA, I, etc.		*/
	int	siz;			/* Number of bits		*/
	int output;         /* C, RAW, MIPMAP       */
	u32 flags;
	u32 shuffle_mask;

	int realwidth;  // for kirby64 BG
	int realheight;  // for kirby64 BG
} Texture;

#define FLIP_FLAG 		(0x001)
#define PAD_FLAG 		(0x002)
#define XTRA_FLAG 		(0x004)
#define QUAD_FLAG 		(0x008)
#define SHUF_FLAG 		(0x010)
#define MM_SUPPLIED_FLAG 	(0x020)
#define SKIP_RAW_FLAG 		(0x040)
#define MM_HI_LEVEL 		(0x080)
#define HALF_SHIFT 		(0x100)
#define MAKE_BG_FLAG 		(0x200)

struct HashEntry {
    unsigned char red, green, blue, alpha, ColorNumber;
};

#ifdef __cplusplus
extern "C" {
#endif

int tex_convert (char *fn, struct texture *tex, int fmt, int siz, int makestatic,
	int lr, int lg, int lb, int hr, int hg, int hb, int output, int flags,
	int shuffle_mask,
	char *palname
	);

u8 avg_rgb(rgba *texel);

char *fmtstr (int fmt);
char *cmbstr (int fmt);
char *sizstr (int siz);

void export_rgba(rgba *img, Texture *tex);
void export_ia(rgba *img, Texture *tex);
void export_i(rgba *img, Texture *tex);
void export_ci(Texture *tex, char *path);

void write_word(Texture *t, u32 w);
void write_hword(Texture *t, u8 *h);
void write_byte(Texture *t, u8 b);

#ifdef __cplusplus
}
#endif
