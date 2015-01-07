/*
 * SDyn: Operations over SDyn values, and important global values. This
 * includes both functions such as sdyn_add, which perform general-purpose
 * operations over values, and internal functionality such as boxing.
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

#define _BSD_SOURCE /* for MAP_ANON */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "sdyn/jit.h"
#include "sdyn/value.h"

/* map functions */
static size_t stringHash(SDyn_String str)
{
    GGC_char_Array arr = NULL;
    size_t i, ret = 0;

    GGC_PUSH_2(str, arr);
    arr = GGC_RP(str, value);

    for (i = 0; i < arr->length; i++)
        ret = ((unsigned char) GGC_RAD(arr, i)) + (ret << 16) - ret;

    return ret;
}

static int stringCmp(SDyn_String strl, SDyn_String strr)
{
    GGC_char_Array arrl = NULL, arrr = NULL;
    size_t lenl, lenr, minlen;
    int ret;

    GGC_PUSH_4(strl, strr, arrl, arrr);
    arrl = GGC_RP(strl, value);
    arrr = GGC_RP(strr, value);
    lenl = arrl->length;
    lenr = arrr->length;
    if (lenl < lenr) minlen = lenl;
    else minlen = lenr;

    /* do the direct comparison */
    ret = memcmp(arrl->a__data, arrr->a__data, minlen);

    /* then adjust for length */
    if (ret == 0) {
        if (lenl < lenr) ret = -1;
        else if (lenl > lenr) ret = 1;
    }

    return ret;
}

/* map definitions */
GGC_MAP_DEFN(SDyn_ShapeMap, SDyn_String, SDyn_Shape, stringHash, stringCmp);
GGC_MAP_DEFN(SDyn_IndexMap, SDyn_String, GGC_size_t_Unit, stringHash, stringCmp);

/* important global values */
SDyn_Undefined sdyn_undefined = NULL;
SDyn_Boolean sdyn_false = NULL, sdyn_true = NULL;
SDyn_Shape sdyn_emptyShape = NULL;
SDyn_Object sdyn_globalObject = NULL;

static void pushGlobals()
{
    GGC_PUSH_5(sdyn_undefined, sdyn_false, sdyn_true, sdyn_emptyShape, sdyn_globalObject);
    GGC_GLOBALIZE();
    return;
}

/* and our global value initializer */
void sdyn_initValues()
{
    SDyn_Tag tag = NULL;
    SDyn_Number number = NULL;
    SDyn_String string = NULL;
    SDyn_ShapeMap esm = NULL;
    SDyn_IndexMap eim = NULL;
    SDyn_UndefinedArray em = NULL;
    SDyn_Function func = NULL;

    GGC_PUSH_7(tag, number, string, esm, eim, em, func);

    /* first push them to the global pointer stack */
    pushGlobals();

    /* now for each type, create a type tag and write that as the user pointer */

    /* undefined */
    tag = GGC_NEW(SDyn_Tag);
    GGC_WD(tag, type, SDYN_TYPE_BOXED_UNDEFINED);
    sdyn_undefined = GGC_NEW(SDyn_Undefined);
    GGC_WUP(sdyn_undefined, tag);

    /* boolean */
    tag = GGC_NEW(SDyn_Tag);
    GGC_WD(tag, type, SDYN_TYPE_BOXED_BOOL);
    sdyn_false = GGC_NEW(SDyn_Boolean);
    GGC_WUP(sdyn_false, tag);
    sdyn_true = GGC_NEW(SDyn_Boolean);
    GGC_WD(sdyn_true, value, 1);

    /* number */
    tag = GGC_NEW(SDyn_Tag);
    GGC_WD(tag, type, SDYN_TYPE_BOXED_INT);
    number = GGC_NEW(SDyn_Number);
    GGC_WUP(number, tag);

    /* string */
    tag = GGC_NEW(SDyn_Tag);
    GGC_WD(tag, type, SDYN_TYPE_STRING);
    string = GGC_NEW(SDyn_String);
    GGC_WUP(string, tag);

    /* the empty shape */
    sdyn_emptyShape = GGC_NEW(SDyn_Shape);
    esm = GGC_NEW(SDyn_ShapeMap);
    eim = GGC_NEW(SDyn_IndexMap);
    GGC_WP(sdyn_emptyShape, children, esm);
    GGC_WP(sdyn_emptyShape, members, eim);

    /* object */
    tag = GGC_NEW(SDyn_Tag);
    GGC_WD(tag, type, SDYN_TYPE_OBJECT);
    sdyn_globalObject = GGC_NEW(SDyn_Object);
    GGC_WUP(sdyn_globalObject, tag);
    GGC_WP(sdyn_globalObject, shape, sdyn_emptyShape);
    em = GGC_NEW_PA(SDyn_Undefined, 0);
    GGC_WP(sdyn_globalObject, members, em);

    /* function */
    tag = GGC_NEW(SDyn_Tag);
    GGC_WD(tag, type, SDYN_TYPE_FUNCTION);
    func = GGC_NEW(SDyn_Function);
    GGC_WUP(func, tag);

    /* so long as we're at it, initialize our pointer stack */
#define POINTER_STACK_SZ 8388608
    ggc_jitPointerStack = ggc_jitPointerStackTop =
        (void **) mmap(NULL, POINTER_STACK_SZ * sizeof(void *), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0) +
        POINTER_STACK_SZ;
#undef POINTER_STACK_SZ

    return;
}

/* simple boxer for functions */
SDyn_Function sdyn_boxFunction(SDyn_Node ast)
{
    SDyn_Function ret = NULL;

    GGC_PUSH_1(ret);

    ret = GGC_NEW(SDyn_Function);
    GGC_WP(ret, ast, ast);

    return ret;
}

#define PSTACK() do { \
    if (pstack) ggc_jitPointerStack = pstack; \
} while(0)

/* create an object */
SDyn_Object sdyn_newObject(void **pstack)
{
    SDyn_Object ret = NULL;
    SDyn_UndefinedArray members = NULL;

    PSTACK();
    GGC_PUSH_2(ret, members);

    ret = GGC_NEW(SDyn_Object);
    members = GGC_NEW_PA(SDyn_Undefined, 0);
    GGC_WP(ret, members, members);
    GGC_WP(ret, shape, sdyn_emptyShape);

    return ret;
}

/* simple boxer for bool */
SDyn_Boolean sdyn_boxBool(void **pstack, int value)
{
    PSTACK();
    if (value)
        return sdyn_true;
    else
        return sdyn_false;
}

/* simple boxer for ints */
SDyn_Number sdyn_boxInt(void **pstack, long value)
{
    SDyn_Number ret = NULL;

    PSTACK();
    GGC_PUSH_1(ret);

    ret = GGC_NEW(SDyn_Number);
    GGC_WD(ret, value, value);

    return ret;
}

/* simple boxer for strings */
SDyn_String sdyn_boxString(void **pstack, char *value, size_t len)
{
    SDyn_String ret = NULL;
    GGC_char_Array arr = NULL;

    PSTACK();
    GGC_PUSH_2(ret, arr);

    arr = GGC_NEW_DA(char, len);
    strncpy(arr->a__data, value, len);

    ret = GGC_NEW(SDyn_String);
    GGC_WP(ret, value, arr);

    return ret;
}

/* and a specialized boxer for quoted strings */
SDyn_String sdyn_unquote(SDyn_String istr)
{
    SDyn_String ret = NULL;
    GGC_char_Array ia = NULL, reta = NULL;
    size_t i, o;

    GGC_PUSH_4(istr, ret, ia, reta);

    ia = GGC_RP(istr, value);
    reta = GGC_NEW_DA(char, ia->length);

    /* just look for escapes */
    for (i = 1, o = 0;
         i < ia->length - 1;
         i++) {
        char co = GGC_RAD(ia, i);
        if (co == '\\') {
            /* an escape */
            co = GGC_RAD(ia, i);
            i++;
            switch (co) {
                case 'n':
                    co = '\n';
                    break;

                case 'r':
                    co = '\r';
                    break;
            }

        }

        GGC_WAD(reta, o, co);
        o++;
    }
    reta->length = o;

    /* then box it up */
    ret = GGC_NEW(SDyn_String);
    GGC_WP(ret, value, reta);

    return ret;
}

/* coerce to boolean */
int sdyn_toBoolean(void **pstack, SDyn_Undefined value)
{
    SDyn_Tag tag = NULL;
    SDyn_Boolean boolean = NULL;
    SDyn_Number number = NULL;
    SDyn_String string = NULL;

    PSTACK();
    GGC_PUSH_5(value, tag, boolean, number, string);

    tag = (SDyn_Tag) GGC_RUP(value);
    switch (GGC_RD(tag, type)) {
        case SDYN_TYPE_BOXED_BOOL:
            boolean = (SDyn_Boolean) value;
            return GGC_RD(boolean, value);

        case SDYN_TYPE_BOXED_UNDEFINED:
            return 0;

        case SDYN_TYPE_BOXED_INT:
            number = (SDyn_Number) value;
            return GGC_RD(number, value) ? 1 : 0;

        case SDYN_TYPE_STRING:
            string = (SDyn_String) value;
            return (GGC_RP(string, value)->length) ? 1 : 0;

        default:
            return 1;
    }
}

/* coerce to number */
long sdyn_toNumber(void **pstack, SDyn_Undefined value)
{
    SDyn_Tag tag = NULL;
    SDyn_Number number = NULL;
    SDyn_Boolean boolean = NULL;
    SDyn_String string = NULL;
    GGC_char_Array strRaw = NULL;

    PSTACK();
    GGC_PUSH_6(value, tag, number, boolean, string, strRaw);

    tag = (SDyn_Tag) GGC_RUP(value);
    switch (GGC_RD(tag, type)) {
        case SDYN_TYPE_BOXED_INT:
            number = (SDyn_Number) value;
            return GGC_RD(number, value);

        case SDYN_TYPE_BOXED_UNDEFINED:
            return 0;

        case SDYN_TYPE_BOXED_BOOL:
            boolean = (SDyn_Boolean) value;
            return GGC_RD(boolean, value);

        case SDYN_TYPE_STRING:
        {
            /* we can't use strtol, because it depends on a terminated string */
            size_t i;
            long val = 0;
            int sign = 1;
            string = (SDyn_String) value;
            strRaw = GGC_RP(string, value);
            i = 0;
            if (GGC_RAD(strRaw, 0) == '-') {
                sign = -1;
                i++;
            } else if (GGC_RAD(strRaw, 0) == '+') i++;
            for (; i < strRaw->length; i++) {
                char c = GGC_RAD(strRaw, i);
                if (c >= '0' && c <= '9') {
                    val *= 10;
                    val += (c - '0');
                } else break;
            }
            return sign * val;
        }

        default:
            return 0;
    }
}

/* coerce to string */
SDyn_String sdyn_toString(void **pstack, SDyn_Undefined value)
{
    SDyn_Tag tag = NULL;
    SDyn_String ret = NULL;
    GGC_char_Array ca = NULL;
    SDyn_Boolean boolean = NULL;
    SDyn_Number number = NULL;

    PSTACK();
    GGC_PUSH_6(value, tag, ret, ca, boolean, number);

    tag = (SDyn_Tag) GGC_RUP(value);
    switch (GGC_RD(tag, type)) {
        case SDYN_TYPE_STRING:
            return (SDyn_String) value;

        case SDYN_TYPE_BOXED_UNDEFINED:
        {
            static const char sundefined[] = "undefined";
            ca = GGC_NEW_DA(char, sizeof(sundefined)-1);
            memcpy(ca->a__data, sundefined, sizeof(sundefined)-1);
            break;
        }

        case SDYN_TYPE_BOXED_BOOL:
        {
            static const char strue[] = "true";
            static const char sfalse[] = "false";
            boolean = (SDyn_Boolean) value;
            if (GGC_RD(boolean, value)) {
                ca = GGC_NEW_DA(char, sizeof(strue)-1);
                memcpy(ca->a__data, strue, sizeof(strue)-1);
            } else {
                ca = GGC_NEW_DA(char, sizeof(sfalse)-1);
                memcpy(ca->a__data, sfalse, sizeof(sfalse)-1);
            }
            break;
        }

        case SDYN_TYPE_BOXED_INT:
        {
            size_t len;
            long val;
            number = (SDyn_Number) value;

            /* first determine the necessary length */
            val = GGC_RD(number, value);
            if (val < 0) len = 1;
            else len = 0;
            if (val == 0) len++;
            else for (; val; len++) val /= 10;

            /* now allocate that length */
            ca = GGC_NEW_DA(char, len);

            /* and convert */
            val = GGC_RD(number, value);
            for (len--; len > 0; len--) {
                char c = (val % 10) + '0';
                GGC_WAD(ca, len, c);
                val /= 10;
            }
            if (GGC_RD(number, value) < 0) {
                char c = '-';
                GGC_WAD(ca, 0, c);
            } else {
                char c = (val % 10) + '0';
                GGC_WAD(ca, 0, c);
            }
            break;
        }

        case SDYN_TYPE_OBJECT:
        {
            static const char sobject[] = "[object Object]";
            ca = GGC_NEW_DA(char, sizeof(sobject)-1);
            memcpy(ca->a__data, sobject, sizeof(sobject)-1);
            break;
        }

        case SDYN_TYPE_FUNCTION:
        {
            static const char sfunction[] = "[function]";
            ca = GGC_NEW_DA(char, sizeof(sfunction)-1);
            memcpy(ca->a__data, sfunction, sizeof(sfunction)-1);
            break;
        }

        default:
        {
            static const char serror[] = "[ERROR!]";
            ca = GGC_NEW_DA(char, sizeof(serror)-1);
            memcpy(ca->a__data, serror, sizeof(serror)-1);
        }
    }

    /* now convert the character array into a string */
    ret = GGC_NEW(SDyn_String);
    GGC_WP(ret, value, ca);

    return ret;
}

/* convert to either a string or a number, with preference towards string */
SDyn_Undefined sdyn_toValue(void **pstack, SDyn_Undefined value)
{
    SDyn_Tag tag = NULL;

    PSTACK();
    GGC_PUSH_2(value, tag);
    tag = (SDyn_Tag) GGC_RUP(value);
    switch (GGC_RD(tag, type)) {
        case SDYN_TYPE_BOXED_INT:
        case SDYN_TYPE_STRING:
            return value;

        default:
            return (SDyn_Undefined) sdyn_toString(NULL, value);
    }
}

/* assertions */
SDyn_Function sdyn_assertFunction(void **pstack, SDyn_Function func)
{
    SDyn_Tag tag = NULL;

    PSTACK();
    GGC_PUSH_2(func, tag);

    tag = (SDyn_Tag) GGC_RUP(func);
    if (GGC_RD(tag, type) != SDYN_TYPE_FUNCTION) {
        fprintf(stderr, "Attempt to call a non-function (type %d)!\n", GGC_RD(tag, type));
        abort();
    }

    return func;
}

/* the typeof operation */
SDyn_String sdyn_typeof(void **pstack, SDyn_Undefined value)
{
    SDyn_Tag tag = NULL;
    GGC_char_Array reta = NULL;
    SDyn_String ret = NULL;

    PSTACK();
    GGC_PUSH_4(value, tag, reta, ret);

    tag = (SDyn_Tag) GGC_RUP(value);

    /* macro to load a string into GGC */
#define LSTR(str) do { \
    reta = GGC_NEW_DA(char, sizeof(str)-1); \
    memcpy(reta->a__data, str, sizeof(str)-1); \
} while(0)

    /* make our string return */
    switch (GGC_RD(tag, type)) {
        case SDYN_TYPE_BOXED_UNDEFINED: LSTR("undefined"); break;
        case SDYN_TYPE_BOXED_BOOL:      LSTR("boolean"); break;
        case SDYN_TYPE_BOXED_INT:       LSTR("number"); break;
        case SDYN_TYPE_STRING:          LSTR("string"); break;
        case SDYN_TYPE_OBJECT:          LSTR("object"); break;
        case SDYN_TYPE_FUNCTION:        LSTR("function"); break;
        default:                        LSTR("???"); break;
    }

    /* and box it */
    ret = GGC_NEW(SDyn_String);
    GGC_WP(ret, value, reta);

    return ret;
}

/* get the index to which a member belongs in this object, creating one if requested */
size_t sdyn_getObjectMemberIndex(void **pstack, SDyn_Object object, SDyn_String member, int create)
{
    SDyn_Shape shape = NULL, cshape = NULL;
    SDyn_ShapeMap shapeChildren = NULL;
    SDyn_IndexMap shapeMembers = NULL;
    SDyn_UndefinedArray oldObjectMembers = NULL, newObjectMembers = NULL;
    GGC_size_t_Unit indexBox = NULL;
    size_t ret;

    PSTACK();
    GGC_PUSH_9(object, member, shape, cshape, shapeChildren, shapeMembers,
        oldObjectMembers, newObjectMembers, indexBox);

    shape = GGC_RP(object, shape);

    /* first check if it already exists */
    shapeMembers = GGC_RP(shape, members);
    if (SDyn_IndexMapGet(shapeMembers, member, &indexBox)) {
        /* got it! */
        ret = GGC_RD(indexBox, v);
        return ret;
    }

    /* nope! Do we stop here? */
    if (!create) return (size_t) -1;

    /* expand the object */
    oldObjectMembers = GGC_RP(object, members);
    ret = oldObjectMembers->length;
    newObjectMembers = GGC_NEW_PA(SDyn_Undefined, ret + 1);
    memcpy(newObjectMembers->a__ptrs, oldObjectMembers->a__ptrs, ret * sizeof(SDyn_Undefined));
    GGC_WAP(newObjectMembers, ret, sdyn_undefined);
    GGC_WP(object, members, newObjectMembers);

    /* check if there's already a defined child with it */
    shapeChildren = GGC_RP(shape, children);
    if (SDyn_ShapeMapGet(shapeChildren, member, &shape)) {
        /* got it! */
        GGC_WP(object, shape, shape);
        return ret;
    }

    /* nope. Make the new shape */
    cshape = GGC_NEW(SDyn_Shape);
    SDyn_ShapeMapPut(shapeChildren, member, cshape);
    ret++;
    GGC_WD(cshape, size, ret);
    ret--;
    shapeChildren = GGC_NEW(SDyn_ShapeMap);
    GGC_WP(cshape, children, shapeChildren);
    shapeMembers = SDyn_IndexMapClone(shapeMembers);
    GGC_WP(cshape, members, shapeMembers);
    indexBox = GGC_NEW(GGC_size_t_Unit);
    GGC_WD(indexBox, v, ret);
    SDyn_IndexMapPut(shapeMembers, member, indexBox);
    GGC_WP(object, shape, cshape);

    return ret;
}

/* get a member of an object, or sdyn_undefined if it does not exist */
SDyn_Undefined sdyn_getObjectMember(void **pstack, SDyn_Object object, SDyn_String member)
{
    SDyn_Undefined ret = NULL;
    SDyn_Tag tag = NULL;
    size_t idx;

    PSTACK();
    GGC_PUSH_4(object, member, ret, tag);

    /* assert that it IS an object */
    tag = (SDyn_Tag) GGC_RUP(object);
    if (GGC_RD(tag, type) != SDYN_TYPE_OBJECT) return sdyn_undefined;

    /* then get the member */
    if ((idx = sdyn_getObjectMemberIndex(NULL, object, member, 0)) != (size_t) -1) {
        ret = GGC_RAP(GGC_RP(object, members), idx);
        return ret;
    } else
        return sdyn_undefined;
}

/* set or add a member on/to an object */
void sdyn_setObjectMember(void **pstack, SDyn_Object object, SDyn_String member, SDyn_Undefined value)
{
    SDyn_UndefinedArray members = NULL;
    SDyn_Tag tag = NULL;
    size_t idx;

    PSTACK();
    GGC_PUSH_4(object, member, members, tag);

    tag = (SDyn_Tag) GGC_RUP(object);
    if (GGC_RD(tag, type) != SDYN_TYPE_OBJECT) return;

    idx = sdyn_getObjectMemberIndex(NULL, object, member, 1);
    members = GGC_RP(object, members);
    GGC_WAP(members, idx, value);

    return;
}

/* the ever-complicated add function */
SDyn_Undefined sdyn_add(void **pstack, SDyn_Undefined left, SDyn_Undefined right)
{
    SDyn_Tag ltag = NULL, rtag = NULL;
    SDyn_Number ln = NULL, rn = NULL;
    SDyn_String ls = NULL, rs = NULL, rets = NULL;
    GGC_char_Array lsa = NULL, rsa = NULL, retsa = NULL;

    PSTACK();
    GGC_PUSH_12(left, right, ltag, rtag, ln, rn, ls, rs, rets, lsa, rsa, retsa);

    ltag = (SDyn_Tag) GGC_RUP(left);
    rtag = (SDyn_Tag) GGC_RUP(right);

    /* only if both are numbers do we add them as numbers */
    if (GGC_RD(ltag, type) == SDYN_TYPE_BOXED_INT && GGC_RD(rtag, type) == SDYN_TYPE_BOXED_INT) {
        long retv;
        ln = (SDyn_Number) left;
        rn = (SDyn_Number) right;
        retv = GGC_RD(ln, value) + GGC_RD(rn, value);
        return (SDyn_Undefined) sdyn_boxInt(NULL, retv);
    }

    /* need to convert to strings */
    ls = sdyn_toString(NULL, left);
    rs = sdyn_toString(NULL, right);
    lsa = GGC_RP(ls, value);
    rsa = GGC_RP(rs, value);

    /* and concatenate them */
    retsa = GGC_NEW_DA(char, lsa->length + rsa->length);
    memcpy(retsa->a__data, lsa->a__data, lsa->length);
    memcpy(retsa->a__data + lsa->length, rsa->a__data, rsa->length);
    rets = GGC_NEW(SDyn_String);
    GGC_WP(rets, value, retsa);

    return (SDyn_Undefined) rets;
}

/* and the even-more-complicated equals function */
int sdyn_equal(void **pstack, SDyn_Undefined left, SDyn_Undefined right)
{
    /* Defined in ES5 as follows (reduced to eliminate types/values/conversions not relevant to SDyn):
     *
     * If Type(x) is the same as Type(y), then
     *
     *     If Type(x) is Undefined, return true.
     *
     *     If Type(x) is Number, then
     *
     *         If x is the same Number value as y, return true.
     *
     *         Return false.
     *
     *     If Type(x) is String, then return true if x and y are exactly the
     *     same sequence of characters (same length and same characters in
     *     corresponding positions). Otherwise, return false.
     *
     *     If Type(x) is Boolean, return true if x and y are both true or both
     *     false. Otherwise, return false.
     *
     *     Return true if x and y refer to the same object. Otherwise, return
     *     false.
     *
     * If Type(x) is Number and Type(y) is String, return the result of the
     * comparison x == ToNumber(y).
     *
     * If Type(x) is String and Type(y) is Number, return the result of the
     * comparison ToNumber(x) == y.
     *
     * If Type(x) is Boolean, return the result of the comparison ToNumber(x) == y.
     *
     * If Type(y) is Boolean, return the result of the comparison x == ToNumber(y).
     *
     * If Type(x) is either String or Number and Type(y) is Object, return the
     * result of the comparison ToString(x) == ToString(y).
     *
     * If Type(x) is Object and Type(y) is either String or Number, return the
     * result of the comparison ToString(x) == ToString(y).
     *
     * Return false.
     */

    SDyn_Tag ltag = NULL, rtag = NULL;
    SDyn_Number lnum = NULL, rnum = NULL;
    SDyn_String lstr = NULL, rstr = NULL;
    GGC_char_Array lstra = NULL, rstra = NULL;
    int ltagv, rtagv;

    PSTACK();
    GGC_PUSH_10(left, right, ltag, rtag, lnum, rnum, lstr, rstr, lstra, rstra);

    ltag = (SDyn_Tag) GGC_RUP(left);
    rtag = (SDyn_Tag) GGC_RUP(right);
    ltagv = GGC_RD(ltag, type);
    rtagv = GGC_RD(rtag, type);
    retry:

    /* first check if they're the same type */
    if (ltagv == rtagv ||
        (ltagv == SDYN_TYPE_OBJECT && rtagv == SDYN_TYPE_FUNCTION) ||
        (ltagv == SDYN_TYPE_FUNCTION && rtagv == SDYN_TYPE_OBJECT)) {
        /* simplish case */
        switch (ltagv) {
            case SDYN_TYPE_BOXED_INT:
                /* compare values */
                lnum = (SDyn_Number) left;
                rnum = (SDyn_Number) right;
                return (GGC_RD(lnum, value) == GGC_RD(rnum, value));

            case SDYN_TYPE_STRING:
            {
                size_t i;

                lstr = (SDyn_String) left;
                rstr = (SDyn_String) right;
                lstra = GGC_RP(lstr, value);
                rstra = GGC_RP(rstr, value);

                /* first off, if they're not the same length, they can't be equal */
                if (lstra->length != rstra->length) return 0;

                /* look for differences */
                for (i = 0; i < lstra->length; i++) {
                    if (GGC_RAD(lstra, i) != GGC_RAD(rstra, i)) return 0;
                }

                /* identical */
                return 1;
            }

            default:
                /* all other cases can be compared directly */
                return (left == right);
        }
    }

    /* not the same type. Is one of them a boolean? */
    if (ltagv == SDYN_TYPE_BOXED_BOOL) {
        left = (SDyn_Undefined) sdyn_boxInt(NULL, sdyn_toNumber(NULL, left));
        ltagv = SDYN_TYPE_BOXED_INT;
        goto retry;
    }
    if (rtagv == SDYN_TYPE_BOXED_BOOL) {
        right = (SDyn_Undefined) sdyn_boxInt(NULL, sdyn_toNumber(NULL, right));
        rtagv = SDYN_TYPE_BOXED_INT;
        goto retry;
    }

    /* is one of them an object or function? */
    if (ltagv == SDYN_TYPE_OBJECT || ltagv == SDYN_TYPE_FUNCTION ||
        rtagv == SDYN_TYPE_OBJECT || rtagv == SDYN_TYPE_FUNCTION) {
        left = (SDyn_Undefined) sdyn_toString(NULL, left);
        ltagv = SDYN_TYPE_STRING;
        right = (SDyn_Undefined) sdyn_toString(NULL, right);
        rtagv = SDYN_TYPE_STRING;
        goto retry;
    }

    /* is it (number,string) or (string,number)? */
    if (ltagv == SDYN_TYPE_BOXED_INT && rtagv == SDYN_TYPE_STRING) {
        right = (SDyn_Undefined) sdyn_boxInt(NULL, sdyn_toNumber(NULL, right));
        rtagv = SDYN_TYPE_BOXED_INT;
        goto retry;
    }
    if (ltagv == SDYN_TYPE_STRING && rtagv == SDYN_TYPE_BOXED_INT) {
        left = (SDyn_Undefined) sdyn_boxInt(NULL, sdyn_toNumber(NULL, left));
        ltagv = SDYN_TYPE_BOXED_INT;
        goto retry;
    }

    /* must not be equal */
    return 0;
}

/* assert that a function is compiled */
sdyn_native_function_t sdyn_assertCompiled(void **pstack, SDyn_Function func)
{
    SDyn_IRNodeArray ir = NULL;
    sdyn_native_function_t nfunc;

    PSTACK();
    GGC_PUSH_2(func, ir);

    /* need to compile? */
    nfunc = GGC_RD(func, value);
    if (!nfunc) {
        /* need to IR-compile? */
        ir = GGC_RP(func, irValue);
        if (!ir) {
            ir = sdyn_irCompile(GGC_RP(func, ast), NULL);
            GGC_WP(func, irValue, ir);
        }

        nfunc = sdyn_compile(ir);
        GGC_WD(func, value, nfunc);
    }

    return nfunc;
}

/* call a function, with JIT compilation */
SDyn_Undefined sdyn_call(void **pstack, SDyn_Function func, size_t argCt, SDyn_Undefined *args)
{
    sdyn_native_function_t nfunc;

    PSTACK();
    GGC_PUSH_1(func);

    nfunc = sdyn_assertCompiled(NULL, func);

    return nfunc(ggc_jitPointerStack, argCt, args);
}
