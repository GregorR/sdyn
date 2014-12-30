CC=gcc
ECFLAGS=-g
CFLAGS=-Ih -I../ggggc -I../smalljitasm $(ECFLAGS)
LIBS=../ggggc/libggggc.a ../smalljitasm/libsmalljitasm.a -pthread

OBJS=\
    tokenizer.o \
    parser.o \
    ir.o \
    jit.o \
    intrinsics.o \
    value.o

EXTRAS=\
    test-tokenizer \
    test-parser \
    test-ir \
    test-jit

all: sdyn

extras: sdyn $(EXTRAS)

sdyn: $(OBJS) main.o
	$(CC) $(CFLAGS) $(OBJS) main.o $(LIBS) -o $@

test-%: $(OBJS) %-test.o
	$(CC) $(CFLAGS) $(filter-out $*.o,$(OBJS)) $*-test.o $(LIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%-test.o: %.c
	$(CC) $(CFLAGS) -DUSE_SDYN_`echo "$*" | tr '[a-z]' '[A-Z]'`_TEST -c $*.c -o $*-test.o

clean:
	rm -f sdyn $(EXTRAS) *.o
