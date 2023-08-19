
INCLUDE+=-I ./libzmbv
INCLUDE+=-DZMBV_USE_ZLIB
LIBS+=./libzmbv/zmbv.c
LIBS+=./libzmbv/zmbv_avi.c
#LIBS+=./libzmbv/miniz.c
LIBS+=-lz

UNPINCLUDE+=-I ./libzmbvu
UNPINCLUDE+=-DZMBVU_USE_ZLIB
UNPLIBS+=./libzmbvu/zmbvu.c
#UNPLIBS+=./libzmbvu/miniz.c
UNPLIBS+=-lz

all: test unpack_small unpack

test: test.c $(LIBS)
	$(CC) $(INCLUDE) -o test test.c $(LIBS)

unpack_small: unpack_small.c $(UNLIBS)
	$(CC) $(UNPINCLUDE) -o unpack_small unpack_small.c $(UNPLIBS)

unpack: unpack.c $(LIBS) $(UNLIBS)
	$(CC) $(INCLUDE) $(UNPINCLUDE) -o unpack unpack.c $(LIBS) $(UNPLIBS)

clean:
	$(RM) test
	$(RM) unpack_small
	$(RM) unpack
