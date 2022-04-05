#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "readtex.h"
#include "lodepng.h"

void export_ci(Texture *tex, char *filename) {
    LodePNGState state;

    unsigned char* image = 0;
    unsigned char* png = 0;
    size_t pngsize;
    int error;

    lodepng_state_init(&state);

    state.info_png.color.colortype = LCT_PALETTE;
    state.info_png.color.bitdepth = tex->siz;
    state.info_raw.colortype = LCT_PALETTE;
    state.info_raw.bitdepth = tex->siz;

    error = lodepng_load_file(&png, &pngsize, filename);
    if(!error) error = lodepng_decode(&image, &tex->width, &tex->height, &state, png, pngsize);
    if(error) fprintf(stderr, "error %u: %s\n", error, lodepng_error_text(error));

    if (tex->flags & MAKE_BG_FLAG) {
        export_bgheader(tex);
    }

    for (int i = 0; i < tex->width * tex->height * tex->siz / 8; i++) {
        write_byte(tex, image[i]);
    }

    if (tex->flags & MAKE_BG_FLAG) {
        // pad to 0x10
        // write palette

        for (int i = 0; i < 256; i++) {
            uint8_t r, g, b, a;

            r = SCALE_8_5(state.info_png.color.palette[i * 4 + 0]);
            g = SCALE_8_5(state.info_png.color.palette[i * 4 + 1]);
            b = SCALE_8_5(state.info_png.color.palette[i * 4 + 2]);
            a = state.info_png.color.palette[i * 4 + 3] ? 0x1 : 0x0;

            u8 pkt[2] = {0};
            pkt[0] = (r << 3) | (g >> 2);
            pkt[1] = ((g & 0x3) << 6) | (b << 1) | a;

            write_hword(tex, pkt);
        }

    }


    // done saving data
    free(png);
    free(image);
    lodepng_state_cleanup(&state);
}

