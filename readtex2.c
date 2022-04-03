#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "readtex.h"
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_TGA
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION

void write_byte(Texture *t, u8 b) {
	u8 ba[] = {0, 0};
	ba[0] = b;
	switch (t->output) {
		case C:
			printf("0x%02X, ", b);
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
		case RAW:
			fwrite(wa, 4, 1, stdout);
			break;
	}
}


void newline(int output) {
	if (output == C) {
		printf("\n");
	}
}



rgba *read_image(char *filename, int *w, int *h) {
	rgba *img;
	int channels = 0;

	stbi_uc *data = stbi_load(filename, w, h, &channels, STBI_default);
	u32 img_size = *w * *h * sizeof(*img);
	img = malloc(img_size);
	if (!img) {
		printf("error %s %d\n", __FILE__, __LINE__);
		exit(1);
	}

	switch (channels) {
		case 3: // red, green, blue
		case 4: // red, green, blue, alpha
			for (int j = 0; j < *h; j++) {
				for (int i = 0; i < *w; i++) {
					int idx = j * *w + i;
					img[idx].red   = data[channels*idx];
					img[idx].green = data[channels*idx + 1];
					img[idx].blue  = data[channels*idx + 2];
					if (channels == 4) {
						img[idx].alpha = data[channels*idx + 3];
					} else {
						img[idx].alpha = 0xFF;
					}
				}
			}
			break;
		case 2: // grey, alpha
			for (int j = 0; j < *h; j++) {
				for (int i = 0; i < *w; i++) {
					int idx = j * *w + i;
					img[idx].red   = data[2*idx];
					img[idx].green = data[2*idx];
					img[idx].blue  = data[2*idx];
					img[idx].alpha = data[2*idx + 1];
				}
			}
			break;
		default:
			fprintf(stderr, "Don't know how to read channels: %d\n", channels);
			free(img);
			img = NULL;
	}

	// cleanup
	stbi_image_free(data);

	return img;
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
		u8 intensity_0 = img[i].red;
		u8 alpha_0 = img[i].alpha;
		u8 intensity_1 = img[i + 1].red;
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

int tex_convert (char *filename, struct texture *tex, int fmt, int siz, int makestatic,
	int lr, int lg, int lb, int hr, int hg, int hb, int output, int flags,
	int shuffle_mask) {

	int width = 0;
	int height = 0;

	tex->fmt = fmt;
	tex->siz = siz;
	tex->output = output;

	rgba *img = read_image(filename, &width, &height);

	tex->width = width;
	tex->height = height;

	switch (tex->fmt) {
		case RGBA:
			export_rgba(img, tex); break;
		case IA:
			export_ia(img, tex); break;
		default:
			printf("Bad format?\n");
			exit(1);
		// case IA:
		// 	export_ia(tex);
	}

}

