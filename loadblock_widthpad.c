#include "readtex.h"
#include "lodepng.h"


int widthTab[] = {4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 48, 64, 72, 76, 100, 108, 128, 144, 152, 164, 200, 216, 228, 256, 304, 328, 432, 456, 512, 684, 820, 912};

int gClampWidth_LoadBlock(int w) {
    for (int i = 0; i < ACOUNT(widthTab); i++) {
        if (widthTab[i] >= w) {
            return widthTab[i];
        }
    }
    return w;
}

int gWidthDiff_LoadBlock(int w) {
    for (int i = 0; i < ACOUNT(widthTab); i++) {
        if (widthTab[i] >= w) {
            return widthTab[i] - w;
        }
    }
    return 0;
}

