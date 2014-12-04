CC=gcc
ECFLAGS=-g
CFLAGS=-I../ggggc $(ECFLAGS)
LIBS=../ggggc/libggggc.a -pthread

SRCS=\
    tokenizer.c \
    parser.c \
    ir.c \
    value.c

ALL=\
    test-tokenizer \
    test-parser \
    test-ir

all: $(ALL)

test-tokenizer: $(SRCS)
	$(CC) $(CFLAGS) -DUSE_SDYN_TOKENIZER_TEST $(SRCS) $(LIBS) -o $@

test-parser: $(SRCS)
	$(CC) $(CFLAGS) -DUSE_SDYN_PARSER_TEST $(SRCS) $(LIBS) -o $@

test-ir: $(SRCS)
	$(CC) $(CFLAGS) -DUSE_SDYN_IR_TEST $(SRCS) $(LIBS) -o $@

clean:
	rm -f $(ALL)