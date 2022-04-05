

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "readtex.h"

void usage(void) {
    fprintf(stderr, "\nusage: rgb2c [options] image.png\n"
                    "\n"
                    "options:  -m name    Name the texture (defaults to \"texture\").\n"
                    "          -Q         Quadricate lookup table for color index texture\n"
                    "          -C fname   CI Palette output location (if not defined pallete embeds in file)\n"
                    "          -B         Create Kirby64 Image Header\n"
                    "          -W width   Image Header reported width\n"
                    "          -H height  Image Header reported height\n"
                    "          -P         Toggle Padding of texel rows for use in Load Block "
                    "(default=on)\n"
                    "          -F         Flip Image Vertically\n"
                    "          -S smask   Swap 32-bit words on odd lines (smask = bit to "
                    "toggle in texels)\n"
                    "          -X         Xtra padding to make all declared objects be 32x "
                    "bytes long\n"
                    "          -t citype  Color Index type (C, I)\n"
                    "          -f fmt     Format of texel (RGBA, YUV, CI, IA, I, A)\n"
                    "			(defaults to best type for image).\n"
                    "          -o output  Format of output text (`C' for .c output, `RAW'\n"
                    "                        for raw ascii format, 'MIP' for MipMapping "
                    "(.c),\n"
                    "                        'MIPSUPPLIED' for supplied mip maps (.c)\n"
                    "          -b         toggle shifting each tile by 0.5.  This is "
                    "neccessary\n"
                    "                       when mip map levels are generated using certain\n"
                    "                       types of filters.  DEFAULT:\n"
                    "                         shift ON for MIPSUPPLIED\n"
                    "                         shift OFF for MIP\n"
                    "                         not applicable for RAW or C formats\n"
                    "          -r         Toggle include raw texture data in .c file\n"
                    "                       (default = include the raw data)\n"
                    "          -s bits    Size of each texel in bits (4, 8, 16, or 32)\n"
                    "			(defaults to best type for image).\n"
                    "          -l r,g,b   / Specify the low and high colors to interpolate\n"
                    "          -h r,g,b   / between when making RGB from intensity.\n\n");

    exit(1);
}

int main(int argc, char *argv[]) {
    int c, lr, lg, lb, hr, hg, hb;
    extern int optind;
    extern char *optarg;
    int fmt, siz, output, ColorIndexType;
    int flags;
    struct texture tex;
    int shuf_mask;
    char *palettename = NULL;
    int makeBG = 0;
    int realheight = 0;
    int realwidth = 0;

    strcpy(tex.name, "texture");
    lr = lg = lb = 0;
    hr = hg = hb = 255;
    flags = PAD_FLAG | FLIP_FLAG; /* Default is ON to correct for SGI format */
    fmt = -1;
    siz = -1;
    shuf_mask = 0;
    output = C;
    ColorIndexType = COLOR;

    while ((c = getopt(argc, argv, "W:H:l:h:m:f:C:s:o:t:FPrBXQS:")) != EOF) {
        switch (c) {
            case 'C':
                palettename = (char *)malloc(1025);
                strcpy(palettename, optarg);
                break;
            case 'W':
                realwidth = atoi(optarg);
                break;
            case 'H':
                realheight = atoi(optarg);
                break;
            case 'B':
                flags ^= MAKE_BG_FLAG;
                break;
            case 'Q':
                flags ^= QUAD_FLAG;
                break;
            case 'P':
                flags ^= PAD_FLAG;
                break;
            case 'F':
                flags ^= FLIP_FLAG;
                break;
            case 'S':
                flags ^= SHUF_FLAG;
                shuf_mask = atoi(optarg);
                break;
            case 'X':
                flags ^= XTRA_FLAG;
                break;
            case 'm':
                strcpy(tex.name, optarg);
                break;
            case 'f':
                if (strcasecmp(optarg, "RGBA") == 0) {
                    fmt = RGBA;
                } else if (strcasecmp(optarg, "IA") == 0) {
                    fmt = IA;
                } else if (strcasecmp(optarg, "I") == 0) {
                    fmt = I;
                } else if (strcasecmp(optarg, "A") == 0) {
                    fmt = A;
                } else if (strcasecmp(optarg, "CI") == 0) {
                    fmt = CI;
                } else if (strcasecmp(optarg, "YUV") == 0) {
                    fmt = YUV;
                } else {
                    fprintf(stderr,
                            "Unknown format "
                            "type: %s.\n",
                            optarg);
                    exit(1);
                }
                break;
            case 'r':
                flags ^= SKIP_RAW_FLAG;
                break;
            case 'b':
                flags ^= HALF_SHIFT;
                break;
            case 'o':
                if (strcasecmp(optarg, "C") == 0) {
                    output = C;
                    flags &= ~MM_SUPPLIED_FLAG;
                } else if (strcasecmp(optarg, "RAW") == 0) {
                    output = RAW;
                    flags &= ~MM_SUPPLIED_FLAG;
                } else if (strcasecmp(optarg, "MIP") == 0) {
                    output = MIPMAP;
                    flags &= ~MM_SUPPLIED_FLAG;
                } else if (strcasecmp(optarg, "MIPSUPPLIED") == 0) {
                    output = MIPMAP;
                    flags |= MM_SUPPLIED_FLAG;
                    flags ^= HALF_SHIFT;
                } else {
                    fprintf(stderr,
                            "Unknown format "
                            "type: %s. \n",
                            optarg);
                    exit(1);
                }
                break;
            case 't':
                if (strcasecmp(optarg, "C") == 0) {
                    ColorIndexType = COLOR;
                } else if (strcasecmp(optarg, "I") == 0) {
                    ColorIndexType = INTENSITY;
                } else {
                    fprintf(stderr,
                            "Unknown color index type "
                            ": %s. \n",
                            optarg);
                }
                break;
            case 's':
                siz = atoi(optarg);
                break;
            case 'l':
                sscanf(optarg, "%d,%d,%d", &lr, &lg, &lb);
                break;
            case 'h':
                sscanf(optarg, "%d,%d,%d", &hr, &hg, &hb);
                break;
            case '?':
                usage();
                break;
        }
    }

    if (optind == argc) {
        usage();
    }

    fprintf(stderr, "Making texture %s\n", tex.name);

    if (output != MIPMAP)
        flags &= ~SKIP_RAW_FLAG;

    if ((output == C) || (output == MIPMAP)) {
        printf("/*\n");
        printf(" * Do not edit this file.  It was automatically generated\n");
        printf(" * by \"rgb2c\" from the file \"%s\".\n", argv[optind]);
        printf(" *\n");
    }

    if (realheight > 0) tex.realheight = realheight;
    if (realwidth > 0) tex.realwidth = realwidth;

    tex_convert(argv[optind], &tex, fmt, siz, 0, lr, lg, lb, hr, hg, hb, output, flags, shuf_mask, palettename);
}
