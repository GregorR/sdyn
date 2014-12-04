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
typedef SDyn_Undefined (*sdyn_native_function_t)(struct GGGGC_PointerStack *pstack, size_t argCt, SDyn_Undefined *args);

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

/* our global value initializer */
void sdyn_initValues(void);

/* simple boxer for ints */
SDyn_Number sdyn_boxInt(long value);

/* simple boxer for strings */
SDyn_String sdyn_boxString(char *value, size_t len);

/* type coercions */
int sdyn_toBoolean(SDyn_Undefined value);
long sdyn_toNumber(SDyn_Undefined value);
SDyn_String sdyn_toString(SDyn_Undefined value);
SDyn_Undefined sdyn_toValue(SDyn_Undefined value);

/* functions for our maps */
int sdyn_shapeMapPut(SDyn_ShapeMap map, SDyn_String key, SDyn_Shape value);

#endif
