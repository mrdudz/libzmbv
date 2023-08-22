
INCLUDE+=-I ./libzmbv
INCLUDE+=-DZMBV_USE_ZLIB
#INCLUDE+=-DZMBV_EXCLUDE_DECODER
LIBS+=./libzmbv/zmbv.c
LIBS+=./libzmbv/zmbv_avi.c
#LIBS+=./libzmbv/miniz.c
LIBS+=-lz -lm

UNPINCLUDE+=-I ./libzmbvu
UNPINCLUDE+=-DZMBVU_USE_ZLIB
UNPLIBS+=./libzmbvu/zmbvu.c
#UNPLIBS+=./libzmbvu/miniz.c
UNPLIBS+=-lz -lm

CCOPTS+=-O3

all: test test-avi unpack_small unpack

test: test.c $(LIBS)
	$(CC) $(CCOPTS) $(INCLUDE) -o test test.c $(LIBS)

test-avi: test-avi.c $(LIBS)
	$(CC) $(CCOPTS) $(INCLUDE) -o test-avi test-avi.c $(LIBS)

unpack_small: unpack_small.c $(UNLIBS)
	$(CC) $(CCOPTS) $(UNPINCLUDE) -o unpack_small unpack_small.c $(UNPLIBS)

unpack: unpack.c $(LIBS) $(UNLIBS)
	$(CC) $(CCOPTS) $(INCLUDE) $(UNPINCLUDE) -o unpack unpack.c $(LIBS) $(UNPLIBS)

clean:
	$(RM) test
	$(RM) unpack_small
	$(RM) unpack
