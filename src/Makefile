
INCLUDE=-I ./libzmbv
#LIBS+=./libzmbv/miniz.c
LIBS+=./libzmbv/zmbv.c
LIBS+=./libzmbv/zmbv_avi.c

UNPINCLUDE=-I ./libzmbvu
#UNPLIBS+=./libzmbvu/miniz.c
UNPLIBS+=./libzmbvu/zmbvu.c

all: test unpack_small unpack

test: test.c $(LIBS)
	$(CC) $(INCLUDE) -o test $(LIBS) test.c

unpack_small: unpack_small.c $(UNLIBS)
	$(CC) $(UNPINCLUDE) -o unpack_small unpack_small.c $(UNPLIBS)

unpack: unpack.c $(LIBS) $(UNLIBS)
	$(CC) $(UNPINCLUDE) -o unpack unpack.c $(LIBS) $(UNPLIBS)

clean:
	$(RM) test
	$(RM) unpack_small
	$(RM) unpack
