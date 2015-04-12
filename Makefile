CC=gcc
ECFLAGS=-g
CFLAGS=-Ih -Iggggc -Ismalljitasm $(ECFLAGS)
LLIBS=ggggc/libggggc.a smalljitasm/libsmalljitasm.a
LIBS=$(LLIBS) -pthread

OBJS=\
    exec.o \
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
	binsearch1 bool1 cmp1 cmp2 cmp3 cmp4 divmul1 eval1 eq1 fib1 fib2 \
	global1 loop1 loop2 loop3 obj1 obj2 obj3 obj4 simple1 simple2 simple3 \
	simple4 sum1 sum2 sum3 this1 typeof1

all: sdyn

extras: sdyn $(EXTRAS)

sdyn: $(OBJS) main.o $(LLIBS)
	$(CC) $(CFLAGS) $(OBJS) main.o $(LIBS) -o $@

test-%: $(OBJS) %-test.o $(LLIBS)
	$(CC) $(CFLAGS) $(filter-out $*.o,$(OBJS)) $*-test.o $(LIBS) -o $@

ggggc/libggggc.a: ggggc/ggggc/gc.h
	cd ggggc ; $(MAKE)

ggggc/ggggc/gc.h:
	cd ggggc-unpatched ; $(MAKE) patch PATCHES=jitpstack

smalljitasm/libsmalljitasm.a:
	cd smalljitasm ; $(MAKE)

test: sdyn
	mkdir -p tests/results
	for i in $(TESTS) ; do \
	    ./sdyn tests/$$i.sdyn > tests/results/$$i || break; \
	    diff -u tests/results/$$i tests/correct/$$i || break; \
	done

%.o: %.c ggggc/ggggc/gc.h
	$(CC) $(CFLAGS) -c $< -o $@

%-test.o: %.c ggggc/ggggc/gc.h
	$(CC) $(CFLAGS) -DUSE_SDYN_`echo "$*" | tr '[a-z]' '[A-Z]'`_TEST -c $*.c -o $*-test.o

clean:
	rm -f sdyn $(EXTRAS) *.o deps
	rm -rf tests/results
	rm -rf ggggc
	cd smalljitasm ; $(MAKE) clean

include deps

deps:
	-$(CC) -Ih -Iggggc -Ismalljitasm -MM *.c > deps
