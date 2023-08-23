
# libzmbv (encode) options
#ENOPT+=-DZMBV_USE_MINIZ
ENOPT+=-DZMBV_INCLUDE_DECODER

INCLUDE+=-I ./libzmbv
LIBS+=./libzmbv/zmbv.c
LIBS+=./libzmbv/zmbv_avi.c
#LIBS+=./libzmbv/miniz.c
LIBS+=-lz -lm

# libzmbvu (decode) options
#DEOPT+=-DZMBVU_USE_MINIZ

UNPINCLUDE+=-I ./libzmbvu
UNPLIBS+=./libzmbvu/zmbvu.c
#UNPLIBS+=./libzmbvu/miniz.c
UNPLIBS+=-lz -lm

CCOPTS+=-O3 -W -Wall -Wextra

all: test test-avi unpack_small unpack

test: test.c $(LIBS)
	$(CC) $(CCOPTS) $(ENOPT) $(INCLUDE) -o test test.c $(LIBS)

test-avi: test-avi.c $(LIBS)
	$(CC) $(CCOPTS) $(ENOPT) $(INCLUDE) -o test-avi test-avi.c $(LIBS)

unpack_small: unpack_small.c $(UNLIBS)
	$(CC) $(CCOPTS) $(DEOPT) $(UNPINCLUDE) -o unpack_small unpack_small.c $(UNPLIBS)

unpack: unpack.c $(LIBS) $(UNLIBS)
	$(CC) $(CCOPTS) $(ENOPT) $(DEOPT) $(INCLUDE) $(UNPINCLUDE) -o unpack unpack.c $(LIBS) $(UNPLIBS)

clean:
	$(RM) test
	$(RM) test-avi
	$(RM) unpack_small
	$(RM) unpack
