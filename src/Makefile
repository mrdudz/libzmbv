
# libzmbv (encode) options
#ENOPT+=-DZMBV_USE_MINIZ
ENOPT+=-DZMBV_INCLUDE_DECODER

INCLUDE+=-I ./libzmbv
LIBS+=./libzmbv/zmbv.c
LIBS+=./libzmbv/zmbv_avi.c

# libzmbvu (decode) options
#DEOPT+=-DZMBVU_USE_MINIZ

UNPINCLUDE+=-I ./libzmbvu
UNPLIBS+=./libzmbvu/zmbvu.c

CCOPTS+=-O0
CCOPTS+=-W -Wall -Wextra
#this might make a difference on windows?
#CCOPTS+=-mno-ms-bitfields
LINK+=-lm
LINK+=-lz


all: test test-avi unpack_small unpack

test: test.c $(LIBS)
	$(CC) $(CCOPTS) $(ENOPT) $(INCLUDE) -o test test.c $(LIBS) $(LINK)

test-avi: test-avi.c $(LIBS)
	$(CC) $(CCOPTS) $(ENOPT) $(INCLUDE) -o test-avi test-avi.c $(LIBS) $(LINK)

unpack_small: unpack_small.c $(UNPLIBS)
	$(CC) $(CCOPTS) $(DEOPT) $(UNPINCLUDE) -o unpack_small unpack_small.c $(UNPLIBS) $(LINK)

unpack: unpack.c $(LIBS) $(UNPLIBS)
	$(CC) $(CCOPTS) $(ENOPT) $(DEOPT) $(INCLUDE) $(UNPINCLUDE) -o unpack unpack.c $(LIBS) $(UNPLIBS) $(LINK)

clean:
	$(RM) test
	$(RM) test-avi
	$(RM) unpack_small
	$(RM) unpack
