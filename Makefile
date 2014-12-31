CC=gcc
ECFLAGS=-g
CFLAGS=-Ih -Iggggc -Ismalljitasm $(ECFLAGS)
LIBS=ggggc/libggggc.a smalljitasm/libsmalljitasm.a -pthread

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

TESTS=\
	bool1 cmp1 cmp2 obj1 obj2 obj3 obj4 simple1 simple2 simple3 simple4 \
	sum1 sum2 sum3

all: sdyn

extras: sdyn $(EXTRAS)

sdyn: $(OBJS) main.o $(LIBS)
	$(CC) $(CFLAGS) $(OBJS) main.o $(LIBS) -o $@

test-%: $(OBJS) %-test.o $(LIBS)
	$(CC) $(CFLAGS) $(filter-out $*.o,$(OBJS)) $*-test.o $(LIBS) -o $@

ggggc/libggggc.a:
	cd ggggc ; $(MAKE)

smalljitasm/libsmalljitasm.a:
	cd smalljitasm ; $(MAKE)

-pthread:

test: sdyn
	mkdir -p tests/results
	for i in $(TESTS) ; do \
	    ./sdyn < tests/$$i.sdyn > tests/results/$$i || break; \
	    diff tests/results/$$i tests/correct/$$i || break; \
	done

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%-test.o: %.c
	$(CC) $(CFLAGS) -DUSE_SDYN_`echo "$*" | tr '[a-z]' '[A-Z]'`_TEST -c $*.c -o $*-test.o

clean:
	rm -f sdyn $(EXTRAS) *.o deps
	rm -rf tests/results
	cd ggggc ; $(MAKE) clean
	cd smalljitasm ; $(MAKE) clean

include deps

deps:
	-$(CC) -Ih -Iggggc -Ismalljitasm -MM *.c > deps
