/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=99 ft=cpp:
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla SpiderMonkey JavaScript code.
 *
 * The Initial Developer of the Original Code is
 * the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Chris Leary <cdleary@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "jsinfer.h"

#include "builtin/RegExp.h"

#include "vm/RegExpObject-inl.h"
#include "vm/RegExpStatics-inl.h"

using namespace js;
using namespace js::types;

/*
 * Return:
 * - The original if no escaping need be performed.
 * - A new string if escaping need be performed.
 * - NULL on error.
 */
static JSString *
EscapeNakedForwardSlashes(JSContext *cx, JSString *unescaped)
{
    size_t oldLen = unescaped->length();
    const jschar *oldChars = unescaped->getChars(cx);
    if (!oldChars)
        return NULL;
    JS::Anchor<JSString *> anchor(unescaped);

    js::Vector<jschar, 128> newChars(cx);
    for (const jschar *it = oldChars; it < oldChars + oldLen; ++it) {
        if (*it == '/' && (it == oldChars || it[-1] != '\\')) {
            if (!newChars.length()) {
                if (!newChars.reserve(oldLen + 1))
                    return NULL;
                newChars.infallibleAppend(oldChars, size_t(it - oldChars));
            }
            if (!newChars.append('\\'))
                return NULL;
        }

        if (!newChars.empty() && !newChars.append(*it))
            return NULL;
    }

    if (newChars.empty())
        return unescaped;

    size_t len = newChars.length();
    if (!newChars.append('\0'))
        return NULL;
    jschar *chars = newChars.extractRawBuffer();
    JSString *escaped = js_NewString(cx, chars, len);
    if (!escaped)
        cx->free_(chars);
    return escaped;
}

static bool
ResetRegExpObjectWithStatics(JSContext *cx, RegExpObject *reobj,
                             JSString *str, RegExpFlag flags = RegExpFlag(0))
{
    flags = RegExpFlag(flags | cx->regExpStatics()->getFlags());
    return ResetRegExpObject(cx, reobj, str, flags);
}

/*
 * Compile a new |RegExpPrivate| for the |RegExpObject|.
 *
 * Per ECMAv5 15.10.4.1, we act on combinations of (pattern, flags) as
 * arguments:
 *
 *  RegExp, undefined => flags := pattern.flags
 *  RegExp, _ => throw TypeError
 *  _ => pattern := ToString(pattern) if defined(pattern) else ''
 *       flags := ToString(flags) if defined(flags) else ''
 */
static bool
CompileRegExpObject(JSContext *cx, RegExpObject *obj, uintN argc, Value *argv, Value *rval)
{
    if (argc == 0) {
        if (!ResetRegExpObjectWithStatics(cx, obj, cx->runtime->emptyString))
            return false;
        *rval = ObjectValue(*obj);
        return true;
    }

    Value sourceValue = argv[0];
    if (ValueIsRegExp(sourceValue)) {
        /*
         * If we get passed in a |RegExpObject| source we return a new
         * object with the same |RegExpPrivate|.
         *
         * Note: the regexp static flags are not taken into consideration here.
         */
        JSObject &sourceObj = sourceValue.toObject();
        if (argc >= 2 && !argv[1].isUndefined()) {
            JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_NEWREGEXP_FLAGGED);
            return false;
        }

        RegExpPrivate *rep = sourceObj.asRegExp()->getPrivate();
        if (!rep)
            return false;

        rep->incref(cx);
        if (!ResetRegExpObject(cx, obj, AlreadyIncRefed<RegExpPrivate>(rep)))
            return false;
        *rval = ObjectValue(*obj);
        return true;
    }

    JSString *sourceStr;
    if (sourceValue.isUndefined()) {
        sourceStr = cx->runtime->emptyString;
    } else {
        /* Coerce to string and compile. */
        sourceStr = js_ValueToString(cx, sourceValue);
        if (!sourceStr)
            return false;
    }

    RegExpFlag flags = RegExpFlag(0);
    if (argc > 1 && !argv[1].isUndefined()) {
        JSString *flagStr = js_ValueToString(cx, argv[1]);
        if (!flagStr)
            return false;
        argv[1].setString(flagStr);
        if (!ParseRegExpFlags(cx, flagStr, &flags))
            return false;
    }

    JSString *escapedSourceStr = EscapeNakedForwardSlashes(cx, sourceStr);
    if (!escapedSourceStr)
        return false;

    if (!ResetRegExpObjectWithStatics(cx, obj, escapedSourceStr, flags))
        return false;
    *rval = ObjectValue(*obj);
    return true;
}

static JSBool
regexp_compile(JSContext *cx, uintN argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    bool ok;
    JSObject *obj = NonGenericMethodGuard(cx, args, regexp_compile, &RegExpClass, &ok);
    if (!obj)
        return ok;

    RegExpObject *reobj = obj->asRegExp();
    ok = CompileRegExpObject(cx, reobj, args.length(), args.array(), &args.rval());
    JS_ASSERT_IF(ok, reobj->getPrivate());
    return ok;
}

static JSBool
regexp_construct(JSContext *cx, uintN argc, Value *vp)
{
    Value *argv = JS_ARGV(cx, vp);

    if (!IsConstructing(vp)) {
        /*
         * If first arg is regexp and no flags are given, just return the arg.
         * Otherwise, delegate to the standard constructor.
         * See ECMAv5 15.10.3.1.
         */
        if (argc >= 1 && ValueIsRegExp(argv[0]) && (argc == 1 || argv[1].isUndefined())) {
            *vp = argv[0];
            return true;
        }
    }

    JSObject *obj = NewBuiltinClassInstance(cx, &RegExpClass);
    if (!obj)
        return false;

    PreInitRegExpObject pireo(obj);
    RegExpObject *reobj = pireo.get();

    if (!CompileRegExpObject(cx, reobj, argc, argv, &JS_RVAL(cx, vp))) {
        pireo.fail();
        return false;
    }

    pireo.succeed();
    *vp = ObjectValue(*reobj);
    return true;
}

static JSBool
regexp_toString(JSContext *cx, uintN argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    bool ok;
    JSObject *obj = NonGenericMethodGuard(cx, args, regexp_toString, &RegExpClass, &ok);
    if (!obj)
        return ok;

    JSString *str = obj->asRegExp()->toString(cx);
    if (!str)
        return false;

    *vp = StringValue(str);
    return true;
}

static JSFunctionSpec regexp_methods[] = {
#if JS_HAS_TOSOURCE
    JS_FN(js_toSource_str,  regexp_toString,    0,0),
#endif
    JS_FN(js_toString_str,  regexp_toString,    0,0),
    JS_FN("compile",        regexp_compile,     2,0),
    JS_FN("exec",           regexp_exec,        1,0),
    JS_FN("test",           regexp_test,        1,0),
    JS_FS_END
};

/*
 * RegExp static properties.
 *
 * RegExp class static properties and their Perl counterparts:
 *
 *  RegExp.input                $_
 *  RegExp.multiline            $*
 *  RegExp.lastMatch            $&
 *  RegExp.lastParen            $+
 *  RegExp.leftContext          $`
 *  RegExp.rightContext         $'
 */

#define DEFINE_STATIC_GETTER(name, code)                                        \
    static JSBool                                                               \
    name(JSContext *cx, JSObject *obj, jsid id, jsval *vp)                      \
    {                                                                           \
        RegExpStatics *res = cx->regExpStatics();                               \
        code;                                                                   \
    }

DEFINE_STATIC_GETTER(static_input_getter,        return res->createPendingInput(cx, vp))
DEFINE_STATIC_GETTER(static_multiline_getter,    *vp = BOOLEAN_TO_JSVAL(res->multiline());
                                                 return true)
DEFINE_STATIC_GETTER(static_lastMatch_getter,    return res->createLastMatch(cx, vp))
DEFINE_STATIC_GETTER(static_lastParen_getter,    return res->createLastParen(cx, vp))
DEFINE_STATIC_GETTER(static_leftContext_getter,  return res->createLeftContext(cx, vp))
DEFINE_STATIC_GETTER(static_rightContext_getter, return res->createRightContext(cx, vp))

DEFINE_STATIC_GETTER(static_paren1_getter,       return res->createParen(cx, 1, vp))
DEFINE_STATIC_GETTER(static_paren2_getter,       return res->createParen(cx, 2, vp))
DEFINE_STATIC_GETTER(static_paren3_getter,       return res->createParen(cx, 3, vp))
DEFINE_STATIC_GETTER(static_paren4_getter,       return res->createParen(cx, 4, vp))
DEFINE_STATIC_GETTER(static_paren5_getter,       return res->createParen(cx, 5, vp))
DEFINE_STATIC_GETTER(static_paren6_getter,       return res->createParen(cx, 6, vp))
DEFINE_STATIC_GETTER(static_paren7_getter,       return res->createParen(cx, 7, vp))
DEFINE_STATIC_GETTER(static_paren8_getter,       return res->createParen(cx, 8, vp))
DEFINE_STATIC_GETTER(static_paren9_getter,       return res->createParen(cx, 9, vp))

#define DEFINE_STATIC_SETTER(name, code)                                        \
    static JSBool                                                               \
    name(JSContext *cx, JSObject *obj, jsid id, JSBool strict, jsval *vp)       \
    {                                                                           \
        RegExpStatics *res = cx->regExpStatics();                               \
        code;                                                                   \
        return true;                                                            \
    }

DEFINE_STATIC_SETTER(static_input_setter,
                     if (!JSVAL_IS_STRING(*vp) && !JS_ConvertValue(cx, *vp, JSTYPE_STRING, vp))
                         return false;
                     res->setPendingInput(JSVAL_TO_STRING(*vp)))
DEFINE_STATIC_SETTER(static_multiline_setter,
                     if (!JSVAL_IS_BOOLEAN(*vp) && !JS_ConvertValue(cx, *vp, JSTYPE_BOOLEAN, vp))
                         return false;
                     res->setMultiline(cx, !!JSVAL_TO_BOOLEAN(*vp)))

const uint8 REGEXP_STATIC_PROP_ATTRS    = JSPROP_PERMANENT | JSPROP_SHARED | JSPROP_ENUMERATE;
const uint8 RO_REGEXP_STATIC_PROP_ATTRS = REGEXP_STATIC_PROP_ATTRS | JSPROP_READONLY;

const uint8 HIDDEN_PROP_ATTRS = JSPROP_PERMANENT | JSPROP_SHARED;
const uint8 RO_HIDDEN_PROP_ATTRS = HIDDEN_PROP_ATTRS | JSPROP_READONLY;

static JSPropertySpec regexp_static_props[] = {
    {"input",        0, REGEXP_STATIC_PROP_ATTRS,    static_input_getter, static_input_setter},
    {"multiline",    0, REGEXP_STATIC_PROP_ATTRS,    static_multiline_getter,
                                                     static_multiline_setter},
    {"lastMatch",    0, RO_REGEXP_STATIC_PROP_ATTRS, static_lastMatch_getter,    NULL},
    {"lastParen",    0, RO_REGEXP_STATIC_PROP_ATTRS, static_lastParen_getter,    NULL},
    {"leftContext",  0, RO_REGEXP_STATIC_PROP_ATTRS, static_leftContext_getter,  NULL},
    {"rightContext", 0, RO_REGEXP_STATIC_PROP_ATTRS, static_rightContext_getter, NULL},
    {"$1",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren1_getter,       NULL},
    {"$2",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren2_getter,       NULL},
    {"$3",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren3_getter,       NULL},
    {"$4",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren4_getter,       NULL},
    {"$5",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren5_getter,       NULL},
    {"$6",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren6_getter,       NULL},
    {"$7",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren7_getter,       NULL},
    {"$8",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren8_getter,       NULL},
    {"$9",           0, RO_REGEXP_STATIC_PROP_ATTRS, static_paren9_getter,       NULL},

    {"$_",           0, HIDDEN_PROP_ATTRS,    static_input_getter, static_input_setter},
    {"$*",           0, HIDDEN_PROP_ATTRS,    static_multiline_getter, static_multiline_setter},
    {"$&",           0, RO_HIDDEN_PROP_ATTRS, static_lastMatch_getter, NULL},
    {"$+",           0, RO_HIDDEN_PROP_ATTRS, static_lastParen_getter, NULL},
    {"$`",           0, RO_HIDDEN_PROP_ATTRS, static_leftContext_getter, NULL},
    {"$'",           0, RO_HIDDEN_PROP_ATTRS, static_rightContext_getter, NULL},
    {0,0,0,0,0}
};

JSObject *
js_InitRegExpClass(JSContext *cx, JSObject *obj)
{
    JS_ASSERT(obj->isNative());

    GlobalObject *global = obj->asGlobal();

    JSObject *proto = global->createBlankPrototype(cx, &RegExpClass);
    if (!proto)
        return NULL;

    {
        AlreadyIncRefed<RegExpPrivate> rep =
          RegExpPrivate::create(cx, cx->runtime->emptyString, RegExpFlag(0), NULL);
        if (!rep)
            return NULL;

        /*
         * Associate the empty regular expression with |RegExp.prototype|, and define
         * the initial non-method properties of any regular expression instance.
         * These must be added before methods to preserve slot layout.
         */
#ifdef DEBUG
        assertSameCompartment(cx, proto, rep->compartment);
#endif

        PreInitRegExpObject pireo(proto);
        RegExpObject *reproto = pireo.get();
        if (!ResetRegExpObject(cx, reproto, rep)) {
            pireo.fail();
            return NULL;
        }

        pireo.succeed();
    }

    if (!DefinePropertiesAndBrand(cx, proto, NULL, regexp_methods))
        return NULL;

    JSFunction *ctor = global->createConstructor(cx, regexp_construct, &RegExpClass,
                                                 CLASS_ATOM(cx, RegExp), 2);
    if (!ctor)
        return NULL;

    if (!LinkConstructorAndPrototype(cx, ctor, proto))
        return NULL;

    /* Add static properties to the RegExp constructor. */
    if (!JS_DefineProperties(cx, ctor, regexp_static_props))
        return NULL;

    /* Capture normal data properties pregenerated for RegExp objects. */
    TypeObject *type = proto->getNewType(cx);
    if (!type)
        return NULL;
    AddTypeProperty(cx, type, "source", Type::StringType());
    AddTypeProperty(cx, type, "global", Type::BooleanType());
    AddTypeProperty(cx, type, "ignoreCase", Type::BooleanType());
    AddTypeProperty(cx, type, "multiline", Type::BooleanType());
    AddTypeProperty(cx, type, "sticky", Type::BooleanType());
    AddTypeProperty(cx, type, "lastIndex", Type::Int32Type());

    if (!DefineConstructorAndPrototype(cx, global, JSProto_RegExp, ctor, proto))
        return NULL;

    return proto;
}

/*
 * ES5 15.10.6.2 (and 15.10.6.3, which calls 15.10.6.2).
 *
 * RegExp.prototype.test doesn't need to create a results array, and we use
 * |execType| to perform this optimization.
 */
static JSBool
ExecuteRegExp(JSContext *cx, Native native, uintN argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    /* Step 1. */
    bool ok;
    JSObject *obj = NonGenericMethodGuard(cx, args, native, &RegExpClass, &ok);
    if (!obj)
        return ok;

    RegExpObject *reobj = obj->asRegExp();
    RegExpPrivate *re = reobj->getPrivate();
    if (!re)
        return true;

    /*
     * Code execution under this call could swap out the guts of |reobj|, so we
     * have to take a defensive refcount here.
     */
    AutoRefCount<RegExpPrivate> arc(cx, NeedsIncRef<RegExpPrivate>(re));
    RegExpStatics *res = cx->regExpStatics();

    /* Step 2. */
    JSString *input = js_ValueToString(cx, (args.length() > 0) ? args[0] : UndefinedValue());
    if (!input)
        return false;

    /* Step 3. */
    size_t length = input->length();

    /* Step 4. */
    const Value &lastIndex = reobj->getLastIndex();

    /* Step 5. */
    jsdouble i;
    if (!ToInteger(cx, lastIndex, &i))
        return false;

    /* Steps 6-7 (with sticky extension). */
    if (!re->global() && !re->sticky())
        i = 0;

    /* Step 9a. */
    if (i < 0 || i > length) {
        reobj->zeroLastIndex();
        args.rval() = NullValue();
        return true;
    }

    /* Steps 8-21. */
    size_t lastIndexInt(i);
    if (!re->execute(cx, res, input, &lastIndexInt, native == regexp_test, &args.rval()))
        return false;

    /* Step 11 (with sticky extension). */
    if (re->global() || (!args.rval().isNull() && re->sticky())) {
        if (args.rval().isNull())
            reobj->zeroLastIndex();
        else
            reobj->setLastIndex(lastIndexInt);
    }

    return true;
}

/* ES5 15.10.6.2. */
JSBool
js::regexp_exec(JSContext *cx, uintN argc, Value *vp)
{
    return ExecuteRegExp(cx, regexp_exec, argc, vp);
}

/* ES5 15.10.6.3. */
JSBool
js::regexp_test(JSContext *cx, uintN argc, Value *vp)
{
    if (!ExecuteRegExp(cx, regexp_test, argc, vp))
        return false;
    if (!vp->isTrue())
        vp->setBoolean(false);
    return true;
}
