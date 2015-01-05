/*
 * SDyn: Operations over SDyn values.
 *
 * Copyright (c) 2015 Gregor Richards
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef SDYN_VALUE_H
#define SDYN_VALUE_H 1

#include "ggggc/gc.h"
#include "ggggc/collections/map.h"
#include "ggggc/collections/unit.h"

#include "ir.h"
#include "parser.h"

/* all valid data types */
enum SDyn_DataType {
    SDYN_TYPE_NIL,

    /* unboxed (raw) data */
    SDYN_TYPE_UNDEFINED,
    SDYN_TYPE_BOOL,
    SDYN_TYPE_INT,

    /* boxed data */
    SDYN_TYPE_FIRST_BOXED,
    SDYN_TYPE_BOXED, /* boxed, but what's being boxed is unknown */
    SDYN_TYPE_BOXED_UNDEFINED,
    SDYN_TYPE_BOXED_BOOL,
    SDYN_TYPE_BOXED_INT,
    SDYN_TYPE_STRING,
    SDYN_TYPE_OBJECT,
    SDYN_TYPE_FUNCTION,
    SDYN_TYPE_LAST_BOXED,

    SDYN_TYPE_LAST
};

/* the type tag for boxed data types */
GGC_TYPE(SDyn_Tag)
    GGC_MDATA(int, type);
GGC_END_TYPE(SDyn_Tag, GGC_NO_PTRS);

/* boxed undefined, also used as a supertype of sorts */
GGC_TYPE(SDyn_Undefined)
GGC_END_TYPE(SDyn_Undefined, GGC_NO_PTRS);

/* boxed bool */
GGC_TYPE(SDyn_Boolean)
    GGC_MDATA(unsigned char, value);
GGC_END_TYPE(SDyn_Boolean, GGC_NO_PTRS);

/* boxed int */
GGC_TYPE(SDyn_Number)
    GGC_MDATA(long, value);
GGC_END_TYPE(SDyn_Number, GGC_NO_PTRS);

/* boxed strings */
GGC_TYPE(SDyn_String)
    GGC_MPTR(GGC_char_Array, value);
GGC_END_TYPE(SDyn_String,
    GGC_PTR(SDyn_String, value)
    );

/* object shape */
typedef struct SDyn_ShapeMap__struct *SDyn_ShapeMap_;
typedef struct SDyn_IndexMap__struct *SDyn_IndexMap_;
GGC_TYPE(SDyn_Shape)
    GGC_MDATA(size_t, size);
    GGC_MPTR(SDyn_ShapeMap_, children);
    GGC_MPTR(SDyn_IndexMap_, members);
GGC_END_TYPE(SDyn_Shape,
    GGC_PTR(SDyn_Shape, children)
    GGC_PTR(SDyn_Shape, members)
    );

/* map of strings to object shapes */
GGC_MAP_DECL(SDyn_ShapeMap, SDyn_String, SDyn_Shape);

/* map of strings to indexes (size_ts) */
GGC_UNIT(size_t)
GGC_MAP_DECL(SDyn_IndexMap, SDyn_String, GGC_size_t_Unit);

/* object */
GGC_TYPE(SDyn_Object)
    GGC_MPTR(SDyn_Shape, shape);
    GGC_MPTR(SDyn_UndefinedArray, members);
GGC_END_TYPE(SDyn_Object,
    GGC_PTR(SDyn_Object, shape)
    GGC_PTR(SDyn_Object, members)
    );

/* function (compiled) */
typedef SDyn_Undefined (*sdyn_native_function_t)(void **pstack, size_t argCt, SDyn_Undefined *args);

/* function (data type) */
GGC_TYPE(SDyn_Function)
    GGC_MPTR(SDyn_Node, ast);
    GGC_MPTR(SDyn_IRNodeArray, irValue);
    GGC_MDATA(sdyn_native_function_t, value);
GGC_END_TYPE(SDyn_Function,
    GGC_PTR(SDyn_Function, ast)
    GGC_PTR(SDyn_Function, irValue)
    );

/* important global values */
extern SDyn_Undefined sdyn_undefined;
extern SDyn_Boolean sdyn_false, sdyn_true;
extern SDyn_Shape sdyn_emptyShape;
extern SDyn_Object sdyn_globalObject;

/* our global value initializer */
void sdyn_initValues(void);

/* simple boxer for functions */
SDyn_Function sdyn_boxFunction(SDyn_Node ast);

/* the remaining functions are intended to be called by the JIT */

/* create an object */
SDyn_Object sdyn_newObject(void **pstack);

/* simple boxer for bool */
SDyn_Boolean sdyn_boxBool(void **pstack, int value);

/* simple boxer for ints */
SDyn_Number sdyn_boxInt(void **pstack, long value);

/* simple boxer for strings */
SDyn_String sdyn_boxString(void **pstack, char *value, size_t len);

/* and a specialized boxer for quoted strings */
SDyn_String sdyn_unquote(SDyn_String istr);

/* type coercions */
int sdyn_toBoolean(void **pstack, SDyn_Undefined value);
long sdyn_toNumber(void **pstack, SDyn_Undefined value);
SDyn_String sdyn_toString(void **pstack, SDyn_Undefined value);
SDyn_Undefined sdyn_toValue(void **pstack, SDyn_Undefined value);

/* assertions */
SDyn_Function sdyn_assertFunction(void **pstack, SDyn_Function func);

/* the typeof operation */
SDyn_String sdyn_typeof(void **pstack, SDyn_Undefined value);

/* get the index to which a member belongs in this object, creating one if requested */
size_t sdyn_getObjectMemberIndex(void **pstack, SDyn_Object object, SDyn_String member, int create);

/* get a member of an object, or sdyn_undefined if it does not exist */
SDyn_Undefined sdyn_getObjectMember(void **pstack, SDyn_Object object, SDyn_String member);

/* set or add a member on/to an object */
void sdyn_setObjectMember(void **pstack, SDyn_Object object, SDyn_String member, SDyn_Undefined value);

/* the ever-complicated add function */
SDyn_Undefined sdyn_add(void **pstack, SDyn_Undefined left, SDyn_Undefined right);

/* assert that a function is compiled */
sdyn_native_function_t sdyn_assertCompiled(void **pstack, SDyn_Function func);

/* call a function, with JIT compilation */
SDyn_Undefined sdyn_call(void **pstack, SDyn_Function func, size_t argCt, SDyn_Undefined *args);

#endif
