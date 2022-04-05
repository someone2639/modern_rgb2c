#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "readtex.h"

#include "lodepng.h"


void write_byte(Texture *t, u8 b) {
	u8 ba[] = {0, 0};
	ba[0] = b;
	switch (t->output) {
		case C:
			printf("0x%02X, ", b);
			break;
		case ASCII:
			printf("%.2x ", b);
			break;
		case RAW:
			fwrite(ba, 1, 1, stdout);
			break;
	}
}

void write_hword(Texture *t, u8 *h) {
	switch (t->output) {
		case C:
			printf("0x%02X, 0x%02X, ", h[0], h[1]);
			break;
		case ASCII:
			printf("%.2x %.2x ", h[0], h[1]);
			break;
		case RAW:
			fwrite(h, 1, 2, stdout);
			break;
	}
}

void write_word(Texture *t, u32 w) {
	u32 wa[2] = {0};
	wa[0] = __builtin_bswap32(w);
	switch (t->output) {
		case C:
			printf("0x%02X, 0x%02X, 0x%02X, 0x%02X, ",
				(w >> 24) & 0xFF,
				(w >> 16) & 0xFF,
				(w >> 8 ) & 0xFF,
				(w      ) & 0xFF
			);
			break;
		case ASCII:
			printf("%.2x %.2x %.2x %.2x ",
				(w >> 24) & 0xFF,
				(w >> 16) & 0xFF,
				(w >> 8 ) & 0xFF,
				(w      ) & 0xFF
			);
			break;
		case RAW:
			fwrite(wa, 4, 1, stdout);
			break;
	}
}


void newline(Texture *tex) {
	if (tex->output == C) {
		printf("\n");
	}
}

u8 avg_rgb(rgba *texel) {
	return (texel->red + texel->green + texel->blue) / 3;
}


u8 *read_image(const char* filename, int *w, int *h) {
	unsigned error;
	unsigned char* image = 0;
	unsigned char* png = 0;
	size_t pngsize;
	LodePNGState state;

	lodepng_state_init(&state);
	/*optionally customize the state*/

	error = lodepng_load_file(&png, &pngsize, filename);
	if(!error) error = lodepng_decode(&image, w, h, &state, png, pngsize);
	if(error) printf("error %u: %s\n", error, lodepng_error_text(error));

	free(png);



	lodepng_state_cleanup(&state);
	return image;
}

void export_i(rgba *img, struct texture *t) {
	if (t->output == C) {
		printf("#include <ultra64.h>\n");
		printf("u32 %s[] = {\n", t->name);
	}

	u32 texelcount = t->width * t->height;

	for (u32 i = 0; i < texelcount; i += 2) {
		// Process 2 pixels at a time for 4 bit to be feasible
		// i hope every image has an even number of pixels...
		u8 intensity_0 = avg_rgb(&img[i]);
		u8 intensity_1 = avg_rgb(&img[i + 1]);

		if (t->siz == 4) {
			u8 i4i4 = (SCALE_8_4(intensity_0) << 4)
			          | (SCALE_8_4(intensity_1));
			write_byte(t, i4i4);
		} else if (t->siz == 8) {
			// funny endian memes so we just make a zero terminated u8 buffer here
			u8 i8i8[] = {0, 0, 0};

			i8i8[0] = intensity_0;
			i8i8[1] = intensity_1;
			write_hword(t, i8i8);
		}
	}

	if (t->output == C) {
		printf("};\n");
		printf("Gfx %s_pad[] = {gsSPEndDisplayList(),};\n", t->name);
	}
}

void export_ia(rgba *img, struct texture *t) {
	if (t->output == C) {
		printf("#include <ultra64.h>\n");
		printf("u32 %s[] = {\n", t->name);
	}

	u32 texelcount = t->width * t->height;

	for (u32 i = 0; i < texelcount; i += 2) {
		// Process 2 pixels at a time for 4 bit to be feasible
		// i hope every image has an even number of pixels...
		u8 intensity_0 = avg_rgb(&img[i]);
		u8 alpha_0 = img[i].alpha;
		u8 intensity_1 = avg_rgb(&img[i + 1]);
		u8 alpha_1 = img[i + 1].alpha;

		if (t->siz == 4) {
			u8 ia4ia4 = (SCALE_8_3(intensity_0) << 5 | CLAMP_1(alpha_0) << 4)
			          | (SCALE_8_3(intensity_1) << 1 | CLAMP_1(alpha_1));
			write_byte(t, ia4ia4);
		} else if (t->siz == 8) {
			// funny endian memes so we just make a zero terminated u8 buffer here
			u8 ia8ia8[] = {0, 0, 0};

			#define IA8_2_IA4(val) (SCALE_8_4((val)) & 0xF)

			ia8ia8[0] = (IA8_2_IA4(intensity_0) << 4) | (IA8_2_IA4(alpha_0));
			ia8ia8[1] = (IA8_2_IA4(intensity_1) << 4) | (IA8_2_IA4(alpha_1));
			write_hword(t, ia8ia8);
		} else if (t->siz == 16) {
			// and here
			u8 ia16ia16[] = {0, 0, 0, 0, 0};
			ia16ia16[0] = intensity_0;
			ia16ia16[1] = intensity_1;
			ia16ia16[2] = alpha_0;
			ia16ia16[3] = alpha_1;

			write_word(t, ia16ia16);
		}
	}

	if (t->output == C) {
		printf("};\n");
		printf("Gfx %s_pad[] = {gsSPEndDisplayList(),};\n", t->name);
	}
}

void export_rgba(rgba *img, struct texture *t) {
	if (t->output == C) {
		printf("#include <ultra64.h>\n");
		printf("u32 %s[] = {\n", t->name);
	}

	u32 texelcount = t->width * t->height;

	for (u32 i = 0; i < texelcount; i++) {
		if (t->siz == 16) {
			uint8_t r, g, b, a;
			r = SCALE_8_5(img[i].red);
			g = SCALE_8_5(img[i].green);
			b = SCALE_8_5(img[i].blue);
			a = img[i].alpha ? 0x1 : 0x0;

			u8 pkt[2] = {0};
			pkt[0] = (r << 3) | (g >> 2);
			pkt[1] = ((g & 0x3) << 6) | (b << 1) | a;
			write_hword(t, pkt);
		} else {
			u32 pkt = (img[i].red << 24) | (img[i].green << 16) | (img[i].blue << 8) | img[i].alpha;
			write_word(t, pkt);
		}
	}

	if (t->output == C) {
		printf("};\n");
		printf("Gfx %s_pad[] = {gsSPEndDisplayList(),};\n", t->name);
	}
}

#define G_IM_SIZ_4b	0
#define G_IM_SIZ_8b	1
#define G_IM_SIZ_16b	2
#define G_IM_SIZ_32b	3
#define G_IM_SIZ_DD	5

void export_bgheader(Texture *tex) {
	write_byte(tex, tex->fmt);
	switch(tex->siz) {
		case 4: write_byte(tex, G_IM_SIZ_4b); break;
		case 8: write_byte(tex, G_IM_SIZ_8b); break;
		case 16: write_byte(tex, G_IM_SIZ_16b); break;
		case 32: write_byte(tex, G_IM_SIZ_32b); break;
	}

	if (tex->fmt != CI) write_byte(tex, 0x7C);
	else                write_byte(tex, 0);

	write_byte(tex, 0xFF);

	uint16_t _wd = 0, _ht = 0;
	if (tex->realwidth != 0) _wd = tex->realwidth;
	else                     _wd = tex->width;

	if (tex->realheight != 0) _ht = tex->realheight;
	else                      _ht = tex->height;

	u8 wd[2] = {0, 0};
	wd[0] = _wd >> 16;
	wd[1] = _wd & 0xFF;

	u8 ht[2] = {0, 0};
	ht[0] = _ht >> 16;
	ht[1] = _ht & 0xFF;

	write_hword(tex, wd);
	write_hword(tex, ht);

	write_word(tex, 0x10);
	u32 pal_rom = tex->width * tex->height * tex->siz / 8 + 0x10;
	fprintf(stderr, "pal_rom is (%d %d %08X)\n", tex->realwidth, tex->realheight, pal_rom);
	write_byte(tex, pal_rom >> 24);
	write_byte(tex, pal_rom >> 16);
	write_byte(tex, pal_rom >> 8);
	write_byte(tex, pal_rom & 0xFF);

	newline(tex);
}

void api_tex_convert(char *filename, char *palette_name, Texture *tex) {
	if (tex->fmt == CI) {
		export_ci(tex, filename);
		return;
	}

	rgba *img = read_image(filename, &tex->width, &tex->height);

	if (tex->flags & MAKE_BG_FLAG) {
		export_bgheader(tex);
	}

	switch (tex->fmt) {
		case RGBA:
			export_rgba(img, tex); break;
		case IA:
			export_ia(img, tex); break;
		case I:
			export_i(img, tex); break;
		default:
			printf("Bad format?\n");
			exit(1);
	}
	free(img);
}

int tex_convert (char *filename, struct texture *tex, int fmt, int siz, int makestatic,
	int lr, int lg, int lb, int hr, int hg, int hb, int output, int flags,
	int shuffle_mask,
	// new fields
	char *pallete_name
) {

	int width = 0;
	int height = 0;

	tex->fmt = fmt;
	tex->siz = siz;
	tex->output = output;
	tex->flags = flags;
	tex->shuffle_mask = shuffle_mask;

	if (tex->fmt == CI) {
		export_ci(tex, filename);
		return;
	}

	rgba *img = read_image(filename, &width, &height);

	tex->width = width;
	tex->height = height;

	if (tex->flags & MAKE_BG_FLAG) {
		export_bgheader(tex);
	}

	switch (tex->fmt) {
		case RGBA:
			export_rgba(img, tex); break;
		case IA:
			export_ia(img, tex); break;
		case I:
			export_i(img, tex); break;
		// case A: // same algo as FMT_I
		// 	export_i(img, tex); break;
		default:
			printf("Bad format?\n");
			exit(1);
	}
	free(img);
}

