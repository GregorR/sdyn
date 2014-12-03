#include <string.h>

#include "sdyn/value.h"

/* important global values */
SDyn_Undefined sdyn_undefined = NULL;
SDyn_Boolean sdyn_false = NULL, sdyn_true = NULL;
SDyn_Shape sdyn_emptyShape = NULL;

static void pushGlobals()
{
    GGC_PUSH_4(sdyn_undefined, sdyn_false, sdyn_true, sdyn_emptyShape);
    GGC_GLOBALIZE();
    return;
}

/* and our global value initializer */
void sdyn_initValues()
{
    SDyn_Tag tag = NULL;
    SDyn_Number number = NULL;
    SDyn_String string = NULL;
    SDyn_Object object = NULL;

    GGC_PUSH_4(tag, number, string, object);

    /* first push them to the global pointer stack */
    pushGlobals();

    /* now for each type, create a type tag and write that as the user pointer */

    /* undefined */
    tag = GGC_NEW(SDyn_Tag);
    tag->type = SDYN_TYPE_BOXED_UNDEFINED;
    sdyn_undefined = GGC_NEW(SDyn_Undefined);
    GGC_WUP(sdyn_undefined, tag);

    /* boolean */
    tag = GGC_NEW(SDyn_Tag);
    tag->type = SDYN_TYPE_BOXED_BOOL;
    sdyn_false = GGC_NEW(SDyn_Boolean);
    GGC_WUP(sdyn_false, tag);
    sdyn_true = GGC_NEW(SDyn_Boolean);
    sdyn_true->value = 1;

    /* number */
    tag = GGC_NEW(SDyn_Tag);
    tag->type = SDYN_TYPE_BOXED_INT;
    number = GGC_NEW(SDyn_Number);
    GGC_WUP(number, tag);

    /* string */
    tag = GGC_NEW(SDyn_Tag);
    tag->type = SDYN_TYPE_STRING;
    string = GGC_NEW(SDyn_String);
    GGC_WUP(string, tag);

    /* object */
    tag = GGC_NEW(SDyn_Tag);
    tag->type = SDYN_TYPE_OBJECT;
    object = GGC_NEW(SDyn_Object);
    GGC_WUP(object, tag);

    return;
}

/* simple boxer for ints */
SDyn_Number sdyn_boxInt(long value)
{
    SDyn_Number ret = NULL;

    GGC_PUSH_1(ret);

    ret = GGC_NEW(SDyn_Number);
    ret->value = value;

    return ret;
}

/* simple boxer for strings */
SDyn_String sdyn_boxString(char *value, size_t len)
{
    SDyn_String ret = NULL;
    GGC_char_Array arr = NULL;

    GGC_PUSH_2(ret, arr);

    arr = GGC_NEW_DA(char, len);
    strncpy(arr->a, value, len);

    ret = GGC_NEW(SDyn_String);
    GGC_W(ret, value, arr);

    return ret;
}

/* coerce to boolean */
int sdyn_toBoolean(SDyn_Undefined value)
{
    SDyn_Tag tag = NULL;
    SDyn_Boolean boolean = NULL;
    SDyn_Number number = NULL;
    SDyn_String string = NULL;

    GGC_PUSH_5(value, tag, boolean, number, string);

    tag = (SDyn_Tag) GGC_RUP(value);
    switch (tag->type) {
        case SDYN_TYPE_BOXED_BOOL:
            boolean = (SDyn_Boolean) value;
            return boolean->value;

        case SDYN_TYPE_BOXED_UNDEFINED:
            return 0;

        case SDYN_TYPE_BOXED_INT:
            number = (SDyn_Number) value;
            return (number->value) ? 1 : 0;

        case SDYN_TYPE_STRING:
            string = (SDyn_String) value;
            return (GGC_R(string, value)->length) ? 1 : 0;

        default:
            return 1;
    }
}

/* coerce to number */
long sdyn_toNumber(SDyn_Undefined value)
{
    SDyn_Tag tag = NULL;
    SDyn_Number number = NULL;
    SDyn_Boolean boolean = NULL;
    SDyn_String string = NULL;
    GGC_char_Array strRaw = NULL;

    GGC_PUSH_6(value, tag, number, boolean, string, strRaw);

    tag = (SDyn_Tag) GGC_RUP(value);
    switch (tag->type) {
        case SDYN_TYPE_BOXED_INT:
            number = (SDyn_Number) value;
            return number->value;

        case SDYN_TYPE_BOXED_UNDEFINED:
            return 0;

        case SDYN_TYPE_BOXED_BOOL:
            boolean = (SDyn_Boolean) value;
            return boolean->value;

        case SDYN_TYPE_STRING:
        {
            /* we can't use strtol, because it depends on a terminated string */
            size_t i;
            long val = 0;
            strRaw = GGC_R(string, value);
            for (i = 0; i < strRaw->length; i++) {
                char c = strRaw->a[i];
                if (c >= '0' && c <= '9') {
                    val *= 10;
                    val += (c - '0');
                } else break;
            }
            return val;
        }

        default:
            return 0;
    }
}

/* coerce to string */
SDyn_String sdyn_toString(SDyn_Undefined value)
{
    SDyn_Tag tag = NULL;
    SDyn_String ret = NULL;
    GGC_char_Array ca = NULL;
    SDyn_Boolean boolean = NULL;
    SDyn_Number number = NULL;

    GGC_PUSH_6(value, tag, ret, ca, boolean, number);

    tag = (SDyn_Tag) GGC_RUP(value);
    switch (tag->type) {
        case SDYN_TYPE_STRING:
            return (SDyn_String) value;

        case SDYN_TYPE_BOXED_UNDEFINED:
        {
            static const char sundefined[] = "undefined";
            ca = GGC_NEW_DA(char, sizeof(sundefined)-1);
            memcpy(ca->a, sundefined, sizeof(sundefined)-1);
            break;
        }

        case SDYN_TYPE_BOXED_BOOL:
        {
            static const char strue[] = "true";
            static const char sfalse[] = "false";
            boolean = (SDyn_Boolean) value;
            if (boolean->value) {
                ca = GGC_NEW_DA(char, sizeof(strue)-1);
                memcpy(ca->a, strue, sizeof(strue)-1);
            } else {
                ca = GGC_NEW_DA(char, sizeof(sfalse)-1);
                memcpy(ca->a, sfalse, sizeof(sfalse)-1);
            }
            break;
        }

        case SDYN_TYPE_BOXED_INT:
        {
            size_t len;
            long val;
            number = (SDyn_Number) value;

            /* first determine the necessary length */
            val = number->value;
            if (val < 0) len = 1;
            else len = 0;
            if (val == 0) len++;
            else for (; val; len++) val /= 10;

            /* now allocate that length */
            ca = GGC_NEW_DA(char, len);

            /* and convert */
            val = number->value;
            for (len--; len > 0; len--) {
                ca->a[len] = (val % 10) + '0';
                val /= 10;
            }
            if (number->value < 0) ca->a[0] = '-';
            else ca->a[0] = (val % 10) + '0';
            break;
        }

        case SDYN_TYPE_OBJECT:
        {
            static const char sobject[] = "[object Object]";
            ca = GGC_NEW_DA(char, sizeof(sobject)-1);
            memcpy(ca->a, sobject, sizeof(sobject)-1);
            break;
        }

        case SDYN_TYPE_FUNCTION:
        {
            static const char sfunction[] = "[function]";
            ca = GGC_NEW_DA(char, sizeof(sfunction)-1);
            memcpy(ca->a, sfunction, sizeof(sfunction)-1);
            break;
        }

        default:
        {
            static const char serror[] = "[ERROR!]";
            ca = GGC_NEW_DA(char, sizeof(serror)-1);
            memcpy(ca->a, serror, sizeof(serror)-1);
        }
    }

    /* now convert the character array into a string */
    ret = GGC_NEW(SDyn_String);
    GGC_W(ret, value, ca);

    return ret;
}

/* convert to either a string or a number, with preference towards string */
SDyn_Undefined sdyn_toValue(SDyn_Undefined value)
{
    SDyn_Tag tag = NULL;

    GGC_PUSH_2(value, tag);
    tag = (SDyn_Tag) GGC_RUP(value);
    switch (tag->type) {
        case SDYN_TYPE_BOXED_INT:
        case SDYN_TYPE_STRING:
            return value;

        default:
            return (SDyn_Undefined) sdyn_toString(value);
    }
}
