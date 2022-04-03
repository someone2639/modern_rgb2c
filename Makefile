default: build/rgb2c

CFLAGS := -I. -g

clean:
	rm -rf build/

build/:
	mkdir -p $@

build/rgb2c.o: rgb2c.c | build/
	$(CC) $(CFLAGS) -c -o $@ $<
build/readtex2.o: readtex2.c | build/
	$(CC) $(CFLAGS) -c -o $@ $<

build/rgb2c: build/rgb2c.o build/readtex2.o
	$(CC) $^ -o $@
