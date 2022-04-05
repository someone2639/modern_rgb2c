/*
 * readtex.c -- converts .rgb files to .c files for Reality
 *
 * Lawrence Kesteloot
 * June 21st, 1994
 *
 * $Id: readtex.c,v 1.51 2000/06/22 08:29:18 hyoshida Exp $
 */

#include "ultra64.h"
#include <fcntl.h>
#include <malloc.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>

/* Next three are for iopen(), etc. */
#include <PRimage.h>
#include <gl/device.h>
#include <gl/gl.h>

#include "readtex.h"

#define MAX_WIDTH (2048)

#define TEMPFILENAME "foofile"

/* Maybe size of 1 should be I/8: */
int bestfmt[] = { IA, IA, RGBA, RGBA };
int bestsiz[] = { 16, 16, 16, 16 };
char TempString[80];
int QuantLevels;

struct HashEntry {
    unsigned char red, green, blue, alpha, ColorNumber;
};

struct {
    int NumberOfEntries;
    struct HashEntry RGB[256];
} ColorHashTable[256];



struct {
    int mm_supplied;
    int levels; /* 0= top level only; 1=2 lvls;  2=3lvls;  etc */
    char base[100];
    char ext[100];
    unsigned char *image[7];
    struct texture tex[7];
} MipMapFile;

/*
 * These three prototypes are here because gl/image.h doesn't declare them
 * very well (or at all).
 */

/*
int getrow(IMAGE *image, unsigned short *buffer,
                unsigned int y, unsigned int z);
int iclose(IMAGE *image);
IMAGE *iopen(char *, char *);
*/

/*
 * Prototypes of functions in this file
 */
void RGBtoUYVY(unsigned short *rp, unsigned short *gp, unsigned short *bp, unsigned char *ap,
               int xsize);

/*
 * Helper functions
 */

char *fmtstr(int fmt) {
    switch (fmt) {
        case RGBA:
            return "G_IM_FMT_RGBA";
        case YUV:
            return "G_IM_FMT_YUV";
        case CI:
            return "G_IM_FMT_CI";
        case IA:
            return "G_IM_FMT_IA";
        case I:
            return "G_IM_FMT_I";
        case A:
            return "G_IM_FMT_A";
        case MASK:
            return "G_IM_FMT_MASK";
    }

    return "G_IM_FMT_UNKNOWN";
}

char *cmbstr(int fmt) {
    switch (fmt) {
        case RGBA:
            return "RGBA";
        case YUV:
            return "YUV";
        case CI:
            return "CI";
        case IA:
            return "IA";
        case I:
            return "I";
        case A:
            return "A";
        case MASK:
            return "MASK";
    }

    return "UNKNOWN";
}

char *sizstr(int siz) {
    switch (siz) {
        case 4:
            return "G_IM_SIZ_4b";
        case 8:
            return "G_IM_SIZ_8b";
        case 16:
            return "G_IM_SIZ_16b";
        case 32:
            return "G_IM_SIZ_32b";
        default:
            return "G_IM_SIZ_OTHER";
    }

    return "G_IM_SIZ_UNKNOWN";
}

/*
 * printCheader()
 *
 * When output mode is C format, this header gets put out first
 *
 */

printCheader(int siz, IMAGE *im, int fmt, struct texture *tex) {
    printf(" *   Size: %d x %d\n", im->xsize, im->ysize);
    printf(" *   Number of channels: %d\n", im->zsize);
    printf(" *   Number of bits per texel: %d (%s)\n", siz, sizstr(siz));
    printf(" *   Format of texel: %s\n", fmtstr(fmt));
    printf(" *\n");
    printf(" * Example usage:\n");
    printf(" *\n");
    printf(" *   gsDPPipeSync (),\n");
    printf(" *   gsDPSetCombineMode (G_CC_DECALRGB, G_CC_DECALRGB),\n");
    printf(" *   gsDPSetTexturePersp (G_TP_PERSP),\n");
    printf(" *   gsDPSetTextureLOD (G_TL_TILE),\n");
    printf(" *   gsDPSetTextureFilter (G_TF_BILERP),\n");
    printf(" *   gsDPSetTextureConvert(G_TC_FILT),\n");
    if (fmt == CI) {
        printf(" *   gsDPLoadTLUT_pal256 (%stlut),\n", tex->name);
        printf(" *   gsDPSetTextureLUT (G_TT_RGBA16),\n");
    } else {
        printf(" *   gsDPSetTextureLUT (G_TT_NONE),\n");
    }
    if (siz == 4) {
        printf(" *   gsDPLoadTextureBlock_4b (%s, %s, %d, %d, 0,\n", tex->name, fmtstr(fmt), im->xsize,
               im->ysize);
    } else {
        printf(" *   gsDPLoadTextureBlock (%s, %s, %s, %d, %d, 0,\n", tex->name, fmtstr(fmt),
               sizstr(siz), im->xsize, im->ysize);
    }
    printf(" *     G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR,\n");
    printf(" *     G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),\n");
    printf(" */\n\n");
}

static unsigned char *saved_pic = NULL;
static unsigned short int *savedp16 = NULL;
static unsigned char *savedp8 = NULL;

/*
 * printMipMapheader()
 *
 * When output mode is C MipMap format, this header gets put out first
 *
 */

printMipMapheader(int siz, IMAGE *im, int fmt, struct texture *tex) {
    saved_pic = (unsigned char *) malloc((im->xsize * im->ysize * siz) / 8);
    savedp16 = (unsigned short int *) saved_pic;
    savedp8 = (unsigned char *) saved_pic;

    printf("/*\n");
    printf(" *   Size: %d x %d\n", im->xsize, im->ysize);
    printf(" *   Number of channels: %d\n", im->zsize);
    printf(" *   Number of bits per texel: %d (%s)\n", siz, sizstr(siz));
    printf(" *   Format of texel: %s\n", fmtstr(fmt));
    printf(" *\n");
    printf(" * Example usage:\n");
    printf(" *\n");
    printf(" *   gsSPTexture (128, 128, (levels-1), G_TX_RENDERTILE, 1),\n");
    printf(" *   gsDPPipeSync (),\n");
    printf(" *   gsDPSetCombineMode (G_CC_MODULATE%s, G_CC_MODULATE%s),\n", cmbstr(fmt), cmbstr(fmt));
    printf(" *   gsDPSetTexturePersp (G_TP_PERSP),\n");
    printf(" *   gsDPSetTextureDetail (G_TD_CLAMP),\n");
    printf(" *   gsDPSetTextureLOD (G_TL_TILE),\n");
    printf(" *   gsDPSetTextureLUT (G_TT_NONE),\n");
    printf(" *   gsDPSetTextureFilter (G_TF_BILERP),\n");
    printf(" *   gsDPSetTextureConvert(G_TC_FILT),\n");
    printf(" *   gsDPLoadTextureBlock (%s, %s, %s, %d, %d, 0\n", tex->name, fmtstr(fmt), sizstr(siz),
           im->xsize, im->ysize);
    printf(" *     G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR,\n");
    printf(" *     G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),\n");
    printf(" */\n\n");
}

printRAWheader(int siz, IMAGE *im, int fmt, struct texture *tex) {
    char OutputType[80];

    switch (fmt) {
        case I:
            sprintf(OutputType, "I");
            break;
        case A:
            sprintf(OutputType, "A");
            break;
        case IA:
            sprintf(OutputType, "IA");
            break;
        case RGBA:
            sprintf(OutputType, "RGBA");
            break;
        case CI:
            sprintf(OutputType, "CI");
            break;
        default:
            sprintf(OutputType, "Error in format \n");
            break;
    }

    printf("# Format %s BitSize %d Rows %d Columns %d \n", OutputType, siz, im->ysize, im->xsize);
}

/*
 * printpreview()
 *
 * When output mode is C format, this texture preview gets printed
 *
 */

printpreview(IMAGE *im, ushort buf[4][MAX_WIDTH]) {
    int x, y, i, data;

    /*
     * Display the texture preview.
     */

    printf("#if 0\t/* Image preview */\n\t+");
    for (x = 0; x < im->xsize; x++) {
        putchar('-');
    }
    printf("+\n");
    for (y = im->ysize - 1; y >= 0; y--) {
        for (i = 0; i < im->zsize; i++) {
            getrow(im, buf[i], y, i); /* R */
        }
        printf("\t|"); /* To not have # in 1st column */
        for (x = 0; x < im->xsize; x++) {
            if (im->zsize > 2) {
                buf[0][x] = (int) (0.299 * buf[0][x] + 0.587 * buf[1][x] + 0.114 * buf[2][x] + 0.5);
            }
            if (im->zsize == 4) {
                buf[0][x] = buf[0][x] * buf[3][x] / 255;
            }
            if (buf[0][x] < 0) {
                buf[0][x] = 0;
            }
            if (buf[0][x] > 255) {
                buf[0][x] = 255;
            }
            if ((im->zsize == 2 || im->zsize == 4) && buf[im->zsize - 1][x] == 0) {
                putchar(' ');
            } else {
                putchar(".,~+o*%#"[buf[0][x] >> 5]);
            }
        }
        printf("|\n");
    }
    printf("\t+");
    for (x = 0; x < im->xsize; x++) {
        putchar('-');
    }
    printf("+\n#endif\n\n");
}

void InitHashTableStructures(void) {
    /* Probably not needed */

    int i;

    for (i = 0; i < 255; i++)
        ColorHashTable[i].NumberOfEntries = 0;
}

int PrintCIColorMap(int output, int flags) {
    int i, j, HashEntries, NumberOfColorsSeen;
    int red, green, blue, alpha;
    static unsigned short cmap[256];

    NumberOfColorsSeen = 0;
    for (i = 0; i < 256; i++) {
        HashEntries = ColorHashTable[i].NumberOfEntries;
        for (j = 0; j < HashEntries; j++) {
            unsigned short OutputValue;
            red = ColorHashTable[i].RGB[j].red;
            green = ColorHashTable[i].RGB[j].green;
            blue = ColorHashTable[i].RGB[j].blue;
            alpha = ColorHashTable[i].RGB[j].alpha;
            OutputValue = (red >> 3) << 11 | (green >> 3) << 6 | (blue >> 3) << 1 | alpha;
            cmap[ColorHashTable[i].RGB[j].ColorNumber] = OutputValue;
        }
    }

    NumberOfColorsSeen = 0;
    for (i = 0; i < 256; i++) {
        unsigned short OutputValue;

        OutputValue = cmap[i];
        if (!(flags & QUAD_FLAG)) {
            if (output == C)
                printf("0x%04x, ", OutputValue);
            else
                printf("%.2x %.2x ", ((OutputValue & 0xff00) >> 8), (OutputValue & 0xff));
        } else /* replicate each entry 4 times */
        {
            if (output == C)
                printf("0x%04x, 0x%04x, 0x%04x, 0x%04x, ", OutputValue, OutputValue, OutputValue,
                       OutputValue);
            else
                printf("%.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x ", ((OutputValue & 0xff00) >> 8),
                       (OutputValue & 0xff), ((OutputValue & 0xff00) >> 8), (OutputValue & 0xff),
                       ((OutputValue & 0xff00) >> 8), (OutputValue & 0xff),
                       ((OutputValue & 0xff00) >> 8), (OutputValue & 0xff));
        }

        NumberOfColorsSeen++;
        if (NumberOfColorsSeen % 8 == 0)
            printf("\n");
    };

    return (256);
}

int CIColorIndexValue(IMAGE *im, int siz, int fmt, int i, ushort red[MAX_WIDTH],
                      ushort green[MAX_WIDTH], ushort blue[MAX_WIDTH], ushort alpha[MAX_WIDTH],
                      ushort intensity[MAX_WIDTH], int output, int x) {
    int j, BestMatch, HashEntries;
    ushort HashValue;

    HashValue = intensity[x];
    BestMatch = -1;

    HashEntries = ColorHashTable[HashValue].NumberOfEntries;
    for (j = 0; j < HashEntries; j++) {
        if ((ColorHashTable[HashValue].RGB[j].red == red[x])
            && (ColorHashTable[HashValue].RGB[j].green == green[x])
            && (ColorHashTable[HashValue].RGB[j].blue == blue[x])
            && (ColorHashTable[HashValue].RGB[j].alpha == (alpha[x] ? 1 : 0)))
            BestMatch = ColorHashTable[HashValue].RGB[j].ColorNumber;
    }

    if (BestMatch == -1) {
        ColorHashTable[HashValue].NumberOfEntries++;
        ColorHashTable[HashValue].RGB[HashEntries].red = red[x];
        ColorHashTable[HashValue].RGB[HashEntries].green = green[x];
        ColorHashTable[HashValue].RGB[HashEntries].blue = blue[x];
        ColorHashTable[HashValue].RGB[HashEntries].alpha = alpha[x] ? 1 : 0;
        ColorHashTable[HashValue].RGB[HashEntries].ColorNumber = NumberOfColorsSeen;
        BestMatch = NumberOfColorsSeen++;
    }
    return (BestMatch);
}

printCIdata(IMAGE *im, int siz, int fmt, int i, ushort red[MAX_WIDTH], ushort green[MAX_WIDTH],
            ushort blue[MAX_WIDTH], ushort alpha[MAX_WIDTH], ushort intensity[MAX_WIDTH], int output,
            int shuffle_mask, int flags) {
    int xx, x, j, BestMatch1, BestMatch2, HashEntries;
    ushort HashValue;

    for (x = 0; x < im->xsize; x++) {
        xx = x ^ shuffle_mask;
        if (siz == 8) {
            BestMatch1 =
                CIColorIndexValue(im, siz, fmt, i, red, green, blue, alpha, intensity, output, xx);
            if (!(flags & SKIP_RAW_FLAG)) {
                if (output == C)
                    printf("0x%02x, ", BestMatch1);
                else
                    printf("%.2x ", BestMatch1);
            }
        } else {
            BestMatch1 =
                CIColorIndexValue(im, siz, fmt, i, red, green, blue, alpha, intensity, output, xx);
            BestMatch2 =
                CIColorIndexValue(im, siz, fmt, i, red, green, blue, alpha, intensity, output, xx + 1);
            if (!(flags & SKIP_RAW_FLAG)) {
                if (output == C)
                    printf("0x%02x, ", (BestMatch1 << 4) | BestMatch2);
                else
                    printf("%.2x ", (BestMatch1 << 4) | BestMatch2);
            }
            x++;
        }
    }
}

#define DO_DITHER

/*
 * 	256*256*219/255 is 56283.85882352 this is "1" for y
 * 	256*256*223/255 is 57311.87450980 this is "1" for u and v
 *
 *	The forward matrix is:
 *
 *	 0.2990        	 0.5870        	 0.1140
 *	-0.1686        	-0.3311        	 0.4997
 *	 0.4998        	-0.4185        	-0.0813
 *
 */
void RGBtoUYVY(rp, gp, bp, ap, xsize) unsigned short *rp, *gp, *bp;
unsigned char *ap;
int xsize;
{
    int i, r, g, b;
    long y1, y2, u, v, u1, u2, v1, v2;

#ifdef DO_DITHER
    y1 = 0x8000;
    y2 = 0x8000;
#else
    y1 = 0;
    y2 = 0;
#endif

    for (i = xsize / 2; i > 0; i--) {

        /* first pixel gives Y and 0.5 of chroma */
        r = *rp++;
        g = *gp++;
        b = *bp++;

        y1 = 16829 * r + 33039 * g + 6416 * b;
        u1 = -4831 * r + -9488 * g + 14319 * b;
        v1 = 14322 * r + -11992 * g + -2330 * b;
        y1 += (y2 & 0xffff);

        /* second pixel gives Y and 0.5 of chroma */
        r = *rp++;
        g = *gp++;
        b = *bp++;

        y2 = 16829 * r + 33039 * g + 6416 * b;
        u2 = -4831 * r + -9488 * g + 14319 * b;
        v2 = 14322 * r + -11992 * g + -2330 * b;
        y2 += (y1 & 0xffff);

        /* average the chroma */
        u = u1 + u2;
        v = v1 + v2;

        /* round the chroma */
        u1 = (u + 0x008000) >> 16;
        v1 = (v + 0x008000) >> 16;

        /* limit the chroma */
        if (u1 < -112)
            u1 = -112;
        if (u1 > 111)
            u1 = 111;
        if (v1 < -112)
            v1 = -112;
        if (v1 > 111)
            v1 = 111;

        /* limit the lum */
        if (y1 > 0x00dbffff)
            y1 = 0x00dbffff;
        if (y2 > 0x00dbffff)
            y2 = 0x00dbffff;

        /* save the results */
        ap[0] = u1 + 128;
        ap[1] = (y1 >> 16) + 16;
        ap[2] = v1 + 128;
        ap[3] = (y2 >> 16) + 16;
        ap += 4;
    }
}

int printdata(IMAGE *im, int siz, int fmt, int i, ushort red[MAX_WIDTH], ushort green[MAX_WIDTH],
              ushort blue[MAX_WIDTH], ushort alpha[MAX_WIDTH], ushort intensity[MAX_WIDTH], int output,
              int flags, int shuffle_mask) {
    int xx, x, data;
    unsigned short uyvy[MAX_WIDTH];

    /* Some of these are going to break for odd sizes, need to be fixed */

    if (siz == 4 && fmt == I) {
        for (x = 0; x < im->xsize; x += 2) {
            xx = x ^ shuffle_mask;
            if (!(flags & SKIP_RAW_FLAG)) {
                if ((output == C) || (output == MIPMAP))
                    printf("0x%x, ", (intensity[xx] & 0xF0) | ((intensity[xx + 1] & 0xf0) >> 4));
                else
                    printf("%.2x ", (intensity[xx] & 0xF0) | ((intensity[xx + 1] & 0xf0) >> 4));
            }
            if (output == MIPMAP)
                *savedp8++ = (intensity[xx] & 0xF0) | ((intensity[xx + 1] & 0xf0) >> 4);
        }
    } else if (siz == 4 && fmt == A) {
        for (x = 0; x < im->xsize; x += 2) {
            xx = x ^ shuffle_mask;
            if (!(flags & SKIP_RAW_FLAG)) {
                if ((output == C) || (output == MIPMAP))
                    printf("0x%x, ", (alpha[xx] & 0xF0) | (alpha[xx + 1] >> 4));
                else
                    printf("%.2x ", (alpha[xx] & 0xF0) | (alpha[xx + 1] >> 4));
            }
            if (output == MIPMAP)
                *savedp8++ = (alpha[xx] & 0xF0) | (alpha[xx + 1] >> 4);
        }
    } else if (siz == 4 && fmt == IA) {
        for (x = 0; x < im->xsize; x += 2) {
            xx = x ^ shuffle_mask;
            i = intensity[xx] + 16;
            if (i > 255) {
                i = 255;
            }
            data = (i & 0xE0) | ((1 & (alpha[xx] > 0)) << 4);
            i = intensity[xx + 1] + 16;
            if (i > 255) {
                i = 255;
            }
            data |= (((i >> 4) & 0xE) | (1 & (alpha[xx + 1] > 0)));
            if (!(flags & SKIP_RAW_FLAG)) {
                if (output == C)
                    printf("0x%x, ", data);
                else
                    printf("%.2x, ", data);
            }
            if (output == MIPMAP)
                *savedp8++ = data;
        }
    } else if (siz == 8 && fmt == I) {
        for (x = 0; x < im->xsize; x++) {
            xx = x ^ shuffle_mask;
            if (!(flags & SKIP_RAW_FLAG)) {
                if ((output == C) || (output == MIPMAP))
                    printf("0x%x, ", intensity[xx]);
                else
                    printf("%.2x ", intensity[xx]);
            }
            if (output == MIPMAP)
                *savedp8++ = intensity[xx];
        }
    } else if (siz == 8 && fmt == A) {
        for (x = 0; x < im->xsize; x++) {
            xx = x ^ shuffle_mask;
            if (!(flags & SKIP_RAW_FLAG)) {
                if ((output == C) || (output == MIPMAP))
                    printf("0x%x, ", alpha[xx]);
                else
                    printf("%.2x ", alpha[xx]);
            }
            if (output == MIPMAP)
                *savedp8++ = alpha[xx];
        }
    } else if (siz == 8 && fmt == IA) {
        for (x = 0; x < im->xsize; x++) {
            xx = x ^ shuffle_mask;
            if (!(flags & SKIP_RAW_FLAG)) {
                if ((output == C) || (output == MIPMAP))
                    printf("0x%x, ", (intensity[xx] & 0xF0) | alpha[xx] >> 4);
                else
                    printf("%.2x ", (intensity[xx] & 0xF0) | alpha[xx] >> 4);
            }
            if (output == MIPMAP)
                *savedp8++ = (intensity[xx] & 0xF0) | alpha[xx] >> 4;
        }
    } else if (siz == 16 && fmt == IA) {
        for (x = 0; x < im->xsize; x++) {
            xx = x ^ shuffle_mask;
            if (!(flags & SKIP_RAW_FLAG)) {
                if ((output == C) || (output == MIPMAP))
                    printf("0x%x, ", intensity[xx] << 8 | alpha[xx]);
                else
                    printf("%.2x %.2x ", intensity[xx], alpha[xx]);
            }
            if (output == MIPMAP)
                *savedp16++ = intensity[xx] << 8 | alpha[xx];
        }
    } else if (siz == 16 && fmt == RGBA) {
        unsigned short int val;
        int r, g, b;
        int red_prev = 0, green_prev = 0, blue_prev = 0;

        r = g = b = 4; /* Set them to 1/2 */

        for (x = 0; x < im->xsize; x++) {
            xx = x ^ shuffle_mask;

            if (red[xx] == red_prev) /* repeated color? */
                r = 4;               /* don't dither */

            if (green[xx] == green_prev)
                g = 4;

            if (blue[xx] == blue_prev)
                b = 4;

            r = red[xx] + (r & 0x7);
            if (r > 255)
                r = 255;
            g = green[xx] + (g & 0x7);
            if (g > 255)
                g = 255;
            b = blue[xx] + (b & 0x7);
            if (b > 255)
                b = 255;

            red_prev = red[xx];
            green_prev = green[xx];
            blue_prev = blue[xx];

            val = (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | alpha[xx] >> 7;

            if (!(flags & SKIP_RAW_FLAG)) {
                if ((output == C) || (output == MIPMAP))
                    printf("0x%x, ", val & 0xffff);
                else {
                    printf("%.2x %.2x ", (val & 0xFF00) >> 8, val & 0xFF);
                }
            }
            if (output == MIPMAP)
                *savedp16++ = val;
        }
    } else if (siz == 16 && fmt == YUV) {
        unsigned short int val;

        RGBtoUYVY(red, green, blue, (unsigned char *) uyvy, im->xsize);

        for (x = 0; x < im->xsize; x++) {
            xx = x ^ shuffle_mask;

            val = uyvy[xx];

            if (!(flags & SKIP_RAW_FLAG)) {
                if ((output == C) || (output == MIPMAP))
                    printf("0x%04x, ", val & 0xffff);
                else {
                    printf("%.2x %.2x ", (val & 0xFF00) >> 8, val & 0xFF);
                }
            }
            if (output == MIPMAP) {
                *savedp16++ = uyvy[x];
            }
        }
    } else if (siz == 32 && fmt == RGBA) {
        if ((output == C) || (output == MIPMAP)) {
            for (x = 0; x < im->xsize; x++) {
                xx = x ^ shuffle_mask;
                if (!(flags & SKIP_RAW_FLAG)) {
                    printf("0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, ", red[xx], green[xx], blue[xx], alpha[xx]);
                }
                if (output == MIPMAP) {
                    *savedp8++ = red[xx];
                    *savedp8++ = green[xx];
                    *savedp8++ = blue[xx];
                    *savedp8++ = alpha[xx];
                };
            }
        } else {
            for (x = 0; x < im->xsize; x++) {
                xx = x ^ shuffle_mask;
                if (!(flags & SKIP_RAW_FLAG)) {
                    printf("%.2x %.2x %.2x %.2x ", red[xx], green[xx], blue[xx], alpha[xx]);
                }
            }
        }
    } else if (fmt == CI) {
        if (output == MIPMAP) {
            if (!(flags & SKIP_RAW_FLAG)) {
                fprintf(stderr, "Error, CI C output not supported \n");
            }
        } else
            printCIdata(im, siz, fmt, i, red, green, blue, alpha, intensity, output, shuffle_mask,
                        flags);
    }

    if (!(flags & SKIP_RAW_FLAG)) {
        printf("\n");
    }

    return ((im->xsize * siz) / 8); /* Number of bytes! */
}
/**************************************************************************************
                Generate other mipmap
**************************************************************************************/

#include "ultra64.h"
#include <gu.h>

#define TRAM_SIZE 4096 /* in bytes */
#define TRAM_WSIZE 8   /* TRAM word size in bytes */
#define TRAM_LSIZE 8   /* TRAM load word size in bytes */
#define MM_MAX_LEVEL 7 /* number of mipmap levels 0 to MM_MAX_LEVEL */
#define MM_MIN_SIZE 1  /* smallest mipmap tile */

struct texelSizeParams {
    unsigned char gran;
    unsigned char shift;
    unsigned char tsize;
    unsigned char shiftr;
};

/* texture ram tile */
struct Tile {
    int w;    /* width of tile in texels, padded to tram line sz */
    int s, t; /* size of tile in texels */
    int addr; /* address in tram of tile */
};

/* tram mipmaps */
struct Tile mipmap[MM_MAX_LEVEL + 1];
struct texelSizeParams sizeParams[4] = { 16, 3, 1, 0, 8, 2, 2, 1, 4, 1, 4, 2, 2, 0, 8, 3 };

int max_mipmap;
unsigned char *tram;
int txlsize;
int errNo = 0;
int NA = 0;          /* Not applicable */
unsigned int length; /* total texels in mipmap */
int level;           /* total levels in mipmap */

void getQuad(struct Tile *tile, int *s, int *t, int *texel, int shift, int size);
void stuffDisplayList(Gfx **glistp, Image *im, char *tbuf, unsigned char pal, unsigned char cms,
                      unsigned char cmt, unsigned char masks, unsigned char maskt, unsigned char shifts,
                      unsigned char shiftt);
void kernel(int i, int r1, int g1, int b1, int a1, float *r2, float *g2, float *b2, float *a2);

#define unpack_ia16(c, i, a) i = (c & 0xff00) >> 8, a = (c & 0xff)
#define pack_ia16(i, a) (i << 8) | a

#define unpack_ia8(c, i, a) i = ((c & 0xf0) >> 4), a = (c & 0xf)
#define pack_ia8(i, a) (a & 0xf) | ((i & 0xf) << 4)

#define unpack_ia4(c, i, a) i = ((c & 0xe) >> 1), a = (c & 0x1)
#define pack_ia4(i, a) ((i & 0x7) << 1) | ((a & 0x1))

#define unpack_i4(c, i) i = (c & 0xf)
#define pack_i4(i) (i)

#define unpack_i8(c, i) i = (c & 0xff)
#define pack_i8(i) (i)

#define unpack_ci8(c, ci) unpack_i8(c, ci)
#define pack_ci8(ci) pack_i8(ci)

#define unpack_ci4(c, ci) unpack_i4(c, ci)
#define pack_ci4(ci) pack_i4(ci)

#define unpack_rgba(c, r, g, b, a)                                                                     \
    (r = (c & 0xf800) >> 11), g = ((c & 0x07c0) >> 6), b = ((c & 0x003e) >> 1), a = (c & 0x1)

#define pack_rgba(r, g, b, a) ((r & 0x1f) << 11) | (g & 0x1f) << 6 | ((b & 0x1f) << 1) | (a)

/*************************************************************************
 * Generates all levels of a power-of-two mipmap from an input array.	 *
 * Also stuffs display list with entries for loading and rendering the	 *
 * texture. Filtering Color-Index maps makes sense only if the lookup	 *
 * is a linear ramp. Billboards and trees cutout using alpha will change *
 * shape as the level changes due to change in map resolution. Texel 	 *
 * formats with only one bit of alpha will not be filtered very well.	 *
 *************************************************************************
 * ErrNo value		error description				 *
 *-----------------------------------------------------------------------*
 * 	1		Mipmap too big to load into tmem. Not Fatal,     *
 *			will load as many levels as there is space for.  *
 * 									 *
 * 	2		Texel format not supported, Fatal error		 *
 ************************************************************************/

int guLoadTextureBlockMipMapPrint(char *name, unsigned char *tbuf, Image *im, unsigned char pal,
                                  unsigned char cms, unsigned char cmt, unsigned char masks,
                                  unsigned char maskt, unsigned char shifts, unsigned char shiftt,
                                  int flags, unsigned char wcsf, unsigned char wctf) {

    unsigned char *iaddr, *taddr;
    int im_bytes, tr_bytes;
    int h, b;
    int flip;
    char startUnAligned;
    char endUnAligned;

    txlsize = sizeParams[im->siz].tsize; /* texel size in nibbles */
                                         /* to next line size */

    /*
     * Do top level map, swizzle bytes on odd t's
     */
    /* base char address of tile to be loaded */
    iaddr = ((im->t * im->lsize) + ((im->s * txlsize) >> 1) + im->base);

    /*check tile line starting and ending alignments along 4bit bndries */
    startUnAligned = ((im->s & 0x1) && (im->siz == G_IM_SIZ_4b));
    endUnAligned = (((im->s + im->w) & 0x1) && (im->siz == G_IM_SIZ_4b));

    im_bytes = ((im->w * txlsize + 1) >> 1); /* siz of 1 tile line in bytes */
    tr_bytes = im_bytes / TRAM_LSIZE;        /* no of tram lines per tile line */
    tr_bytes = tr_bytes * TRAM_LSIZE;        /* tile line size in bytes */
    if (im_bytes > tr_bytes)
        tr_bytes += TRAM_LSIZE;

    taddr = &tbuf[im->addr]; /* why ? make this zero?*/

    if (startUnAligned) {
        for (h = 0; h < im->h; h++) {
            flip = (h & 1) << 2; /*shift does not depend on txlsize*/
            for (b = 0; b < im_bytes; b++) {
                *(taddr + (b ^ flip)) = ((*(iaddr + b) & 0x0f) << 4) | ((*(iaddr + b + 1) & 0xf0) >> 4);
            }
            /* add last aligned nibble */
            if (!endUnAligned)
                *(taddr + ((b - 1) ^ flip)) &= (0xf0);
            /* pickup trailing bytes */
            for (b = im_bytes; b < tr_bytes; b++)
                *(taddr + (b ^ flip)) = 0;
            iaddr += im->lsize;
            taddr += tr_bytes;
        }
    } else /* if start aligned */
    {
        for (h = 0; h < im->h; h++) {
            flip = (h & 1) << 2; /*shift does not depend on txlsize*/
            for (b = 0; b < im_bytes; b++)
                *(taddr + (b ^ flip)) = *(iaddr + b);

            /* zero out last extra nibble */
            if (endUnAligned)
                *(taddr + ((b - 1) ^ flip)) &= (0xf0);
            /* pad trailing bytes with zeroes */
            for (b = im_bytes; b < tr_bytes; b++)
                *(taddr + (b ^ flip)) = 0;

            iaddr += im->lsize;
            taddr += tr_bytes;
        }
    }

    tram = tbuf;

    /* save tile attributes in top mipmap */
    mipmap[0].s = im->w;                       /* tile width  */
    mipmap[0].t = im->h;                       /* tile height */
                                               /*  guaranteed no remainder ? */
    mipmap[0].w = ((tr_bytes / txlsize) << 1); /* tile line width in texels*/
    mipmap[0].addr = im->addr;
    max_mipmap = MM_MAX_LEVEL;
    length = mipmap[0].w * mipmap[0].t; /* total texels in level 0 */

    /******************************************************************************
            Generate other levels of mipmap using a box filter
    ******************************************************************************/

    { /* generate mip map for this tile */
        unsigned char *taddr, *saddr;
        int shift = (int) sizeParams[im->siz].shift;
        int s, t, si, ti, sii, tii;
        int s4[9];
        int t4[9];
        int tex4[9];
        int r0, g0, b0, a0, r1, g1, b1, a1;
        float r2, g2, b2, a2;
        float dummy;
        int i0, ci0, ia0, i1, ci1, ia1;
        float i2, ci2, ia2;
        int texel;
        int i, trip;
        unsigned int tempaddr;
        int ntexels = ((TRAM_LSIZE / txlsize) << 1); /* texels per line */

        level = 0; /* need to check for memory overflow */
        while ((mipmap[level].s > 1) || (mipmap[level].t > 1)) {
            level++;
            /*
             * set new mipmap level address in bytes
             */
            mipmap[level].addr =
                mipmap[level - 1].addr + (mipmap[level - 1].w * txlsize * mipmap[level - 1].t >> 1);

            /*
             * grab location in tram pointing to the current level address
             */
            taddr = &(tram[mipmap[level].addr]);

            /*
             * downfilter by 2X, bump odd size
             * compute parameters for new mipmap level
             */
            mipmap[level].s = (mipmap[0].s) >> level;
            mipmap[level].t = (mipmap[0].t) >> level;

            if (mipmap[level].s == 0)
                mipmap[level].s = 1;
            if (mipmap[level].t == 0)
                mipmap[level].t = 1;

            /*
             * width must be a multiple of 8 bytes (padding for tram line size)
             */
            mipmap[level].w = ((mipmap[level].s + (ntexels - 1)) >> (shift + 1) << (shift + 1));

            /*
             * compute total no of texels to be loaded
             */
            length += mipmap[level].w * mipmap[level].t;
            if ((length * txlsize >> 1) >= TRAM_SIZE) {
                errNo = 1;
                length -= mipmap[level].w * mipmap[level].t;
                break;
            }

            /*
             * for each scanline
             */
            for (t = 0; t < mipmap[level - 1].t; t += 2) {
                flip = 0;
                trip = (t & 2) << 1; /* invert bit 4 on odd line */
                ti = t + 1;
                tii = t - 1;

                /*
                 * check filtering clamp/wrap flag and do accordingly
                 */
                if (wctf) {
                    if (ti >= mipmap[level - 1].t)
                        ti = t;
                    if (tii < 0)
                        tii = t;
                } else {
                    if (ti >= mipmap[level - 1].t)
                        ti = 0;
                    if (tii < 0)
                        tii = mipmap[level - 1].t - 1;
                }

                tempaddr = 0;

                for (s = 0; s < mipmap[level - 1].s; s += 2) {
                    si = s + 1;
                    sii = s - 1;
                    /*
                     * duplicate last texel for odd sizes for filtering
                     */
                    if (wcsf) {
                        if (si >= mipmap[level - 1].s)
                            si = s;
                        if (sii < 0)
                            sii = s;
                    } else {
                        if (si >= mipmap[level - 1].s)
                            si = 0;
                        if (sii < 0)
                            sii = mipmap[level - 1].s - 1;
                    }

                    /*
                     * grab the nine neighbours to apply kernel function
                     */
                    s4[0] = s;
                    t4[0] = tii;
                    s4[1] = si;
                    t4[1] = tii;
                    s4[2] = si;
                    t4[2] = t;
                    s4[3] = si;
                    t4[3] = ti;
                    s4[4] = s;
                    t4[4] = ti;
                    s4[5] = sii;
                    t4[5] = ti;
                    s4[6] = sii;
                    t4[6] = t;
                    s4[7] = sii;
                    t4[7] = tii;
                    s4[8] = s;
                    t4[8] = t;

                    getQuad(&mipmap[level - 1], s4, t4, tex4, shift, im->siz);

                    saddr = taddr + ((tempaddr >> 1) ^ trip);
                    r1 = g1 = b1 = a1 = ci1 = i1 = 0;
                    r2 = g2 = b2 = a2 = ci2 = i2 = 0;

                    /*
                     * Extract R,G and B components of the 9 texels and
                     * apply the filter kernel
                     */
                    switch (im->fmt) {
                        case (G_IM_FMT_RGBA):
                            if (im->siz == G_IM_SIZ_16b) {
                                for (i = 0; i < 9; i++) {
                                    unpack_rgba(tex4[i], r0, g0, b0, a0);
                                    kernel(i, r0, g0, b0, a0, &r2, &g2, &b2, &a2);
                                }
                                r1 = (int) (r2 / 16.0 + 0.5);
                                g1 = (int) (g2 / 16.0 + 0.5);
                                b1 = (int) (b2 / 16.0 + 0.5);
                                a1 = (int) (a2 / 16.0 + 0.5);

                            } else {
                                /*
                                 * RGBA32 is not supported
                                 */
                                errNo = 2;
                                return errNo;
                            }
                            break;

                        case (G_IM_FMT_YUV):
                            errNo = 2;
                            return errNo;
                            break;

                        case (G_IM_FMT_CI):
                            if (im->siz == G_IM_SIZ_4b) {
                                for (i = 0; i < 9; i++) {
                                    unpack_ci4(tex4[i], ci0);
                                    kernel(i, ci0, 0, 0, 0, &ci2, &dummy, &dummy, &dummy);
                                }
                                ci1 = (int) (ci2 / 16.0 + 0.5);
                            } else if (im->siz == G_IM_SIZ_8b) {
                                for (i = 0; i < 9; i++) {
                                    unpack_ci8(tex4[i], ci0);
                                    kernel(i, ci0, 0, 0, 0, &ci2, &dummy, &dummy, &dummy);
                                }
                                ci1 = (int) (ci2 / 16.0 + 0.5);
                            } else {
                                errNo = 2;
                                return errNo;
                            }
                            break;

                        case (G_IM_FMT_IA):
                            if (im->siz == G_IM_SIZ_4b) {
                                for (i = 0; i < 9; i++) {
                                    unpack_ia4(tex4[i], i0, a0);
                                    kernel(i, i0, a0, 0, 0, &i2, &a2, &dummy, &dummy);
                                }
                                i1 = (int) (i2 / 16.0 + 0.5);
                                a1 = (int) (a2 / 16.0 + 0.5);
                            } else if (im->siz == G_IM_SIZ_8b) {
                                for (i = 0; i < 9; i++) {
                                    unpack_ia8(tex4[i], i0, a0);
                                    kernel(i, i0, a0, 0, 0, &i2, &a2, &dummy, &dummy);
                                }
                                i1 = (int) (i2 / 16.0 + 0.5);
                                a1 = (int) (a2 / 16.0 + 0.5);
                            } else if (im->siz == G_IM_SIZ_16b) {
                                for (i = 0; i < 9; i++) {
                                    unpack_ia16(tex4[i], i0, a0);
                                    kernel(i, i0, a0, 0, 0, &i2, &a2, &dummy, &dummy);
                                }
                                i1 = (int) (i2 / 16.0 + 0.5);
                                a1 = (int) (a2 / 16.0 + 0.5);
                            } else {
                                errNo = 2;
                                return errNo;
                            }
                            break;

                        case (G_IM_FMT_I):
                            if (im->siz == G_IM_SIZ_4b) {
                                for (i = 0; i < 9; i++) {
                                    unpack_i4(tex4[i], i0);
                                    kernel(i, i0, 0, 0, 0, &i2, &dummy, &dummy, &dummy);
                                }
                                i1 = (int) (i2 / 16.0 + 0.5);
                            } else if (im->siz == G_IM_SIZ_8b) {
                                for (i = 0; i < 9; i++) {
                                    unpack_i8(tex4[i], i0);
                                    kernel(i, i0, 0, 0, 0, &i2, &dummy, &dummy, &dummy);
                                }
                                i1 = (int) (i2 / 16.0 + 0.5);
                            } else {
                                errNo = 2;
                                return errNo;
                            }

                        default:
                            break;
                    }

                    /*
                     * Pack fields into destination texel
                     */
                    switch (im->fmt) {

                        case (G_IM_FMT_RGBA):
                            texel = pack_rgba(r1, g1, b1, a1);

                            if (MipMapFile.levels >= level) {
                                texel = ((u16 *) MipMapFile
                                             .image[level - 1])[s / 2 + (t / 2) * mipmap[level].w];
                            }

                            *(short *) ((int) saddr ^ flip) = texel;
                            break;

                        case (G_IM_FMT_YUV):
                            break;

                        case (G_IM_FMT_CI):

                            if (im->siz == G_IM_SIZ_4b) {
                                texel = pack_ci4(ci1);

                                if (MipMapFile.levels >= level) {
                                    texel = ((u8 *) MipMapFile
                                                 .image[level - 1])[s / 4 + (t / 2) * mipmap[level].w];
                                    texel = ((s & 2) ? (texel) : (texel >> 4)) & 0xf;
                                }

                                *(char *) ((int) saddr ^ flip) |= (s & 0x2) ? (texel) : (texel << 4);
                            } else if (im->siz == G_IM_SIZ_8b) {
                                texel = pack_ci8(ci1);

                                if (MipMapFile.levels >= level) {
                                    texel = ((u8 *) MipMapFile
                                                 .image[level - 1])[s / 2 + (t / 2) * mipmap[level].w];
                                }

                                *(char *) ((int) saddr ^ flip) = texel;
                            }
                            break;

                        case (G_IM_FMT_IA):
                            if (im->siz == G_IM_SIZ_4b) {
                                texel = pack_ia4(i1, a1);

                                if (MipMapFile.levels >= level) {
                                    texel = ((u8 *) MipMapFile
                                                 .image[level - 1])[s / 4 + (t / 2) * mipmap[level].w];
                                    texel = ((s & 2) ? (texel) : (texel >> 4)) & 0xf;
                                }

                                *(char *) ((int) saddr ^ flip) |= (s & 0x2) ? (texel) : (texel << 4);
                            } else if (im->siz == G_IM_SIZ_8b) {
                                texel = pack_ia8(i1, a1);

                                if (MipMapFile.levels >= level) {
                                    texel = ((u8 *) MipMapFile
                                                 .image[level - 1])[s / 2 + (t / 2) * mipmap[level].w];
                                }

                                *(char *) ((int) saddr ^ flip) = texel;
                            } else if (im->siz == G_IM_SIZ_16b) {
                                texel = pack_ia16(i1, a1);

                                if (MipMapFile.levels >= level) {
                                    texel = ((u16 *) MipMapFile
                                                 .image[level - 1])[s / 2 + (t / 2) * mipmap[level].w];
                                }

                                *(short *) ((int) saddr ^ flip) = texel;
                            }
                            break;

                        case (G_IM_FMT_I):
                            if (im->siz == G_IM_SIZ_4b) {
                                texel = pack_i4(i1);

                                if (MipMapFile.levels >= level) {
                                    texel = ((u8 *) MipMapFile
                                                 .image[level - 1])[s / 4 + (t / 2) * mipmap[level].w];
                                    texel = ((s & 2) ? (texel) : (texel >> 4)) & 0xf;
                                }

                                *(char *) ((int) saddr ^ flip) |= (s & 0x2) ? (texel) : (texel << 4);
                            } else if (im->siz == G_IM_SIZ_8b) {
                                texel = pack_i8(i1);

                                if (MipMapFile.levels >= level) {
                                    texel = ((u8 *) MipMapFile
                                                 .image[level - 1])[s / 2 + (t / 2) * mipmap[level].w];
                                }

                                *(char *) ((int) saddr ^ flip) = texel;
                            }
                            break;
                    }

                    tempaddr += txlsize;

                } /* end s */

                taddr += ((mipmap[level].w * txlsize) >> 1);

            } /* end t */

            if (mipmap[level].s <= MM_MIN_SIZE && mipmap[level].t <= MM_MIN_SIZE) {
                max_mipmap = level;
                break;
            }
        } /* end level */

    } /* end generate mipmap */
    /*
     * Add entries for texture loading and rendering in DL
     * stuffDisplayList(glistp, im, tbuf, pal, cms, cmt, masks, maskt, shifts,
     * shiftt);
     */
    { /* stuff display list */
        int tile;
        int x, y;
        int ndls = 0;

        if (flags & XTRA_FLAG) {
            printf("#ifdef __MWERKS__\n#pragma align(32)\n#else\n");
            printf("static Gfx %s_dummy_aligner2[] = { gsSPEndDisplayList(), "
                   "gsSPEndDisplayList(), gsSPEndDisplayList(), gsSPEndDisplayList() "
                   "};\n",
                   name);
            printf("#endif\n\n");
        } else {
            printf("#ifdef __MWERKS__\n#pragma align(16)\n#else\n");
            printf("static Gfx %s_dummy_aligner2[] = { gsSPEndDisplayList() };\n", name);
            printf("#endif\n\n");
        }

        y = mipmap[level].addr + 8;

        if (flags & XTRA_FLAG)
            y = (y + 31) & (~31);

        printf("unsigned short %s_buf[] = {\n", name);

        for (y = 0; y < (mipmap[level].addr + 8); y += 8) {
            printf("\t");
            for (x = 0; (x < 8) && ((x + y) < (mipmap[level].addr + 8)); x += 2)
                printf("0x%02x%02x, ", tbuf[y + x], tbuf[y + x + 1]);
            if ((y & 8) == 8)
                printf("\n");
        };

        if (flags & XTRA_FLAG)
            while (y & 31) {
                printf(" 0x0000,\n"); /* Padding */
                y += 2;
            };

        printf("};\n\n");

        printf("Gfx %s_dl[] = {\n", name);

        if (im->siz == G_IM_SIZ_4b) {
            /* set texture image parameters*/
            printf("\tgsDPSetTextureImage( %d, %d, 1, %s_buf),\n", im->fmt, G_IM_SIZ_8b, name);
            ndls++;

            /* set tile no 7. for loading texture */
            printf("\tgsDPSetTile( %d, %d, %d, %d, %s, %d, %d, %d, %d, %d, %d, %d),\n", im->fmt,
                   G_IM_SIZ_8b, NA, 0, "G_TX_LOADTILE", NA, NA, NA, NA, NA, NA, NA);
            ndls++;

            /* Wait 'til all primitives are done */
            printf("\tgsDPLoadSync(),\n");
            ndls++;

            /* Load texture into tram dxt = 0 for turning off swizzling*/
            printf("\tgsDPLoadBlock( %s, %d, %d, %d, %d),\n", "G_TX_LOADTILE", 0, 0, length / 2, 0x0);
            ndls++;

        } else {
            /* set texture image parameters*/
            printf("\tgsDPSetTextureImage( %d, %d, 1, %s_buf),\n", im->fmt, im->siz, name);
            ndls++;

            /* set tile no 7. for loading texture */
            printf("\tgsDPSetTile( %d, %d, %d, %d, %s, %d, %d, %d, %d, %d, %d, %d),\n", im->fmt,
                   im->siz, NA, 0, "G_TX_LOADTILE", NA, NA, NA, NA, NA, NA, NA);
            ndls++;

            /* Load texture into tram dxt = 0 for turning off swizzling*/
            printf("\tgsDPLoadBlock( %s, %d, %d, %d, %d),\n", "G_TX_LOADTILE", 0, 0, length, 0x0);
            ndls++;
        }

        for (tile = 0; tile <= level; tile++) {
            if (maskt == 255)
                maskt = 0; /* unsigned char */
            if (masks == 255)
                masks = 0;
            printf("\tgsDPSetTile(%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d),\n", im->fmt, im->siz,
                   (mipmap[tile].w * txlsize >> 4), (mipmap[tile].addr >> 3), tile, pal, cmt, (maskt--),
                   tile, cms, (masks--), tile);
            ndls++;
            if (flags & HALF_SHIFT)
                printf("\tgsDPSetTileSize( %d,  %d, %d, %d << %s, %d << %s),\n", tile, 2, 2,
                       (mipmap[tile].s - 1), "G_TEXTURE_IMAGE_FRAC", (mipmap[tile].t - 1),
                       "G_TEXTURE_IMAGE_FRAC");
            else
                printf("\tgsDPSetTileSize( %d,  %d, %d, %d << %s, %d << %s),\n", tile, 0, 0,
                       (mipmap[tile].s - 1), "G_TEXTURE_IMAGE_FRAC", (mipmap[tile].t - 1),
                       "G_TEXTURE_IMAGE_FRAC");
            ndls++;
        };

        printf("\tgsSPEndDisplayList(),\n");
        ndls++;

        while (ndls++ & 7)
            printf("\tgsSPEndDisplayList(),\n"); /* Padding */

        printf("};\n");
    }

    return errNo;
} /* end guLoadTextureBlockMipMap */

/******************************************************************************
 *
 * Apply Kernel :
 *			1  2  1
 * 			2  4  1
 * 			1  2  1
 ******************************************************************************/
void kernel(int i, int r0, int g0, int b0, int a0, float *r2, float *g2, float *b2, float *a2) {
    if (i == 8) {
        *r2 += r0 * 4;
        *g2 += g0 * 4;
        *b2 += b0 * 4;
        *a2 += a0 * 4;
    } else if (i % 2 == 0) {
        *r2 += r0 * 2;
        *g2 += g0 * 2;
        *b2 += b0 * 2;
        *a2 += a0 * 2;
    } else {
        *r2 += r0;
        *g2 += g0;
        *b2 += b0;
        *a2 += a0;
    }
}

/******************************************************************************
        Extract quad of texels for filtering.  Compute bank and row addresses.
******************************************************************************/
void getQuad(struct Tile *tile, int *s, int *t, int *texel, int shift, int size) {
    int i;
    int bank, row;
    unsigned int addr;
    int overlap;
    unsigned char r, g, b, a;
    unsigned long tex;
    struct Image *im;
    unsigned int ss, tt;

    for (i = 0; i < 9; i++) {
        ss = s[i];
        tt = t[i];
        /* bank and row indexing */
        bank = (((ss & (0x3 << (shift - 1))) >> (shift - 1)) ^ ((tt & 0x1) << 1)) << 1;
        row = (((tt * tile->w + ss) * txlsize) >> 1) / TRAM_LSIZE;
        addr = tile->addr + row * TRAM_WSIZE + bank;

        overlap = (i == 0) ? bank : overlap ^ bank;

        switch (size) {
            case G_IM_SIZ_4b:
                texel[i] = (tram[addr + ((ss & 0x2) >> 1)] & (0xf0 >> ((ss & 0x1) << 2)));
                if (!(ss & 0x1))
                    texel[i] = texel[i] >> 4;
                break;

            case G_IM_SIZ_8b:
                texel[i] = tram[addr + (ss & 0x1)];
                break;

            case G_IM_SIZ_16b:
                texel[i] = (tram[addr] << 8) | tram[addr + 1];
                break;

            case G_IM_SIZ_32b:
                errNo = 2; /* Format not supported */
                break;

            default:
                break;
        }
    }
}

void mipmap_it(IMAGE *im, int siz, int fmt, char *name, int flags) {
    static Image img;
    static Image *imp = &img;
    static double tbuf_align[2];
    static unsigned char tbuf[4096];
    unsigned char masks, maskt;
    int lvl;

    imp->base = (unsigned char *) saved_pic;

    switch (fmt) {
        case I:
            imp->fmt = G_IM_FMT_I;
            break;
        case A:
            imp->fmt = G_IM_FMT_I;
            break;
        case IA:
            imp->fmt = G_IM_FMT_IA;
            break;
        case CI:
            imp->fmt = G_IM_FMT_CI;
            break;
        case YUV:
            imp->fmt = G_IM_FMT_YUV;
            break;
        case RGBA:
            imp->fmt = G_IM_FMT_RGBA;
            break;
        default:
            fprintf(stderr, "Bad FMT <%d>\n", fmt);
            break;
    };

    switch (siz) {
        case 4:
            imp->siz = G_IM_SIZ_4b;
            break;
        case 8:
            imp->siz = G_IM_SIZ_8b;
            break;
        case 16:
            imp->siz = G_IM_SIZ_16b;
            break;
        case 32:
            imp->siz = G_IM_SIZ_32b;
            break;
        default:
            imp->siz = G_IM_SIZ_32b;
            break;
    };

    imp->xsize = im->xsize;
    imp->ysize = im->ysize;
    imp->lsize = (im->xsize * siz) / 8;
    imp->addr = 0;
    imp->w = im->xsize;
    imp->h = im->ysize;
    imp->s = 0;
    imp->t = 0;

    masks = maskt = 0;

    while ((im->xsize >> masks) > 1)
        masks++;
    while ((im->ysize >> maskt) > 1)
        maskt++;

    /* generate mipmaps, load data and set tiles */
    guLoadTextureBlockMipMapPrint(name, tbuf, imp, 0, G_TX_WRAP | G_TX_NOMIRROR,
                                  G_TX_WRAP | G_TX_NOMIRROR, masks, maskt, G_TX_NOLOD, G_TX_NOLOD,
                                  flags, 0, 0);

    free(saved_pic);
    for (lvl = 0; lvl < MipMapFile.levels; lvl++)
        free(MipMapFile.image[lvl]);
}

static int exec_cmd(char *fmt, ...) {
    va_list ap;
    int status;

    va_start(ap, fmt);
    vsprintf(TempString, fmt, ap);
    va_end(ap);

    status = system(TempString);
    if (status == -1) {
        perror("system");
        return 0;
    } else if (WIFEXITED(status) && (WEXITSTATUS(status) == 0)) {
        return 1;
    } else {
        return 0;
    }
}

/*
 * readtex()
 *
 *   Loads a texture in SGI format and writes it out as a C array.
 *
 *   Returns 1 on success, 0 on failure.
 */

int readtex(char *fn, struct texture *tex, int fmt, int siz, int makestatic, int lr, int lg, int lb,
            int hr, int hg, int hb, int output, int flags, int shuffle_mask) {
    IMAGE *im;
    ushort buf[4][MAX_WIDTH], red[MAX_WIDTH], green[MAX_WIDTH], blue[MAX_WIDTH], alpha[MAX_WIDTH];
    ushort intensity[MAX_WIDTH];
    int x, y, i, data;
    int dy, yfinal;
    int num_bytes;
    int cur_shuf_mask;
    int odd_line_flag;
    int xsize, xsav;
    int lvl;
    unsigned char *saved_pic_save;

    MipMapFile.mm_supplied = 0;

    if (lr <= hr && lg <= hg && lb <= hb) {
        lr = lg = lb = 0;
        hr = hg = hb = 255;
    }

    /* For now, assume SGI RGB: */

    if (fmt == CI) {
        if (flags & MM_SUPPLIED_FLAG) {
            printf("supplied mip maps not currently supported with Color Index (CI) "
                   "textures\n");
            return 0;
        }
        if (siz == 4)
            QuantLevels = 16;
        else if (siz == 8)
            QuantLevels = 256;
        else
            printf("Error in bit size specification for CI file \n");

        if (!exec_cmd("/usr/sbin/cscale %s %s.fooa 2040 2040 2040 -d", fn, TEMPFILENAME))
            return 0;
        if (!exec_cmd("/usr/sbin/cscale %s.fooa %s.foob 2040 2040 2040", TEMPFILENAME, TEMPFILENAME))
            return 0;
        if (!exec_cmd("/usr/sbin/toppm %s.foob %s.ppm", TEMPFILENAME, TEMPFILENAME))
            return 0;
        if (!exec_cmd("$ROOT/usr/sbin/ppmquant %d %s.ppm > %s", QuantLevels, TEMPFILENAME,
                      TEMPFILENAME))
            return 0;
        if (!exec_cmd("/usr/sbin/fromppm %s %s.rgb", TEMPFILENAME, TEMPFILENAME))
            return 0;

        im = iopen(fn, "r");
        if (im == NULL) {
            printf(" *   ERROR: Texture file \"%s\" not "
                   "found.\n */\n\n",
                   fn);
            return 0;
        }

        if (im->zsize == 4) {
            if (!exec_cmd("/usr/sbin/oneband %s.rgb %s.r 0", TEMPFILENAME, TEMPFILENAME))
                return 0;
            if (!exec_cmd("/usr/sbin/oneband %s.rgb %s.g 1", TEMPFILENAME, TEMPFILENAME))
                return 0;
            if (!exec_cmd("/usr/sbin/oneband %s.rgb %s.b 2", TEMPFILENAME, TEMPFILENAME))
                return 0;
            if (!exec_cmd("/usr/sbin/oneband %s %s.a 3", fn, TEMPFILENAME))
                return 0;
            if (!exec_cmd("/usr/sbin/cglue %s.r %s.g %s.b %s.a %s.rgba", TEMPFILENAME, TEMPFILENAME,
                          TEMPFILENAME, TEMPFILENAME, TEMPFILENAME))
                return 0;

            sprintf(TempString, "%s.rgba", TEMPFILENAME);
        } else if (im->zsize == 2) {
            if (!exec_cmd("/usr/sbin/oneband %s.rgb %s.r 0", TEMPFILENAME, TEMPFILENAME))
                return 0;
            if (!exec_cmd("/usr/sbin/oneband %s %s.a 3", fn, TEMPFILENAME))
                return 0;
            if (!exec_cmd("/usr/sbin/cglue %s.r %s.a %s.rgba", TEMPFILENAME, TEMPFILENAME,
                          TEMPFILENAME))
                return 0;

            sprintf(TempString, "%s.rgba", TEMPFILENAME);
        } else {
            sprintf(TempString, "%s.rgb", TEMPFILENAME);
        }

        iclose(im);

        im = iopen(TempString, "r");
        if (im == NULL) {
            printf(" *   ERROR: cannot open temporary file %s \n", TEMPFILENAME);
            return 0;
        }

        InitHashTableStructures();
    } else {
        if ((flags & MM_SUPPLIED_FLAG) && (flags & MIPMAP)) {
            char *c, *cto;

            MipMapFile.mm_supplied = 1;
            cto = MipMapFile.base;
            for (c = fn; c != '\0' && strncmp(c, "0.rgb", 5) && strncmp(c, ".rgb", 4); c++)
                *(cto++) = *c;
            *(cto++) = '\0';
            for (; c != '\0' && strncmp(c, ".rgb", 4); c++)
                ;
            (void) strcpy(MipMapFile.ext, c);
            if (MipMapFile.ext[0] == '\0' || MipMapFile.base[0] == '\0') {
                printf("texture base name invalid\n");
                return 0;
            }

            /*
             * Call readtex (recursive) to load mip map images
             */
            saved_pic_save = saved_pic;
            MipMapFile.levels = 0;
            for (lvl = 0; lvl < 7; lvl++) {
                FILE *fp;
                char name[100];
                int done = 0;

                sprintf(name, "%s%d%s", MipMapFile.base, lvl + 1, MipMapFile.ext);
                sprintf(MipMapFile.tex[lvl].name, "%s_lvl%d", tex->name, lvl + 1);
                if (!(fp = fopen(name, "r")))
                    done = 1;
                fclose(fp);
                if (done)
                    break;
                fprintf(stderr, "loading texture file %s\n", name);
                readtex(name, &MipMapFile.tex[lvl], fmt, siz, makestatic, lr, lg, lb, hr, hg, hb,
                        MIPMAP, (flags & ~MM_SUPPLIED_FLAG) | MM_HI_LEVEL, shuffle_mask);
                MipMapFile.image[lvl] = saved_pic;
                MipMapFile.levels++;
            }
            saved_pic = saved_pic_save;
        }

        im = iopen(fn, "r");

        if (im == NULL) {
            printf(" *   ERROR: Texture file \"%s\" not "
                   "found.\n */\n\n",
                   fn);
            return 0;
        }
    }

    /*
     * Fill out the fmt and siz parameters if necessary and make sure
     * that they are a valid combination.
     */

    if (fmt == -1) {
        fmt = bestfmt[im->zsize - 1];
    }

    if (siz == -1) {
        siz = bestsiz[im->zsize - 1];
    }

    switch (fmt) {
        case RGBA:
            if (siz != 16 && siz != 32) {
                siz = 16;
            }
            break;
        case YUV:
            siz = 16;
            /* XXX */
            break;
        case CI:
            /* XXX */
            break;
        case IA:
            if (siz != 4 && siz != 8 && siz != 16) {
                siz = 16;
            }
            break;
        case I:
        case A:
            if (siz != 4 && siz != 8) {
                siz = 8;
            }
            break;
        case MASK:
            /* XXX */
            break;
        default:
            fprintf(stderr, "Invalid format: %s\n", fmtstr(fmt));
            return 0;
    }

    if (output == C)
        printCheader(siz, im, fmt, tex);
    else if (output == MIPMAP)
        printMipMapheader(siz, im, fmt, tex);
    else
        printRAWheader(siz, im, fmt, tex);

    tex->width = im->xsize;
    tex->height = im->ysize;

    if (output == C)
        printpreview(im, buf);
    if (output == MIPMAP)
        printpreview(im, buf);

    /*
     * Output the data.
     */

    if (((output == C) || (output == MIPMAP)) && !(flags & SKIP_RAW_FLAG)) {
        if (flags & XTRA_FLAG) {
            printf("#ifdef __MWERKS__\n#pragma align(32)\n#else\n");
            printf("static Gfx %s%s_dummy_aligner1[] = { gsSPEndDisplayList(), "
                   "gsSPEndDisplayList(),\n\tgsSPEndDisplayList(), "
                   "gsSPEndDisplayList() };\n",
                   tex->name, ((output == C) ? "_C" : "_MIP"));
            printf("#endif\n\n");
        } else {
            printf("#ifdef __MWERKS__\n#pragma align(16)\n#else\n");
            printf("static Gfx %s%s_dummy_aligner1[] = { gsSPEndDisplayList() };\n", tex->name,
                   ((output == C) ? "_C" : "_MIP"));
            printf("#endif\n\n");
        }
        if (makestatic) {
            printf("static ");
        }
        if (siz == 16) {
            printf("unsigned short ");
        } else {
            printf("unsigned char ");
        }
        if (output == C)
            printf("%s[] = {\n", tex->name);
        else
            printf("%s_orig[] = {\n", tex->name);
    }

    if (flags & FLIP_FLAG) {
        y = 0;
        dy = 1;
        yfinal = im->ysize - 1;
    } else {
        y = im->ysize - 1;
        dy = -1;
        yfinal = 0;
    };

    num_bytes = 0;
    odd_line_flag = 0;
    xsize = im->xsize;
    if (flags & PAD_FLAG) {
        if (siz == 32) /* treat lines as 16-bit size */
            xsize = ((xsize * 16 + 63) / 64) * (64 / 16);
        else if (siz == 16 && fmt == YUV) /* treat lines as 8-bit size */
            xsize = ((xsize * 8 + 63) / 64) * (64 / 8);
        else
            xsize = ((xsize * siz + 63) / 64) * (64 / siz);
    };

    for (; y != (yfinal + dy); y += dy) {
        if (output == C)
            printf("\t");

        for (i = 0; i < im->zsize; i++) {
            getrow(im, buf[i], y, i); /* R */
        }

        switch (im->zsize) {
            case 1:
                for (x = 0; x < im->xsize; x++) {
                    red[x] = (hr - lr) * buf[0][x] / 255 + lr;
                    green[x] = (hg - lg) * buf[0][x] / 255 + lg;
                    blue[x] = (hb - lb) * buf[0][x] / 255 + lb;
                    intensity[x] = buf[0][x];
                    alpha[x] = buf[0][x];
                };
                for (; x < xsize; x++) {
                    red[x] = red[x - 1];
                    green[x] = green[x - 1];
                    blue[x] = blue[x - 1];
                    intensity[x] = intensity[x - 1];
                    alpha[x] = alpha[x - 1];
                }
                break;
            case 2:
                for (x = 0; x < im->xsize; x++) {
                    red[x] = (hr - lr) * buf[0][x] / 255 + lr;
                    green[x] = (hg - lg) * buf[0][x] / 255 + lg;
                    blue[x] = (hb - lb) * buf[0][x] / 255 + lb;
                    intensity[x] = buf[0][x];
                    alpha[x] = buf[1][x];
                }
                for (; x < xsize; x++) {
                    red[x] = red[x - 1];
                    green[x] = green[x - 1];
                    blue[x] = blue[x - 1];
                    intensity[x] = intensity[x - 1];
                    alpha[x] = alpha[x - 1];
                }
                break;
            case 3:
                for (x = 0; x < im->xsize; x++) {
                    red[x] = buf[0][x];
                    green[x] = buf[1][x];
                    blue[x] = buf[2][x];
                    i = (int) (0.299 * buf[0][x] + 0.587 * buf[1][x] + 0.114 * buf[2][x] + 0.5);
                    if (i < 0) {
                        i = 0;
                    }
                    if (i > 255) {
                        i = 255;
                    }
                    intensity[x] = i;

                    alpha[x] = 255;
                }
                for (; x < xsize; x++) {
                    red[x] = red[x - 1];
                    green[x] = green[x - 1];
                    blue[x] = blue[x - 1];
                    intensity[x] = intensity[x - 1];
                    alpha[x] = alpha[x - 1];
                }
                break;
            case 4:
                for (x = 0; x < im->xsize; x++) {
                    red[x] = buf[0][x];
                    green[x] = buf[1][x];
                    blue[x] = buf[2][x];
                    i = (int) (0.299 * buf[0][x] + 0.587 * buf[1][x] + 0.114 * buf[2][x] + 0.5);
                    if (i < 0) {
                        i = 0;
                    }
                    if (i > 255) {
                        i = 255;
                    }
                    intensity[x] = i;
                    alpha[x] = buf[3][x];
                }
                for (; x < xsize; x++) {
                    red[x] = red[x - 1];
                    green[x] = green[x - 1];
                    blue[x] = blue[x - 1];
                    intensity[x] = intensity[x - 1];
                    alpha[x] = alpha[x - 1];
                }
                break;
        }

        if ((flags & SHUF_FLAG) && odd_line_flag)
            cur_shuf_mask = shuffle_mask;
        else
            cur_shuf_mask = 0;

        xsav = im->xsize;
        im->xsize = xsize;

        num_bytes += printdata(im, siz, fmt, i, red, green, blue, alpha, intensity, output, flags,
                               cur_shuf_mask);

        im->xsize = xsav;

        odd_line_flag ^= 1;
    }

    if ((flags & XTRA_FLAG) && !(flags & SKIP_RAW_FLAG) && ((output == C) || (output == MIPMAP))) {

        /* We need to pad the output so far to be a multiple of 32 bytes */

        while (num_bytes & 0x1f)
            if (siz == 16) {
                printf("0x0000, ");
                num_bytes += 2;
            } else {
                printf("0x00, ");
                num_bytes += 1;
            };
    };

    if ((output == C) && !(flags & SKIP_RAW_FLAG))
        printf("};\n\n");

    if (output == MIPMAP) {
        if (!(flags & SKIP_RAW_FLAG))
            printf("};\n\n");
        if (!(flags & MM_HI_LEVEL))
            mipmap_it(im, siz, fmt, tex->name, flags);
    };

    iclose(im);

    if (fmt == CI) {

        if (flags & XTRA_FLAG) {
            printf("#ifdef __MWERKS__\n#pragma align(32)\n#else\n");
            printf("static Gfx %stlut_dummy_aligner1[] = { \n\tgsSPEndDisplayList(), "
                   "gsSPEndDisplayList(),\n\tgsSPEndDisplayList(), "
                   "gsSPEndDisplayList() };\n",
                   tex->name);
            printf("#endif\n\n");
        } else {
            printf("#ifdef __MWERKS__\n#pragma align(16)\n#else\n");
            printf("static Gfx %stlut_dummy_aligner1[] = { \n\tgsSPEndDisplayList() "
                   "};\n",
                   tex->name);
            printf("#endif\n\n");
        }

        printf("unsigned short %stlut[] = {\n", tex->name);

        num_bytes = 2 * PrintCIColorMap(output, flags);

        if (flags & XTRA_FLAG)
            while (num_bytes & 0x1f) {
                printf("0x0000, ");
                num_bytes += 2;
            };

        printf("};\n");

        if (!exec_cmd("rm -f %s %s.r %s.g %s.b %s.a %s.ppm %s.rgb %s.rgba "
                      "%s.fooa %s.foob",
                      TEMPFILENAME, TEMPFILENAME, TEMPFILENAME, TEMPFILENAME, TEMPFILENAME,
                      TEMPFILENAME, TEMPFILENAME, TEMPFILENAME, TEMPFILENAME, TEMPFILENAME))
            return 0;
    }

    tex->fmt = fmt;
    tex->siz = siz;

    return 1;
}
