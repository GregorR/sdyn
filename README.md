SDyn is a small dynamic language JIT. It is intended to be useful for
eductional audiences, because while it does do all the necessary madness to
qualify as a JIT, it does very little more than that.

The language itself is documented in docs/. The implementation is quite simple:
The tokenizer and parser are in tokenizer.c and parser.c, and are a simple
direct scanner and recursive-descent parser. The internal representation is
SSA, and is documented in ir.c. The JIT itself is implemented in jit-`arch`.c,
e.g. jit-x8664.c, and is documented in those files.

SDyn depends on GGGGC (Gregor's General-purpose Generational Garbage
Collector), in `ggggc`, and SJA (small JIT assembler), included in
`smalljitasm`.
