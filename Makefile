default: build/rgb2c

CFLAGS := -I. -g -Wno-unused-parameter -Wall -Wextra -pedantic -frounding-math -fsignaling-nans -fsingle-precision-constant

clean:
	rm -rf build/

build/:
	mkdir -p $@

build/rgb2c.o: rgb2c.c | build/
	$(CC) $(CFLAGS) -c -o $@ $<
build/readtex2.o: readtex2.c | build/
	$(CC) $(CFLAGS) -c -o $@ $<
build/ci_texconv.o: ci_texconv.c | build/
	$(CC) $(CFLAGS) -c -o $@ $<
build/lodepng.o: lodepng.c | build/
	$(CC) $(CFLAGS) -c -o $@ $<
build/loadblock_widthpad.o: loadblock_widthpad.c | build/
	$(CC) $(CFLAGS) -c -o $@ $<

build/rgb2c: build/rgb2c.o build/readtex2.o build/loadblock_widthpad.o build/ci_texconv.o build/lodepng.o
	$(CC) $^ -o $@
