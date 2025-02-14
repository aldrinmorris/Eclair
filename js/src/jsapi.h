/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=78:
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
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#ifndef jsapi_h___
#define jsapi_h___
/*
 * JavaScript API.
 */
#include <stddef.h>
#include <stdio.h>
#include "js-config.h"
#include "jspubtd.h"
#include "jsval.h"

#include "js/Utility.h"

/************************************************************************/

/* JS::Value can store a full int32. */
#define JSVAL_INT_BITS          32
#define JSVAL_INT_MIN           ((jsint)0x80000000)
#define JSVAL_INT_MAX           ((jsint)0x7fffffff)

/************************************************************************/

#ifdef __cplusplus
namespace JS {

/*
 * Protecting non-jsval, non-JSObject *, non-JSString * values from collection
 *
 * Most of the time, the garbage collector's conservative stack scanner works
 * behind the scenes, finding all live values and protecting them from being
 * collected. However, when JSAPI client code obtains a pointer to data the
 * scanner does not know about, owned by an object the scanner does know about,
 * Care Must Be Taken.
 *
 * The scanner recognizes only a select set of types: pointers to JSObjects and
 * similar things (JSFunctions, and so on), pointers to JSStrings, and jsvals.
 * So while the scanner finds all live |JSString| pointers, it does not notice
 * |jschar| pointers.
 *
 * So suppose we have:
 *
 *   void f(JSString *str) {
 *     const jschar *ch = JS_GetStringCharsZ(str);
 *     ... do stuff with ch, but no uses of str ...;
 *   }
 *
 * After the call to |JS_GetStringCharsZ|, there are no further uses of
 * |str|, which means that the compiler is within its rights to not store
 * it anywhere. But because the stack scanner will not notice |ch|, there
 * is no longer any live value in this frame that would keep the string
 * alive. If |str| is the last reference to that |JSString|, and the
 * collector runs while we are using |ch|, the string's array of |jschar|s
 * may be freed out from under us.
 *
 * Note that there is only an issue when 1) we extract a thing X the scanner
 * doesn't recognize from 2) a thing Y the scanner does recognize, and 3) if Y
 * gets garbage-collected, then X gets freed. If we have code like this:
 *
 *   void g(JSObject *obj) {
 *     jsval x;
 *     JS_GetProperty(obj, "x", &x);
 *     ... do stuff with x ...
 *   }
 *
 * there's no problem, because the value we've extracted, x, is a jsval, a
 * type that the conservative scanner recognizes.
 *
 * Conservative GC frees us from the obligation to explicitly root the types it
 * knows about, but when we work with derived values like |ch|, we must root
 * their owners, as the derived value alone won't keep them alive.
 *
 * A JS::Anchor is a kind of GC root that allows us to keep the owners of
 * derived values like |ch| alive throughout the Anchor's lifetime. We could
 * fix the above code as follows:
 *
 *   void f(JSString *str) {
 *     JS::Anchor<JSString *> a_str(str);
 *     const jschar *ch = JS_GetStringCharsZ(str);
 *     ... do stuff with ch, but no uses of str ...;
 *   }
 *
 * This simply ensures that |str| will be live until |a_str| goes out of scope.
 * As long as we don't retain a pointer to the string's characters for longer
 * than that, we have avoided all garbage collection hazards.
 */
template<typename T> class AnchorPermitted;
template<> class AnchorPermitted<JSObject *> { };
template<> class AnchorPermitted<const JSObject *> { };
template<> class AnchorPermitted<JSFunction *> { };
template<> class AnchorPermitted<const JSFunction *> { };
template<> class AnchorPermitted<JSString *> { };
template<> class AnchorPermitted<const JSString *> { };
template<> class AnchorPermitted<Value> { };

template<typename T>
class Anchor: AnchorPermitted<T>
{
  public:
    Anchor() { }
    explicit Anchor(T t) { hold = t; }
    inline ~Anchor();
    T &get() { return hold; }
    const T &get() const { return hold; }
    void set(const T &t) { hold = t; }
    void clear() { hold = 0; }
  private:
    T hold;
    /* Anchors should not be assigned or passed to functions. */
    Anchor(const Anchor &);
    const Anchor &operator=(const Anchor &);
};

#ifdef __GNUC__
template<typename T>
inline Anchor<T>::~Anchor()
{
    /*
     * No code is generated for this. But because this is marked 'volatile', G++ will
     * assume it has important side-effects, and won't delete it. (G++ never looks at
     * the actual text and notices it's empty.) And because we have passed |hold| to
     * it, GCC will keep |hold| alive until this point.
     *
     * The "memory" clobber operand ensures that G++ will not move prior memory
     * accesses after the asm --- it's a barrier. Unfortunately, it also means that
     * G++ will assume that all memory has changed after the asm, as it would for a
     * call to an unknown function. I don't know of a way to avoid that consequence.
     */
    asm volatile("":: "g" (hold) : "memory");
}
#else
template<typename T>
inline Anchor<T>::~Anchor()
{
    /*
     * An adequate portable substitute, for non-structure types.
     *
     * The compiler promises that, by the end of an expression statement, the
     * last-stored value to a volatile object is the same as it would be in an
     * unoptimized, direct implementation (the "abstract machine" whose behavior the
     * language spec describes). However, the compiler is still free to reorder
     * non-volatile accesses across this store --- which is what we must prevent. So
     * assigning the held value to a volatile variable, as we do here, is not enough.
     *
     * In our case, however, garbage collection only occurs at function calls, so it
     * is sufficient to ensure that the destructor's store isn't moved earlier across
     * any function calls that could collect. It is hard to imagine the compiler
     * analyzing the program so thoroughly that it could prove that such motion was
     * safe. In practice, compilers treat calls to the collector as opaque operations
     * --- in particular, as operations which could access volatile variables, across
     * which this destructor must not be moved.
     *
     * ("Objection, your honor!  *Alleged* killer whale!")
     *
     * The disadvantage of this approach is that it does generate code for the store.
     * We do need to use Anchors in some cases where cycles are tight.
     *
     * NB: there is a Anchor<Value>::~Anchor() specialization below.
     */
    volatile T sink;
    sink = hold;
}
#endif  /* defined(__GNUC__) */

/*
 * JS::Value is the C++ interface for a single JavaScript Engine value.
 * A few general notes on JS::Value:
 *
 * - JS::Value has setX() and isX() members for X in
 *
 *     { Int32, Double, String, Boolean, Undefined, Null, Object, Magic }
 *
 *   JS::Value also contains toX() for each of the non-singleton types.
 *
 * - Magic is a singleton type whose payload contains a JSWhyMagic "reason" for
 *   the magic value. By providing JSWhyMagic values when creating and checking
 *   for magic values, it is possible to assert, at runtime, that only magic
 *   values with the expected reason flow through a particular value. For
 *   example, if cx->exception has a magic value, the reason must be
 *   JS_GENERATOR_CLOSING.
 *
 * - A key difference between jsval and JS::Value is that JS::Value gives null
 *   a separate type. Thus
 *
 *           JSVAL_IS_OBJECT(v) === v.isObjectOrNull()
 *       !JSVAL_IS_PRIMITIVE(v) === v.isObject()
 *
 *   To help prevent mistakenly boxing a nullable JSObject* as an object,
 *   Value::setObject takes a JSObject&. (Conversely, Value::asObject returns a
 *   JSObject&. A convenience member Value::setObjectOrNull is provided.
 *
 * - JSVAL_VOID is the same as the singleton value of the Undefined type.
 *
 * - Note that JS::Value is 8 bytes on 32 and 64-bit architectures. Thus, on
 *   32-bit user code should avoid copying jsval/JS::Value as much as possible,
 *   preferring to pass by const Value &.
 */
class Value
{
  public:
    /*
     * N.B. the default constructor leaves Value unitialized. Adding a default
     * constructor prevents Value from being stored in a union.
     */

    /*** Mutators ***/

    JS_ALWAYS_INLINE
    void setNull() {
        data.asBits = BUILD_JSVAL(JSVAL_TAG_NULL, 0).asBits;
    }

    JS_ALWAYS_INLINE
    void setUndefined() {
        data.asBits = BUILD_JSVAL(JSVAL_TAG_UNDEFINED, 0).asBits;
    }

    JS_ALWAYS_INLINE
    void setInt32(int32 i) {
        data = INT32_TO_JSVAL_IMPL(i);
    }

    JS_ALWAYS_INLINE
    int32 &getInt32Ref() {
        JS_ASSERT(isInt32());
        return data.s.payload.i32;
    }

    JS_ALWAYS_INLINE
    void setDouble(double d) {
        data = DOUBLE_TO_JSVAL_IMPL(d);
    }

    JS_ALWAYS_INLINE
    double &getDoubleRef() {
        JS_ASSERT(isDouble());
        return data.asDouble;
    }

    JS_ALWAYS_INLINE
    void setString(JSString *str) {
        data = STRING_TO_JSVAL_IMPL(str);
    }

    JS_ALWAYS_INLINE
    void setString(const JS::Anchor<JSString *> &str) {
        setString(str.get());
    }

    JS_ALWAYS_INLINE
    void setObject(JSObject &obj) {
        data = OBJECT_TO_JSVAL_IMPL(&obj);
    }

    JS_ALWAYS_INLINE
    void setObject(const JS::Anchor<JSObject *> &obj) {
        setObject(*obj.get());
    }

    JS_ALWAYS_INLINE
    void setBoolean(bool b) {
        data = BOOLEAN_TO_JSVAL_IMPL(b);
    }

    JS_ALWAYS_INLINE
    void setMagic(JSWhyMagic why) {
        data = MAGIC_TO_JSVAL_IMPL(why);
    }

    JS_ALWAYS_INLINE
    void setMagicWithObjectOrNullPayload(JSObject *obj) {
        data = MAGIC_OBJ_TO_JSVAL_IMPL(obj);
    }

    JS_ALWAYS_INLINE
    JSObject *getMagicObjectOrNullPayload() const {
        return MAGIC_JSVAL_TO_OBJECT_OR_NULL_IMPL(data);
    }

    JS_ALWAYS_INLINE
    bool setNumber(uint32 ui) {
        if (ui > JSVAL_INT_MAX) {
            setDouble((double)ui);
            return false;
        } else {
            setInt32((int32)ui);
            return true;
        }
    }

    JS_ALWAYS_INLINE
    bool setNumber(double d) {
        int32 i;
        if (JSDOUBLE_IS_INT32(d, &i)) {
            setInt32(i);
            return true;
        } else {
            setDouble(d);
            return false;
        }
    }

    JS_ALWAYS_INLINE
    void setObjectOrNull(JSObject *arg) {
        if (arg)
            setObject(*arg);
        else
            setNull();
    }

    JS_ALWAYS_INLINE
    void setObjectOrUndefined(JSObject *arg) {
        if (arg)
            setObject(*arg);
        else
            setUndefined();
    }

    JS_ALWAYS_INLINE
    void swap(Value &rhs) {
        uint64 tmp = rhs.data.asBits;
        rhs.data.asBits = data.asBits;
        data.asBits = tmp;
    }

    /*** Value type queries ***/

    JS_ALWAYS_INLINE
    bool isUndefined() const {
        return JSVAL_IS_UNDEFINED_IMPL(data);
    }

    JS_ALWAYS_INLINE
    bool isNull() const {
        return JSVAL_IS_NULL_IMPL(data);
    }

    JS_ALWAYS_INLINE
    bool isNullOrUndefined() const {
        return isNull() || isUndefined();
    }

    JS_ALWAYS_INLINE
    bool isInt32() const {
        return JSVAL_IS_INT32_IMPL(data);
    }

    JS_ALWAYS_INLINE
    bool isInt32(int32 i32) const {
        return JSVAL_IS_SPECIFIC_INT32_IMPL(data, i32);
    }

    JS_ALWAYS_INLINE
    bool isDouble() const {
        return JSVAL_IS_DOUBLE_IMPL(data);
    }

    JS_ALWAYS_INLINE
    bool isNumber() const {
        return JSVAL_IS_NUMBER_IMPL(data);
    }

    JS_ALWAYS_INLINE
    bool isString() const {
        return JSVAL_IS_STRING_IMPL(data);
    }

    JS_ALWAYS_INLINE
    bool isObject() const {
        return JSVAL_IS_OBJECT_IMPL(data);
    }

    JS_ALWAYS_INLINE
    bool isPrimitive() const {
        return JSVAL_IS_PRIMITIVE_IMPL(data);
    }

    JS_ALWAYS_INLINE
    bool isObjectOrNull() const {
        return JSVAL_IS_OBJECT_OR_NULL_IMPL(data);
    }

    JS_ALWAYS_INLINE
    bool isGCThing() const {
        return JSVAL_IS_GCTHING_IMPL(data);
    }

    JS_ALWAYS_INLINE
    bool isBoolean() const {
        return JSVAL_IS_BOOLEAN_IMPL(data);
    }

    JS_ALWAYS_INLINE
    bool isTrue() const {
        return JSVAL_IS_SPECIFIC_BOOLEAN(data, true);
    }

    JS_ALWAYS_INLINE
    bool isFalse() const {
        return JSVAL_IS_SPECIFIC_BOOLEAN(data, false);
    }

    JS_ALWAYS_INLINE
    bool isMagic() const {
        return JSVAL_IS_MAGIC_IMPL(data);
    }

    JS_ALWAYS_INLINE
    bool isMagic(JSWhyMagic why) const {
        JS_ASSERT_IF(isMagic(), data.s.payload.why == why);
        return JSVAL_IS_MAGIC_IMPL(data);
    }

    JS_ALWAYS_INLINE
    bool isMagicCheck(JSWhyMagic why) const {
        return isMagic() && data.s.payload.why == why;
    }

#if JS_BITS_PER_WORD == 64
    JS_ALWAYS_INLINE
    bool hasPtrPayload() const {
        return data.asBits >= JSVAL_LOWER_INCL_SHIFTED_TAG_OF_PTR_PAYLOAD_SET;
    }
#endif

    JS_ALWAYS_INLINE
    bool isMarkable() const {
        return JSVAL_IS_TRACEABLE_IMPL(data);
    }

    JS_ALWAYS_INLINE
    JSGCTraceKind gcKind() const {
        JS_ASSERT(isMarkable());
        return JSGCTraceKind(JSVAL_TRACE_KIND_IMPL(data));
    }

    JS_ALWAYS_INLINE
    JSWhyMagic whyMagic() const {
        JS_ASSERT(isMagic());
        return data.s.payload.why;
    }

    /*** Comparison ***/

    JS_ALWAYS_INLINE
    bool operator==(const Value &rhs) const {
        return data.asBits == rhs.data.asBits;
    }

    JS_ALWAYS_INLINE
    bool operator!=(const Value &rhs) const {
        return data.asBits != rhs.data.asBits;
    }

    friend inline bool SameType(const Value &lhs, const Value &rhs);

    /*** Extract the value's typed payload ***/

    JS_ALWAYS_INLINE
    int32 toInt32() const {
        JS_ASSERT(isInt32());
        return JSVAL_TO_INT32_IMPL(data);
    }

    JS_ALWAYS_INLINE
    double toDouble() const {
        JS_ASSERT(isDouble());
        return data.asDouble;
    }

    JS_ALWAYS_INLINE
    double toNumber() const {
        JS_ASSERT(isNumber());
        return isDouble() ? toDouble() : double(toInt32());
    }

    JS_ALWAYS_INLINE
    JSString *toString() const {
        JS_ASSERT(isString());
        return JSVAL_TO_STRING_IMPL(data);
    }

    JS_ALWAYS_INLINE
    JSObject &toObject() const {
        JS_ASSERT(isObject());
        return *JSVAL_TO_OBJECT_IMPL(data);
    }

    JS_ALWAYS_INLINE
    JSObject *toObjectOrNull() const {
        JS_ASSERT(isObjectOrNull());
        return JSVAL_TO_OBJECT_IMPL(data);
    }

    JS_ALWAYS_INLINE
    void *toGCThing() const {
        JS_ASSERT(isGCThing());
        return JSVAL_TO_GCTHING_IMPL(data);
    }

    JS_ALWAYS_INLINE
    bool toBoolean() const {
        JS_ASSERT(isBoolean());
        return JSVAL_TO_BOOLEAN_IMPL(data);
    }

    JS_ALWAYS_INLINE
    uint32 payloadAsRawUint32() const {
        JS_ASSERT(!isDouble());
        return data.s.payload.u32;
    }

    JS_ALWAYS_INLINE
    uint64 asRawBits() const {
        return data.asBits;
    }

    JS_ALWAYS_INLINE
    void setRawBits(uint64 bits) {
        data.asBits = bits;
    }

    /*
     * In the extract/box/unbox functions below, "NonDouble" means this
     * functions must not be called on a value that is a double. This allows
     * these operations to be implemented more efficiently, since doubles
     * generally already require special handling by the caller.
     */
    JS_ALWAYS_INLINE
    JSValueType extractNonDoubleType() const {
        return JSVAL_EXTRACT_NON_DOUBLE_TYPE_IMPL(data);
    }

    JS_ALWAYS_INLINE
    JSValueTag extractNonDoubleTag() const {
        return JSVAL_EXTRACT_NON_DOUBLE_TAG_IMPL(data);
    }

    JS_ALWAYS_INLINE
    void unboxNonDoubleTo(uint64 *out) const {
        UNBOX_NON_DOUBLE_JSVAL(data, out);
    }

    JS_ALWAYS_INLINE
    void boxNonDoubleFrom(JSValueType type, uint64 *out) {
        data = BOX_NON_DOUBLE_JSVAL(type, out);
    }

    /*
     * The trace-jit specializes JSVAL_TYPE_OBJECT into JSVAL_TYPE_FUNOBJ and
     * JSVAL_TYPE_NONFUNOBJ. Since these two operations just return the type of
     * a value, the caller must handle JSVAL_TYPE_OBJECT separately.
     */
    JS_ALWAYS_INLINE
    JSValueType extractNonDoubleObjectTraceType() const {
        JS_ASSERT(!isObject());
        return JSVAL_EXTRACT_NON_DOUBLE_TYPE_IMPL(data);
    }

    JS_ALWAYS_INLINE
    JSValueTag extractNonDoubleObjectTraceTag() const {
        JS_ASSERT(!isObject());
        return JSVAL_EXTRACT_NON_DOUBLE_TAG_IMPL(data);
    }

    /*
     * Private API
     *
     * Private setters/getters allow the caller to read/write arbitrary types
     * that fit in the 64-bit payload. It is the caller's responsibility, after
     * storing to a value with setPrivateX to read only using getPrivateX.
     * Privates values are given a type type which ensures they are not marked.
     */

    JS_ALWAYS_INLINE
    void setPrivate(void *ptr) {
        data = PRIVATE_PTR_TO_JSVAL_IMPL(ptr);
    }

    JS_ALWAYS_INLINE
    void *toPrivate() const {
        JS_ASSERT(JSVAL_IS_DOUBLE_IMPL(data));
        return JSVAL_TO_PRIVATE_PTR_IMPL(data);
    }

    JS_ALWAYS_INLINE
    void setPrivateUint32(uint32 ui) {
        data = PRIVATE_UINT32_TO_JSVAL_IMPL(ui);
    }

    JS_ALWAYS_INLINE
    uint32 toPrivateUint32() const {
        JS_ASSERT(JSVAL_IS_DOUBLE_IMPL(data));
        return JSVAL_TO_PRIVATE_UINT32_IMPL(data);
    }

    JS_ALWAYS_INLINE
    uint32 &getPrivateUint32Ref() {
        JS_ASSERT(isDouble());
        return data.s.payload.u32;
    }

    /*
     * An unmarked value is just a void* cast as a Value. Thus, the Value is
     * not safe for GC and must not be marked. This API avoids raw casts
     * and the ensuing strict-aliasing warnings.
     */

    JS_ALWAYS_INLINE
    void setUnmarkedPtr(void *ptr) {
        data.asPtr = ptr;
    }

    JS_ALWAYS_INLINE
    void *toUnmarkedPtr() const {
        return data.asPtr;
    }

    const size_t *payloadWord() const {
#if JS_BITS_PER_WORD == 32
        return &data.s.payload.word;
#elif JS_BITS_PER_WORD == 64
        return &data.asWord;
#endif
    }

#ifndef _MSC_VER
  /* To make jsval binary compatible when linking across C and C++ with MSVC,
   * JS::Value needs to be POD. Otherwise, jsval will be passed in memory
   * in C++ but by value in C (bug 645111).
   */
  private:
#endif

    jsval_layout data;

  private:
    void staticAssertions() {
        JS_STATIC_ASSERT(sizeof(JSValueType) == 1);
        JS_STATIC_ASSERT(sizeof(JSValueTag) == 4);
        JS_STATIC_ASSERT(sizeof(JSBool) == 4);
        JS_STATIC_ASSERT(sizeof(JSWhyMagic) <= 4);
        JS_STATIC_ASSERT(sizeof(Value) == 8);
    }

    friend jsval_layout (::JSVAL_TO_IMPL)(Value);
    friend Value (::IMPL_TO_JSVAL)(jsval_layout l);
};

/************************************************************************/

static JS_ALWAYS_INLINE Value
NullValue()
{
    Value v;
    v.setNull();
    return v;
}

static JS_ALWAYS_INLINE Value
UndefinedValue()
{
    Value v;
    v.setUndefined();
    return v;
}

static JS_ALWAYS_INLINE Value
Int32Value(int32 i32)
{
    Value v;
    v.setInt32(i32);
    return v;
}

static JS_ALWAYS_INLINE Value
DoubleValue(double dbl)
{
    Value v;
    v.setDouble(dbl);
    return v;
}

static JS_ALWAYS_INLINE Value
StringValue(JSString *str)
{
    Value v;
    v.setString(str);
    return v;
}

static JS_ALWAYS_INLINE Value
BooleanValue(bool boo)
{
    Value v;
    v.setBoolean(boo);
    return v;
}

static JS_ALWAYS_INLINE Value
ObjectValue(JSObject &obj)
{
    Value v;
    v.setObject(obj);
    return v;
}

static JS_ALWAYS_INLINE Value
MagicValue(JSWhyMagic why)
{
    Value v;
    v.setMagic(why);
    return v;
}

static JS_ALWAYS_INLINE Value
NumberValue(double dbl)
{
    Value v;
    v.setNumber(dbl);
    return v;
}

static JS_ALWAYS_INLINE Value
ObjectOrNullValue(JSObject *obj)
{
    Value v;
    v.setObjectOrNull(obj);
    return v;
}

static JS_ALWAYS_INLINE Value
PrivateValue(void *ptr)
{
    Value v;
    v.setPrivate(ptr);
    return v;
}

static JS_ALWAYS_INLINE Value
PrivateUint32Value(uint32 ui)
{
    Value v;
    v.setPrivateUint32(ui);
    return v;
}

JS_ALWAYS_INLINE bool
SameType(const Value &lhs, const Value &rhs)
{
    return JSVAL_SAME_TYPE_IMPL(lhs.data, rhs.data);
}

/************************************************************************/

#ifndef __GNUC__

/*
 * The default assignment operator for |struct C| has the signature:
 *
 *   C& C::operator=(const C&)
 *
 * And in particular requires implicit conversion of |this| to type |C| for the
 * return value. But |volatile C| cannot thus be converted to |C|, so just
 * doing |sink = hold| as in the non-specialized version would fail to compile.
 * Do the assignment on asBits instead, since I don't think we want to give
 * jsval_layout an assignment operator returning |volatile jsval_layout|.
 */
template<>
inline Anchor<Value>::~Anchor()
{
    volatile uint64 bits;
    bits = hold.asRawBits();
}

#endif

}  /* namespace JS */

/************************************************************************/

/*
 * JS::Value and jsval are the same type; jsval is the old name, kept around
 * for backwards compatibility along with all the JSVAL_* operations below.
 * jsval_layout is an implementation detail and should not be used externally.
 */
typedef JS::Value jsval;

static JS_ALWAYS_INLINE jsval_layout
JSVAL_TO_IMPL(jsval v)
{
    return v.data;
}

static JS_ALWAYS_INLINE jsval
IMPL_TO_JSVAL(jsval_layout l)
{
    JS::Value v;
    v.data = l;
    return v;
}

#ifdef DEBUG
struct JSValueAlignmentTester { char c; JS::Value v; };
JS_STATIC_ASSERT(sizeof(JSValueAlignmentTester) == 16);
#endif /* DEBUG */

#else  /* defined(__cplusplus) */

/*
 * For SpiderMonkey C clients, there is no JS::Value class, only the
 * traditional jsval with the traditional JSVAL_* operations. Since
 * SpiderMonkey itself is always compiled as C++, this relies on the binary
 * compatibility of jsval_layout and JS::Value (statically asserted below).
 */
typedef union jsval_layout jsval;

static JS_ALWAYS_INLINE jsval_layout
JSVAL_TO_IMPL(jsval v)
{
    return v;
}

static JS_ALWAYS_INLINE jsval
IMPL_TO_JSVAL(jsval_layout l)
{
    return l;
}

#endif  /* defined(__cplusplus) */

#ifdef DEBUG
typedef struct { char c; jsval_layout l; } JSLayoutAlignmentTester;
JS_STATIC_ASSERT(sizeof(JSLayoutAlignmentTester) == 16);
#endif /* DEBUG */

JS_STATIC_ASSERT(sizeof(jsval_layout) == sizeof(jsval));

/************************************************************************/

/* JSClass operation signatures. */

/*
 * Add, delete, or get a property named by id in obj.  Note the jsid id
 * type -- id may be a string (Unicode property identifier) or an int (element
 * index).  The *vp out parameter, on success, is the new property value after
 * an add or get.  After a successful delete, *vp is JSVAL_FALSE iff
 * obj[id] can't be deleted (because it's permanent).
 */
typedef JSBool
(* JSPropertyOp)(JSContext *cx, JSObject *obj, jsid id, jsval *vp);

/*
 * Set a property named by id in obj, treating the assignment as strict
 * mode code if strict is true. Note the jsid id type -- id may be a string
 * (Unicode property identifier) or an int (element index). The *vp out
 * parameter, on success, is the new property value after the
 * set.
 */
typedef JSBool
(* JSStrictPropertyOp)(JSContext *cx, JSObject *obj, jsid id, JSBool strict, jsval *vp);

/*
 * This function type is used for callbacks that enumerate the properties of
 * a JSObject.  The behavior depends on the value of enum_op:
 *
 *  JSENUMERATE_INIT
 *    A new, opaque iterator state should be allocated and stored in *statep.
 *    (You can use PRIVATE_TO_JSVAL() to tag the pointer to be stored).
 *
 *    The number of properties that will be enumerated should be returned as
 *    an integer jsval in *idp, if idp is non-null, and provided the number of
 *    enumerable properties is known.  If idp is non-null and the number of
 *    enumerable properties can't be computed in advance, *idp should be set
 *    to JSVAL_ZERO.
 *
 *  JSENUMERATE_INIT_ALL
 *    Used identically to JSENUMERATE_INIT, but exposes all properties of the
 *    object regardless of enumerability.
 *
 *  JSENUMERATE_NEXT
 *    A previously allocated opaque iterator state is passed in via statep.
 *    Return the next jsid in the iteration using *idp.  The opaque iterator
 *    state pointed at by statep is destroyed and *statep is set to JSVAL_NULL
 *    if there are no properties left to enumerate.
 *
 *  JSENUMERATE_DESTROY
 *    Destroy the opaque iterator state previously allocated in *statep by a
 *    call to this function when enum_op was JSENUMERATE_INIT or
 *    JSENUMERATE_INIT_ALL.
 *
 * The return value is used to indicate success, with a value of JS_FALSE
 * indicating failure.
 */
typedef JSBool
(* JSNewEnumerateOp)(JSContext *cx, JSObject *obj, JSIterateOp enum_op,
                     jsval *statep, jsid *idp);

/*
 * The old-style JSClass.enumerate op should define all lazy properties not
 * yet reflected in obj.
 */
typedef JSBool
(* JSEnumerateOp)(JSContext *cx, JSObject *obj);

/*
 * Resolve a lazy property named by id in obj by defining it directly in obj.
 * Lazy properties are those reflected from some peer native property space
 * (e.g., the DOM attributes for a given node reflected as obj) on demand.
 *
 * JS looks for a property in an object, and if not found, tries to resolve
 * the given id.  If resolve succeeds, the engine looks again in case resolve
 * defined obj[id].  If no such property exists directly in obj, the process
 * is repeated with obj's prototype, etc.
 *
 * NB: JSNewResolveOp provides a cheaper way to resolve lazy properties.
 */
typedef JSBool
(* JSResolveOp)(JSContext *cx, JSObject *obj, jsid id);

/*
 * Like JSResolveOp, but flags provide contextual information as follows:
 *
 *  JSRESOLVE_QUALIFIED   a qualified property id: obj.id or obj[id], not id
 *  JSRESOLVE_ASSIGNING   obj[id] is on the left-hand side of an assignment
 *  JSRESOLVE_DETECTING   'if (o.p)...' or similar detection opcode sequence
 *  JSRESOLVE_DECLARING   var, const, or function prolog declaration opcode
 *  JSRESOLVE_CLASSNAME   class name used when constructing
 *
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 *
 * This hook instead of JSResolveOp is called via the JSClass.resolve member
 * if JSCLASS_NEW_RESOLVE is set in JSClass.flags.
 *
 * Setting JSCLASS_NEW_RESOLVE and JSCLASS_NEW_RESOLVE_GETS_START further
 * extends this hook by passing in the starting object on the prototype chain
 * via *objp.  Thus a resolve hook implementation may define the property id
 * being resolved in the object in which the id was first sought, rather than
 * in a prototype object whose class led to the resolve hook being called.
 *
 * When using JSCLASS_NEW_RESOLVE_GETS_START, the resolve hook must therefore
 * null *objp to signify "not resolved".  With only JSCLASS_NEW_RESOLVE and no
 * JSCLASS_NEW_RESOLVE_GETS_START, the hook can assume *objp is null on entry.
 * This is not good practice, but enough existing hook implementations count
 * on it that we can't break compatibility by passing the starting object in
 * *objp without a new JSClass flag.
 */
typedef JSBool
(* JSNewResolveOp)(JSContext *cx, JSObject *obj, jsid id, uintN flags,
                   JSObject **objp);

/*
 * Convert obj to the given type, returning true with the resulting value in
 * *vp on success, and returning false on error or exception.
 */
typedef JSBool
(* JSConvertOp)(JSContext *cx, JSObject *obj, JSType type, jsval *vp);

/*
 * Delegate typeof to an object so it can cloak a primitive or another object.
 */
typedef JSType
(* JSTypeOfOp)(JSContext *cx, JSObject *obj);

/*
 * Finalize obj, which the garbage collector has determined to be unreachable
 * from other live objects or from GC roots.  Obviously, finalizers must never
 * store a reference to obj.
 */
typedef void
(* JSFinalizeOp)(JSContext *cx, JSObject *obj);

/*
 * Used by JS_AddExternalStringFinalizer and JS_RemoveExternalStringFinalizer
 * to extend and reduce the set of string types finalized by the GC.
 */
typedef void
(* JSStringFinalizeOp)(JSContext *cx, JSString *str);

/*
 * JSClass.checkAccess type: check whether obj[id] may be accessed per mode,
 * returning false on error/exception, true on success with obj[id]'s last-got
 * value in *vp, and its attributes in *attrsp.  As for JSPropertyOp above, id
 * is either a string or an int jsval.
 */
typedef JSBool
(* JSCheckAccessOp)(JSContext *cx, JSObject *obj, jsid id, JSAccessMode mode,
                    jsval *vp);

/*
 * Encode or decode an object, given an XDR state record representing external
 * data.  See jsxdrapi.h.
 */
typedef JSBool
(* JSXDRObjectOp)(JSXDRState *xdr, JSObject **objp);

/*
 * Check whether v is an instance of obj.  Return false on error or exception,
 * true on success with JS_TRUE in *bp if v is an instance of obj, JS_FALSE in
 * *bp otherwise.
 */
typedef JSBool
(* JSHasInstanceOp)(JSContext *cx, JSObject *obj, const jsval *v, JSBool *bp);

/*
 * Function type for trace operation of the class called to enumerate all
 * traceable things reachable from obj's private data structure. For each such
 * thing, a trace implementation must call
 *
 *    JS_CallTracer(trc, thing, kind);
 *
 * or one of its convenience macros as described in jsapi.h.
 *
 * JSTraceOp implementation can assume that no other threads mutates object
 * state. It must not change state of the object or corresponding native
 * structures. The only exception for this rule is the case when the embedding
 * needs a tight integration with GC. In that case the embedding can check if
 * the traversal is a part of the marking phase through calling
 * JS_IsGCMarkingTracer and apply a special code like emptying caches or
 * marking its native structures.
 */
typedef void
(* JSTraceOp)(JSTracer *trc, JSObject *obj);

/*
 * DEBUG only callback that JSTraceOp implementation can provide to return
 * a string describing the reference traced with JS_CallTracer.
 */
typedef void
(* JSTraceNamePrinter)(JSTracer *trc, char *buf, size_t bufsize);

typedef JSBool
(* JSEqualityOp)(JSContext *cx, JSObject *obj, const jsval *v, JSBool *bp);

/*
 * Typedef for native functions called by the JS VM.
 *
 * See jsapi.h, the JS_CALLEE, JS_THIS, etc. macros.
 */

typedef JSBool
(* JSNative)(JSContext *cx, uintN argc, jsval *vp);

/* Callbacks and their arguments. */

typedef enum JSContextOp {
    JSCONTEXT_NEW,
    JSCONTEXT_DESTROY
} JSContextOp;

/*
 * The possible values for contextOp when the runtime calls the callback are:
 *   JSCONTEXT_NEW      JS_NewContext successfully created a new JSContext
 *                      instance. The callback can initialize the instance as
 *                      required. If the callback returns false, the instance
 *                      will be destroyed and JS_NewContext returns null. In
 *                      this case the callback is not called again.
 *   JSCONTEXT_DESTROY  One of JS_DestroyContext* methods is called. The
 *                      callback may perform its own cleanup and must always
 *                      return true.
 *   Any other value    For future compatibility the callback must do nothing
 *                      and return true in this case.
 */
typedef JSBool
(* JSContextCallback)(JSContext *cx, uintN contextOp);

typedef enum JSGCStatus {
    JSGC_BEGIN,
    JSGC_END,
    JSGC_MARK_END,
    JSGC_FINALIZE_END
} JSGCStatus;

typedef JSBool
(* JSGCCallback)(JSContext *cx, JSGCStatus status, JSBool isCompartment);

/*
 * Generic trace operation that calls JS_CallTracer on each traceable thing
 * stored in data.
 */
typedef void
(* JSTraceDataOp)(JSTracer *trc, void *data);

typedef JSBool
(* JSOperationCallback)(JSContext *cx);

/*
 * Callback used when wrapping determines that the underlying object is already
 * in the compartment for which it is being wrapped. This allows consumers to
 * maintain same-compartment wrapping invariants.
 *
 * |obj| is guaranteed to be same-compartment as |cx|, but it may (or may not)
 * be a security or cross-compartment wrapper. This is an unfortunate contract,
 * but is important for to avoid unnecessarily recomputing every cross-
 * compartment wrapper that gets passed to wrap.
 */
typedef JSObject *
(* JSSameCompartmentWrapObjectCallback)(JSContext *cx, JSObject *obj);

typedef void
(* JSErrorReporter)(JSContext *cx, const char *message, JSErrorReport *report);

/*
 * Possible exception types. These types are part of a JSErrorFormatString
 * structure. They define which error to throw in case of a runtime error.
 * JSEXN_NONE marks an unthrowable error.
 */
typedef enum JSExnType {
    JSEXN_NONE = -1,
      JSEXN_ERR,
        JSEXN_INTERNALERR,
        JSEXN_EVALERR,
        JSEXN_RANGEERR,
        JSEXN_REFERENCEERR,
        JSEXN_SYNTAXERR,
        JSEXN_TYPEERR,
        JSEXN_URIERR,
        JSEXN_LIMIT
} JSExnType;

typedef struct JSErrorFormatString {
    /* The error format string (UTF-8 if js_CStringsAreUTF8). */
    const char *format;

    /* The number of arguments to expand in the formatted error message. */
    uint16 argCount;

    /* One of the JSExnType constants above. */
    int16 exnType;
} JSErrorFormatString;

typedef const JSErrorFormatString *
(* JSErrorCallback)(void *userRef, const char *locale,
                    const uintN errorNumber);

#ifdef va_start
#define JS_ARGUMENT_FORMATTER_DEFINED 1

typedef JSBool
(* JSArgumentFormatter)(JSContext *cx, const char *format, JSBool fromJS,
                        jsval **vpp, va_list *app);
#endif

typedef JSBool
(* JSLocaleToUpperCase)(JSContext *cx, JSString *src, jsval *rval);

typedef JSBool
(* JSLocaleToLowerCase)(JSContext *cx, JSString *src, jsval *rval);

typedef JSBool
(* JSLocaleCompare)(JSContext *cx, JSString *src1, JSString *src2,
                    jsval *rval);

typedef JSBool
(* JSLocaleToUnicode)(JSContext *cx, const char *src, jsval *rval);

/*
 * Security protocol types.
 */

/*
 * XDR-encode or -decode a principals instance, based on whether xdr->mode is
 * JSXDR_ENCODE, in which case *principalsp should be encoded; or JSXDR_DECODE,
 * in which case implementations must return a held (via JSPRINCIPALS_HOLD),
 * non-null *principalsp out parameter.  Return true on success, false on any
 * error, which the implementation must have reported.
 */
typedef JSBool
(* JSPrincipalsTranscoder)(JSXDRState *xdr, JSPrincipals **principalsp);

/*
 * Return a weak reference to the principals associated with obj, possibly via
 * the immutable parent chain leading from obj to a top-level container (e.g.,
 * a window object in the DOM level 0).  If there are no principals associated
 * with obj, return null.  Therefore null does not mean an error was reported;
 * in no event should an error be reported or an exception be thrown by this
 * callback's implementation.
 */
typedef JSPrincipals *
(* JSObjectPrincipalsFinder)(JSContext *cx, JSObject *obj);

/*
 * Used to check if a CSP instance wants to disable eval() and friends.
 * See js_CheckCSPPermitsJSAction() in jsobj.
 */
typedef JSBool
(* JSCSPEvalChecker)(JSContext *cx);

/*
 * Callback used to ask the embedding for the cross compartment wrapper handler
 * that implements the desired prolicy for this kind of object in the
 * destination compartment.
 */
typedef JSObject *
(* JSWrapObjectCallback)(JSContext *cx, JSObject *obj, JSObject *proto, JSObject *parent,
                         uintN flags);

/*
 * Callback used by the wrap hook to ask the embedding to prepare an object
 * for wrapping in a context. This might include unwrapping other wrappers
 * or even finding a more suitable object for the new compartment.
 */
typedef JSObject *
(* JSPreWrapCallback)(JSContext *cx, JSObject *scope, JSObject *obj, uintN flags);

typedef enum {
    JSCOMPARTMENT_DESTROY
} JSCompartmentOp;

typedef JSBool
(* JSCompartmentCallback)(JSContext *cx, JSCompartment *compartment, uintN compartmentOp);

/*
 * Read structured data from the reader r. This hook is used to read a value
 * previously serialized by a call to the WriteStructuredCloneOp hook.
 *
 * tag and data are the pair of uint32 values from the header. The callback may
 * use the JS_Read* APIs to read any other relevant parts of the object from
 * the reader r. closure is any value passed to the JS_ReadStructuredClone
 * function. Return the new object on success, NULL on error/exception.
 */
typedef JSObject *(*ReadStructuredCloneOp)(JSContext *cx, JSStructuredCloneReader *r,
                                           uint32 tag, uint32 data, void *closure);

/*
 * Structured data serialization hook. The engine can write primitive values,
 * Objects, Arrays, Dates, RegExps, TypedArrays, and ArrayBuffers. Any other
 * type of object requires application support. This callback must first use
 * the JS_WriteUint32Pair API to write an object header, passing a value
 * greater than JS_SCTAG_USER to the tag parameter. Then it can use the
 * JS_Write* APIs to write any other relevant parts of the value v to the
 * writer w. closure is any value passed to the JS_WriteStructuredCLone function.
 *
 * Return true on success, false on error/exception.
 */
typedef JSBool (*WriteStructuredCloneOp)(JSContext *cx, JSStructuredCloneWriter *w,
                                         JSObject *obj, void *closure);

/*
 * This is called when JS_WriteStructuredClone finds that the object to be
 * written is recursive. To follow HTML5, the application must throw a
 * DATA_CLONE_ERR DOMException. errorid is always JS_SCERR_RECURSION.
 */
typedef void (*StructuredCloneErrorOp)(JSContext *cx, uint32 errorid);

/************************************************************************/

JS_BEGIN_EXTERN_C

/*
 * Silence warning about returning JS::Value (aka jsval) from functions with C
 * linkage. For C JSAPI clients, jsval will be jsval_layout, which should be
 * ABI compatible.
 */
#ifdef _MSC_VER
# pragma warning(disable:4190)
#endif

/************************************************************************/

/*
 * JS constants. For efficiency, prefer predicates (e.g., JSVAL_IS_NULL).
 * N.B. These constants are initialized at startup.
 */
extern JS_PUBLIC_DATA(const jsval) JSVAL_NULL;
extern JS_PUBLIC_DATA(const jsval) JSVAL_ZERO;
extern JS_PUBLIC_DATA(const jsval) JSVAL_ONE;
extern JS_PUBLIC_DATA(const jsval) JSVAL_FALSE;
extern JS_PUBLIC_DATA(const jsval) JSVAL_TRUE;
extern JS_PUBLIC_DATA(const jsval) JSVAL_VOID;

/************************************************************************/

static JS_ALWAYS_INLINE JSBool
JSVAL_IS_NULL(jsval v)
{
    return JSVAL_IS_NULL_IMPL(JSVAL_TO_IMPL(v));
}

static JS_ALWAYS_INLINE JSBool
JSVAL_IS_VOID(jsval v)
{
    return JSVAL_IS_UNDEFINED_IMPL(JSVAL_TO_IMPL(v));
}

static JS_ALWAYS_INLINE JSBool
JSVAL_IS_INT(jsval v)
{
    return JSVAL_IS_INT32_IMPL(JSVAL_TO_IMPL(v));
}

static JS_ALWAYS_INLINE jsint
JSVAL_TO_INT(jsval v)
{
    JS_ASSERT(JSVAL_IS_INT(v));
    return JSVAL_TO_INT32_IMPL(JSVAL_TO_IMPL(v));
}

static JS_ALWAYS_INLINE jsval
INT_TO_JSVAL(int32 i)
{
    return IMPL_TO_JSVAL(INT32_TO_JSVAL_IMPL(i));
}

static JS_ALWAYS_INLINE JSBool
JSVAL_IS_DOUBLE(jsval v)
{
    return JSVAL_IS_DOUBLE_IMPL(JSVAL_TO_IMPL(v));
}

static JS_ALWAYS_INLINE jsdouble
JSVAL_TO_DOUBLE(jsval v)
{
    jsval_layout l;
    JS_ASSERT(JSVAL_IS_DOUBLE(v));
    l = JSVAL_TO_IMPL(v);
    return l.asDouble;
}

static JS_ALWAYS_INLINE jsval
DOUBLE_TO_JSVAL(jsdouble d)
{
    /* This is a manually inlined version of:
     *    d = JS_CANONICALIZE_NAN(d);
     *    return IMPL_TO_JSVAL(DOUBLE_TO_JSVAL_IMPL(d));
     * because GCC from XCode 3.1.4 miscompiles the above code. */
    jsval_layout l;
    if (JS_UNLIKELY(d != d)) {
        l.asBits = 0x7FF8000000000000LL;
    } else {
        l.asDouble = d;
    }
    return IMPL_TO_JSVAL(l);
}

static JS_ALWAYS_INLINE jsval
UINT_TO_JSVAL(uint32 i)
{
    if (i <= JSVAL_INT_MAX)
        return INT_TO_JSVAL((int32)i);
    return DOUBLE_TO_JSVAL((jsdouble)i);
}

static JS_ALWAYS_INLINE JSBool
JSVAL_IS_NUMBER(jsval v)
{
    return JSVAL_IS_NUMBER_IMPL(JSVAL_TO_IMPL(v));
}

static JS_ALWAYS_INLINE JSBool
JSVAL_IS_STRING(jsval v)
{
    return JSVAL_IS_STRING_IMPL(JSVAL_TO_IMPL(v));
}

static JS_ALWAYS_INLINE JSString *
JSVAL_TO_STRING(jsval v)
{
    JS_ASSERT(JSVAL_IS_STRING(v));
    return JSVAL_TO_STRING_IMPL(JSVAL_TO_IMPL(v));
}

static JS_ALWAYS_INLINE jsval
STRING_TO_JSVAL(JSString *str)
{
    return IMPL_TO_JSVAL(STRING_TO_JSVAL_IMPL(str));
}

static JS_ALWAYS_INLINE JSBool
JSVAL_IS_OBJECT(jsval v)
{
    return JSVAL_IS_OBJECT_OR_NULL_IMPL(JSVAL_TO_IMPL(v));
}

static JS_ALWAYS_INLINE JSObject *
JSVAL_TO_OBJECT(jsval v)
{
    JS_ASSERT(JSVAL_IS_OBJECT(v));
    return JSVAL_TO_OBJECT_IMPL(JSVAL_TO_IMPL(v));
}

static JS_ALWAYS_INLINE jsval
OBJECT_TO_JSVAL(JSObject *obj)
{
    if (obj)
        return IMPL_TO_JSVAL(OBJECT_TO_JSVAL_IMPL(obj));
    return JSVAL_NULL;
}

static JS_ALWAYS_INLINE JSBool
JSVAL_IS_BOOLEAN(jsval v)
{
    return JSVAL_IS_BOOLEAN_IMPL(JSVAL_TO_IMPL(v));
}

static JS_ALWAYS_INLINE JSBool
JSVAL_TO_BOOLEAN(jsval v)
{
    JS_ASSERT(JSVAL_IS_BOOLEAN(v));
    return JSVAL_TO_BOOLEAN_IMPL(JSVAL_TO_IMPL(v));
}

static JS_ALWAYS_INLINE jsval
BOOLEAN_TO_JSVAL(JSBool b)
{
    return IMPL_TO_JSVAL(BOOLEAN_TO_JSVAL_IMPL(b));
}

static JS_ALWAYS_INLINE JSBool
JSVAL_IS_PRIMITIVE(jsval v)
{
    return JSVAL_IS_PRIMITIVE_IMPL(JSVAL_TO_IMPL(v));
}

static JS_ALWAYS_INLINE JSBool
JSVAL_IS_GCTHING(jsval v)
{
    return JSVAL_IS_GCTHING_IMPL(JSVAL_TO_IMPL(v));
}

static JS_ALWAYS_INLINE void *
JSVAL_TO_GCTHING(jsval v)
{
    JS_ASSERT(JSVAL_IS_GCTHING(v));
    return JSVAL_TO_GCTHING_IMPL(JSVAL_TO_IMPL(v));
}

/* To be GC-safe, privates are tagged as doubles. */

static JS_ALWAYS_INLINE jsval
PRIVATE_TO_JSVAL(void *ptr)
{
    return IMPL_TO_JSVAL(PRIVATE_PTR_TO_JSVAL_IMPL(ptr));
}

static JS_ALWAYS_INLINE void *
JSVAL_TO_PRIVATE(jsval v)
{
    JS_ASSERT(JSVAL_IS_DOUBLE(v));
    return JSVAL_TO_PRIVATE_PTR_IMPL(JSVAL_TO_IMPL(v));
}

/************************************************************************/

/*
 * A jsid is an identifier for a property or method of an object which is
 * either a 31-bit signed integer, interned string or object. If XML is
 * enabled, there is an additional singleton jsid value; see
 * JS_DEFAULT_XML_NAMESPACE_ID below. Finally, there is an additional jsid
 * value, JSID_VOID, which does not occur in JS scripts but may be used to
 * indicate the absence of a valid jsid.
 *
 * A jsid is not implicitly convertible to or from a jsval; JS_ValueToId or
 * JS_IdToValue must be used instead.
 */

#define JSID_TYPE_STRING                 0x0
#define JSID_TYPE_INT                    0x1
#define JSID_TYPE_VOID                   0x2
#define JSID_TYPE_OBJECT                 0x4
#define JSID_TYPE_DEFAULT_XML_NAMESPACE  0x6
#define JSID_TYPE_MASK                   0x7

/*
 * Avoid using canonical 'id' for jsid parameters since this is a magic word in
 * Objective-C++ which, apparently, wants to be able to #include jsapi.h.
 */
#define id iden

static JS_ALWAYS_INLINE JSBool
JSID_IS_STRING(jsid id)
{
    return (JSID_BITS(id) & JSID_TYPE_MASK) == 0;
}

static JS_ALWAYS_INLINE JSString *
JSID_TO_STRING(jsid id)
{
    JS_ASSERT(JSID_IS_STRING(id));
    return (JSString *)JSID_BITS(id);
}

static JS_ALWAYS_INLINE JSBool
JSID_IS_ZERO(jsid id)
{
    return JSID_BITS(id) == 0;
}

JS_PUBLIC_API(JSBool)
JS_StringHasBeenInterned(JSContext *cx, JSString *str);

/*
 * Only JSStrings that have been interned via the JSAPI can be turned into
 * jsids by API clients.
 *
 * N.B. if a jsid is backed by a string which has not been interned, that
 * string must be appropriately rooted to avoid being collected by the GC.
 */
static JS_ALWAYS_INLINE jsid
INTERNED_STRING_TO_JSID(JSContext *cx, JSString *str)
{
    jsid id;
    JS_ASSERT(str);
    JS_ASSERT(((size_t)str & JSID_TYPE_MASK) == 0);
    JS_ASSERT(JS_StringHasBeenInterned(cx, str));
    JSID_BITS(id) = (size_t)str;
    return id;
}

static JS_ALWAYS_INLINE JSBool
JSID_IS_INT(jsid id)
{
    return !!(JSID_BITS(id) & JSID_TYPE_INT);
}

static JS_ALWAYS_INLINE int32
JSID_TO_INT(jsid id)
{
    JS_ASSERT(JSID_IS_INT(id));
    return ((int32)JSID_BITS(id)) >> 1;
}

/*
 * Note: when changing these values, verify that their use in
 * js_CheckForStringIndex is still valid.
 */
#define JSID_INT_MIN  (-(1 << 30))
#define JSID_INT_MAX  ((1 << 30) - 1)

static JS_ALWAYS_INLINE JSBool
INT_FITS_IN_JSID(int32 i)
{
    return ((jsuint)(i) - (jsuint)JSID_INT_MIN <=
            (jsuint)(JSID_INT_MAX - JSID_INT_MIN));
}

static JS_ALWAYS_INLINE jsid
INT_TO_JSID(int32 i)
{
    jsid id;
    JS_ASSERT(INT_FITS_IN_JSID(i));
    JSID_BITS(id) = ((i << 1) | JSID_TYPE_INT);
    return id;
}

static JS_ALWAYS_INLINE JSBool
JSID_IS_OBJECT(jsid id)
{
    return (JSID_BITS(id) & JSID_TYPE_MASK) == JSID_TYPE_OBJECT &&
           (size_t)JSID_BITS(id) != JSID_TYPE_OBJECT;
}

static JS_ALWAYS_INLINE JSObject *
JSID_TO_OBJECT(jsid id)
{
    JS_ASSERT(JSID_IS_OBJECT(id));
    return (JSObject *)(JSID_BITS(id) & ~(size_t)JSID_TYPE_MASK);
}

static JS_ALWAYS_INLINE jsid
OBJECT_TO_JSID(JSObject *obj)
{
    jsid id;
    JS_ASSERT(obj != NULL);
    JS_ASSERT(((size_t)obj & JSID_TYPE_MASK) == 0);
    JSID_BITS(id) = ((size_t)obj | JSID_TYPE_OBJECT);
    return id;
}

static JS_ALWAYS_INLINE JSBool
JSID_IS_GCTHING(jsid id)
{
    return JSID_IS_STRING(id) || JSID_IS_OBJECT(id);
}

static JS_ALWAYS_INLINE void *
JSID_TO_GCTHING(jsid id)
{
    return (void *)(JSID_BITS(id) & ~(size_t)JSID_TYPE_MASK);
}

/*
 * The magic XML namespace id is not a valid jsid. Global object classes in
 * embeddings that enable JS_HAS_XML_SUPPORT (E4X) should handle this id.
 */

static JS_ALWAYS_INLINE JSBool
JSID_IS_DEFAULT_XML_NAMESPACE(jsid id)
{
    JS_ASSERT_IF(((size_t)JSID_BITS(id) & JSID_TYPE_MASK) == JSID_TYPE_DEFAULT_XML_NAMESPACE,
                 JSID_BITS(id) == JSID_TYPE_DEFAULT_XML_NAMESPACE);
    return ((size_t)JSID_BITS(id) == JSID_TYPE_DEFAULT_XML_NAMESPACE);
}

#ifdef JS_USE_JSID_STRUCT_TYPES
extern JS_PUBLIC_DATA(jsid) JS_DEFAULT_XML_NAMESPACE_ID;
#else
# define JS_DEFAULT_XML_NAMESPACE_ID ((jsid)JSID_TYPE_DEFAULT_XML_NAMESPACE)
#endif

/*
 * A void jsid is not a valid id and only arises as an exceptional API return
 * value, such as in JS_NextProperty. Embeddings must not pass JSID_VOID into
 * JSAPI entry points expecting a jsid and do not need to handle JSID_VOID in
 * hooks receiving a jsid except when explicitly noted in the API contract.
 */

static JS_ALWAYS_INLINE JSBool
JSID_IS_VOID(jsid id)
{
    JS_ASSERT_IF(((size_t)JSID_BITS(id) & JSID_TYPE_MASK) == JSID_TYPE_VOID,
                 JSID_BITS(id) == JSID_TYPE_VOID);
    return ((size_t)JSID_BITS(id) == JSID_TYPE_VOID);
}

static JS_ALWAYS_INLINE JSBool
JSID_IS_EMPTY(jsid id)
{
    return ((size_t)JSID_BITS(id) == JSID_TYPE_OBJECT);
}

#undef id

#ifdef JS_USE_JSID_STRUCT_TYPES
extern JS_PUBLIC_DATA(jsid) JSID_VOID;
extern JS_PUBLIC_DATA(jsid) JSID_EMPTY;
#else
# define JSID_VOID ((jsid)JSID_TYPE_VOID)
# define JSID_EMPTY ((jsid)JSID_TYPE_OBJECT)
#endif

/*
 * Returns true iff the given jsval is immune to GC and can be used across
 * multiple JSRuntimes without requiring any conversion API.
 */
static JS_ALWAYS_INLINE JSBool
JSVAL_IS_UNIVERSAL(jsval v)
{
    return !JSVAL_IS_GCTHING(v);
}

/************************************************************************/

/* Lock and unlock the GC thing held by a jsval. */
#define JSVAL_LOCK(cx,v)        (JSVAL_IS_GCTHING(v)                          \
                                 ? JS_LockGCThing(cx, JSVAL_TO_GCTHING(v))    \
                                 : JS_TRUE)
#define JSVAL_UNLOCK(cx,v)      (JSVAL_IS_GCTHING(v)                          \
                                 ? JS_UnlockGCThing(cx, JSVAL_TO_GCTHING(v))  \
                                 : JS_TRUE)

/* Property attributes, set in JSPropertySpec and passed to API functions. */
#define JSPROP_ENUMERATE        0x01    /* property is visible to for/in loop */
#define JSPROP_READONLY         0x02    /* not settable: assignment is no-op */
#define JSPROP_PERMANENT        0x04    /* property cannot be deleted */
#define JSPROP_GETTER           0x10    /* property holds getter function */
#define JSPROP_SETTER           0x20    /* property holds setter function */
#define JSPROP_SHARED           0x40    /* don't allocate a value slot for this
                                           property; don't copy the property on
                                           set of the same-named property in an
                                           object that delegates to a prototype
                                           containing this property */
#define JSPROP_INDEX            0x80    /* name is actually (jsint) index */
#define JSPROP_SHORTID          0x100   /* set in JS_DefineProperty attrs
                                           if getters/setters use a shortid */
#define JSPROP_NATIVE_ACCESSORS 0x08    /* set in JSPropertyDescriptor.flags
                                           if getters/setters are JSNatives */

/* Function flags, internal use only, returned by JS_GetFunctionFlags. */
#define JSFUN_LAMBDA            0x08    /* expressed, not declared, function */
#define JSFUN_HEAVYWEIGHT       0x80    /* activation requires a Call object */

#define JSFUN_HEAVYWEIGHT_TEST(f)  ((f) & JSFUN_HEAVYWEIGHT)

/* 0x0100 is unused */
#define JSFUN_CONSTRUCTOR     0x0200    /* native that can be called as a ctor
                                           without creating a this object */

#define JSFUN_FLAGS_MASK      0x07f8    /* overlay JSFUN_* attributes --
                                           bits 12-15 are used internally to
                                           flag interpreted functions */

#define JSFUN_STUB_GSOPS      0x1000    /* use JS_PropertyStub getter/setter
                                           instead of defaulting to class gsops
                                           for property holding function */

/*
 * Re-use JSFUN_LAMBDA, which applies only to scripted functions, for use in
 * JSFunctionSpec arrays that specify generic native prototype methods, i.e.,
 * methods of a class prototype that are exposed as static methods taking an
 * extra leading argument: the generic |this| parameter.
 *
 * If you set this flag in a JSFunctionSpec struct's flags initializer, then
 * that struct must live at least as long as the native static method object
 * created due to this flag by JS_DefineFunctions or JS_InitClass.  Typically
 * JSFunctionSpec structs are allocated in static arrays.
 */
#define JSFUN_GENERIC_NATIVE    JSFUN_LAMBDA

/*
 * The first call to JS_CallOnce by any thread in a process will call 'func'.
 * Later calls to JS_CallOnce with the same JSCallOnceType object will be
 * suppressed.
 *
 * Equivalently: each distinct JSCallOnceType object will allow one JS_CallOnce
 * to invoke its JSInitCallback.
 */
extern JS_PUBLIC_API(JSBool)
JS_CallOnce(JSCallOnceType *once, JSInitCallback func);

/*
 * Microseconds since the epoch, midnight, January 1, 1970 UTC.  See the
 * comment in jstypes.h regarding safe int64 usage.
 */
extern JS_PUBLIC_API(int64)
JS_Now(void);

/* Don't want to export data, so provide accessors for non-inline jsvals. */
extern JS_PUBLIC_API(jsval)
JS_GetNaNValue(JSContext *cx);

extern JS_PUBLIC_API(jsval)
JS_GetNegativeInfinityValue(JSContext *cx);

extern JS_PUBLIC_API(jsval)
JS_GetPositiveInfinityValue(JSContext *cx);

extern JS_PUBLIC_API(jsval)
JS_GetEmptyStringValue(JSContext *cx);

extern JS_PUBLIC_API(JSString *)
JS_GetEmptyString(JSRuntime *rt);

/*
 * Format is a string of the following characters (spaces are insignificant),
 * specifying the tabulated type conversions:
 *
 *   b      JSBool          Boolean
 *   c      uint16/jschar   ECMA uint16, Unicode char
 *   i      int32           ECMA int32
 *   u      uint32          ECMA uint32
 *   j      int32           Rounded int32 (coordinate)
 *   d      jsdouble        IEEE double
 *   I      jsdouble        Integral IEEE double
 *   S      JSString *      Unicode string, accessed by a JSString pointer
 *   W      jschar *        Unicode character vector, 0-terminated (W for wide)
 *   o      JSObject *      Object reference
 *   f      JSFunction *    Function private
 *   v      jsval           Argument value (no conversion)
 *   *      N/A             Skip this argument (no vararg)
 *   /      N/A             End of required arguments
 *
 * The variable argument list after format must consist of &b, &c, &s, e.g.,
 * where those variables have the types given above.  For the pointer types
 * char *, JSString *, and JSObject *, the pointed-at memory returned belongs
 * to the JS runtime, not to the calling native code.  The runtime promises
 * to keep this memory valid so long as argv refers to allocated stack space
 * (so long as the native function is active).
 *
 * Fewer arguments than format specifies may be passed only if there is a /
 * in format after the last required argument specifier and argc is at least
 * the number of required arguments.  More arguments than format specifies
 * may be passed without error; it is up to the caller to deal with trailing
 * unconverted arguments.
 */
extern JS_PUBLIC_API(JSBool)
JS_ConvertArguments(JSContext *cx, uintN argc, jsval *argv, const char *format,
                    ...);

#ifdef va_start
extern JS_PUBLIC_API(JSBool)
JS_ConvertArgumentsVA(JSContext *cx, uintN argc, jsval *argv,
                      const char *format, va_list ap);
#endif

#ifdef JS_ARGUMENT_FORMATTER_DEFINED

/*
 * Add and remove a format string handler for JS_{Convert,Push}Arguments{,VA}.
 * The handler function has this signature:
 *
 *   JSBool MyArgumentFormatter(JSContext *cx, const char *format,
 *                              JSBool fromJS, jsval **vpp, va_list *app);
 *
 * It should return true on success, and return false after reporting an error
 * or detecting an already-reported error.
 *
 * For a given format string, for example "AA", the formatter is called from
 * JS_ConvertArgumentsVA like so:
 *
 *   formatter(cx, "AA...", JS_TRUE, &sp, &ap);
 *
 * sp points into the arguments array on the JS stack, while ap points into
 * the stdarg.h va_list on the C stack.  The JS_TRUE passed for fromJS tells
 * the formatter to convert zero or more jsvals at sp to zero or more C values
 * accessed via pointers-to-values at ap, updating both sp (via *vpp) and ap
 * (via *app) to point past the converted arguments and their result pointers
 * on the C stack.
 *
 * When called from JS_PushArgumentsVA, the formatter is invoked thus:
 *
 *   formatter(cx, "AA...", JS_FALSE, &sp, &ap);
 *
 * where JS_FALSE for fromJS means to wrap the C values at ap according to the
 * format specifier and store them at sp, updating ap and sp appropriately.
 *
 * The "..." after "AA" is the rest of the format string that was passed into
 * JS_{Convert,Push}Arguments{,VA}.  The actual format trailing substring used
 * in each Convert or PushArguments call is passed to the formatter, so that
 * one such function may implement several formats, in order to share code.
 *
 * Remove just forgets about any handler associated with format.  Add does not
 * copy format, it points at the string storage allocated by the caller, which
 * is typically a string constant.  If format is in dynamic storage, it is up
 * to the caller to keep the string alive until Remove is called.
 */
extern JS_PUBLIC_API(JSBool)
JS_AddArgumentFormatter(JSContext *cx, const char *format,
                        JSArgumentFormatter formatter);

extern JS_PUBLIC_API(void)
JS_RemoveArgumentFormatter(JSContext *cx, const char *format);

#endif /* JS_ARGUMENT_FORMATTER_DEFINED */

extern JS_PUBLIC_API(JSBool)
JS_ConvertValue(JSContext *cx, jsval v, JSType type, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_ValueToObject(JSContext *cx, jsval v, JSObject **objp);

extern JS_PUBLIC_API(JSFunction *)
JS_ValueToFunction(JSContext *cx, jsval v);

extern JS_PUBLIC_API(JSFunction *)
JS_ValueToConstructor(JSContext *cx, jsval v);

extern JS_PUBLIC_API(JSString *)
JS_ValueToString(JSContext *cx, jsval v);

extern JS_PUBLIC_API(JSString *)
JS_ValueToSource(JSContext *cx, jsval v);

extern JS_PUBLIC_API(JSBool)
JS_ValueToNumber(JSContext *cx, jsval v, jsdouble *dp);

extern JS_PUBLIC_API(JSBool)
JS_DoubleIsInt32(jsdouble d, jsint *ip);

extern JS_PUBLIC_API(int32)
JS_DoubleToInt32(jsdouble d);

extern JS_PUBLIC_API(uint32)
JS_DoubleToUint32(jsdouble d);

/*
 * Convert a value to a number, then to an int32, according to the ECMA rules
 * for ToInt32.
 */
extern JS_PUBLIC_API(JSBool)
JS_ValueToECMAInt32(JSContext *cx, jsval v, int32 *ip);

/*
 * Convert a value to a number, then to a uint32, according to the ECMA rules
 * for ToUint32.
 */
extern JS_PUBLIC_API(JSBool)
JS_ValueToECMAUint32(JSContext *cx, jsval v, uint32 *ip);

/*
 * Convert a value to a number, then to an int32 if it fits by rounding to
 * nearest; but failing with an error report if the double is out of range
 * or unordered.
 */
extern JS_PUBLIC_API(JSBool)
JS_ValueToInt32(JSContext *cx, jsval v, int32 *ip);

/*
 * ECMA ToUint16, for mapping a jsval to a Unicode point.
 */
extern JS_PUBLIC_API(JSBool)
JS_ValueToUint16(JSContext *cx, jsval v, uint16 *ip);

extern JS_PUBLIC_API(JSBool)
JS_ValueToBoolean(JSContext *cx, jsval v, JSBool *bp);

extern JS_PUBLIC_API(JSType)
JS_TypeOfValue(JSContext *cx, jsval v);

extern JS_PUBLIC_API(const char *)
JS_GetTypeName(JSContext *cx, JSType type);

extern JS_PUBLIC_API(JSBool)
JS_StrictlyEqual(JSContext *cx, jsval v1, jsval v2, JSBool *equal);

extern JS_PUBLIC_API(JSBool)
JS_LooselyEqual(JSContext *cx, jsval v1, jsval v2, JSBool *equal);

extern JS_PUBLIC_API(JSBool)
JS_SameValue(JSContext *cx, jsval v1, jsval v2, JSBool *same);

/* True iff fun is the global eval function. */
extern JS_PUBLIC_API(JSBool)
JS_IsBuiltinEvalFunction(JSFunction *fun);

/* True iff fun is the Function constructor. */
extern JS_PUBLIC_API(JSBool)
JS_IsBuiltinFunctionConstructor(JSFunction *fun);

/************************************************************************/

/*
 * Initialization, locking, contexts, and memory allocation.
 *
 * It is important that the first runtime and first context be created in a
 * single-threaded fashion, otherwise the behavior of the library is undefined.
 * See: http://developer.mozilla.org/en/docs/Category:JSAPI_Reference
 */
#define JS_NewRuntime       JS_Init
#define JS_DestroyRuntime   JS_Finish
#define JS_LockRuntime      JS_Lock
#define JS_UnlockRuntime    JS_Unlock

extern JS_PUBLIC_API(JSRuntime *)
JS_NewRuntime(uint32 maxbytes);

/* Deprecated. */
#define JS_CommenceRuntimeShutDown(rt) ((void) 0)

extern JS_PUBLIC_API(void)
JS_DestroyRuntime(JSRuntime *rt);

extern JS_PUBLIC_API(void)
JS_ShutDown(void);

JS_PUBLIC_API(void *)
JS_GetRuntimePrivate(JSRuntime *rt);

JS_PUBLIC_API(void)
JS_SetRuntimePrivate(JSRuntime *rt, void *data);

extern JS_PUBLIC_API(void)
JS_BeginRequest(JSContext *cx);

extern JS_PUBLIC_API(void)
JS_EndRequest(JSContext *cx);

/* Yield to pending GC operations, regardless of request depth */
extern JS_PUBLIC_API(void)
JS_YieldRequest(JSContext *cx);

extern JS_PUBLIC_API(jsrefcount)
JS_SuspendRequest(JSContext *cx);

extern JS_PUBLIC_API(void)
JS_ResumeRequest(JSContext *cx, jsrefcount saveDepth);

extern JS_PUBLIC_API(JSBool)
JS_IsInRequest(JSContext *cx);

#ifdef __cplusplus
JS_END_EXTERN_C

class JSAutoRequest {
  public:
    JSAutoRequest(JSContext *cx JS_GUARD_OBJECT_NOTIFIER_PARAM)
        : mContext(cx), mSaveDepth(0) {
        JS_GUARD_OBJECT_NOTIFIER_INIT;
        JS_BeginRequest(mContext);
    }
    ~JSAutoRequest() {
        JS_EndRequest(mContext);
    }

    void suspend() {
        mSaveDepth = JS_SuspendRequest(mContext);
    }
    void resume() {
        JS_ResumeRequest(mContext, mSaveDepth);
    }

  protected:
    JSContext *mContext;
    jsrefcount mSaveDepth;
    JS_DECL_USE_GUARD_OBJECT_NOTIFIER

#if 0
  private:
    static void *operator new(size_t) CPP_THROW_NEW { return 0; };
    static void operator delete(void *, size_t) { };
#endif
};

class JSAutoSuspendRequest {
  public:
    JSAutoSuspendRequest(JSContext *cx JS_GUARD_OBJECT_NOTIFIER_PARAM)
        : mContext(cx), mSaveDepth(0) {
        JS_GUARD_OBJECT_NOTIFIER_INIT;
        if (mContext) {
            mSaveDepth = JS_SuspendRequest(mContext);
        }
    }
    ~JSAutoSuspendRequest() {
        resume();
    }

    void resume() {
        if (mContext) {
            JS_ResumeRequest(mContext, mSaveDepth);
            mContext = 0;
        }
    }

  protected:
    JSContext *mContext;
    jsrefcount mSaveDepth;
    JS_DECL_USE_GUARD_OBJECT_NOTIFIER

#if 0
  private:
    static void *operator new(size_t) CPP_THROW_NEW { return 0; };
    static void operator delete(void *, size_t) { };
#endif
};

class JSAutoCheckRequest {
  public:
    JSAutoCheckRequest(JSContext *cx JS_GUARD_OBJECT_NOTIFIER_PARAM) {
#if defined JS_THREADSAFE && defined DEBUG
        mContext = cx;
        JS_ASSERT(JS_IsInRequest(cx));
#endif
        JS_GUARD_OBJECT_NOTIFIER_INIT;
    }

    ~JSAutoCheckRequest() {
#if defined JS_THREADSAFE && defined DEBUG
        JS_ASSERT(JS_IsInRequest(mContext));
#endif
    }


  private:
#if defined JS_THREADSAFE && defined DEBUG
    JSContext *mContext;
#endif
    JS_DECL_USE_GUARD_OBJECT_NOTIFIER
};

JS_BEGIN_EXTERN_C
#endif

extern JS_PUBLIC_API(void)
JS_Lock(JSRuntime *rt);

extern JS_PUBLIC_API(void)
JS_Unlock(JSRuntime *rt);

extern JS_PUBLIC_API(JSContextCallback)
JS_SetContextCallback(JSRuntime *rt, JSContextCallback cxCallback);

extern JS_PUBLIC_API(JSContext *)
JS_NewContext(JSRuntime *rt, size_t stackChunkSize);

extern JS_PUBLIC_API(void)
JS_DestroyContext(JSContext *cx);

extern JS_PUBLIC_API(void)
JS_DestroyContextNoGC(JSContext *cx);

extern JS_PUBLIC_API(void)
JS_DestroyContextMaybeGC(JSContext *cx);

extern JS_PUBLIC_API(void *)
JS_GetContextPrivate(JSContext *cx);

extern JS_PUBLIC_API(void)
JS_SetContextPrivate(JSContext *cx, void *data);

extern JS_PUBLIC_API(JSRuntime *)
JS_GetRuntime(JSContext *cx);

extern JS_PUBLIC_API(JSContext *)
JS_ContextIterator(JSRuntime *rt, JSContext **iterp);

extern JS_PUBLIC_API(JSVersion)
JS_GetVersion(JSContext *cx);

extern JS_PUBLIC_API(JSVersion)
JS_SetVersion(JSContext *cx, JSVersion version);

extern JS_PUBLIC_API(const char *)
JS_VersionToString(JSVersion version);

extern JS_PUBLIC_API(JSVersion)
JS_StringToVersion(const char *string);

/*
 * JS options are orthogonal to version, and may be freely composed with one
 * another as well as with version.
 *
 * JSOPTION_VAROBJFIX is recommended -- see the comments associated with the
 * prototypes for JS_ExecuteScript, JS_EvaluateScript, etc.
 */
#define JSOPTION_STRICT         JS_BIT(0)       /* warn on dubious practice */
#define JSOPTION_WERROR         JS_BIT(1)       /* convert warning to error */
#define JSOPTION_VAROBJFIX      JS_BIT(2)       /* make JS_EvaluateScript use
                                                   the last object on its 'obj'
                                                   param's scope chain as the
                                                   ECMA 'variables object' */
#define JSOPTION_PRIVATE_IS_NSISUPPORTS \
                                JS_BIT(3)       /* context private data points
                                                   to an nsISupports subclass */
#define JSOPTION_COMPILE_N_GO   JS_BIT(4)       /* caller of JS_Compile*Script
                                                   promises to execute compiled
                                                   script once only; enables
                                                   compile-time scope chain
                                                   resolution of consts. */
#define JSOPTION_ATLINE         JS_BIT(5)       /* //@line number ["filename"]
                                                   option supported for the
                                                   XUL preprocessor and kindred
                                                   beasts. */
#define JSOPTION_XML            JS_BIT(6)       /* EMCAScript for XML support:
                                                   parse <!-- --> as a token,
                                                   not backward compatible with
                                                   the comment-hiding hack used
                                                   in HTML script tags. */
#define JSOPTION_DONT_REPORT_UNCAUGHT \
                                JS_BIT(8)       /* When returning from the
                                                   outermost API call, prevent
                                                   uncaught exceptions from
                                                   being converted to error
                                                   reports */

#define JSOPTION_RELIMIT        JS_BIT(9)       /* Throw exception on any
                                                   regular expression which
                                                   backtracks more than n^3
                                                   times, where n is length
                                                   of the input string */
/* JS_BIT(10) is currently unused. */

#define JSOPTION_JIT            JS_BIT(11)      /* Enable JIT compilation. */

#define JSOPTION_NO_SCRIPT_RVAL JS_BIT(12)      /* A promise to the compiler
                                                   that a null rval out-param
                                                   will be passed to each call
                                                   to JS_ExecuteScript. */
#define JSOPTION_UNROOTED_GLOBAL JS_BIT(13)     /* The GC will not root the
                                                   contexts' global objects
                                                   (see JS_GetGlobalObject),
                                                   leaving that up to the
                                                   embedding. */

#define JSOPTION_METHODJIT      JS_BIT(14)      /* Whole-method JIT. */
#define JSOPTION_PROFILING      JS_BIT(15)      /* Profiler to make tracer/methodjit choices. */
#define JSOPTION_METHODJIT_ALWAYS \
                                JS_BIT(16)      /* Always whole-method JIT,
                                                   don't tune at run-time. */
#define JSOPTION_PCCOUNT        JS_BIT(17)      /* Collect per-op execution counts */

#define JSOPTION_TYPE_INFERENCE JS_BIT(18)      /* Perform type inference. */

/* Options which reflect compile-time properties of scripts. */
#define JSCOMPILEOPTION_MASK    (JSOPTION_XML)

#define JSRUNOPTION_MASK        (JS_BITMASK(19) & ~JSCOMPILEOPTION_MASK)
#define JSALLOPTION_MASK        (JSCOMPILEOPTION_MASK | JSRUNOPTION_MASK)

extern JS_PUBLIC_API(uint32)
JS_GetOptions(JSContext *cx);

extern JS_PUBLIC_API(uint32)
JS_SetOptions(JSContext *cx, uint32 options);

extern JS_PUBLIC_API(uint32)
JS_ToggleOptions(JSContext *cx, uint32 options);

extern JS_PUBLIC_API(const char *)
JS_GetImplementationVersion(void);

extern JS_PUBLIC_API(JSCompartmentCallback)
JS_SetCompartmentCallback(JSRuntime *rt, JSCompartmentCallback callback);

extern JS_PUBLIC_API(JSWrapObjectCallback)
JS_SetWrapObjectCallbacks(JSRuntime *rt,
                          JSWrapObjectCallback callback,
                          JSSameCompartmentWrapObjectCallback sccallback,
                          JSPreWrapCallback precallback);

extern JS_PUBLIC_API(JSCrossCompartmentCall *)
JS_EnterCrossCompartmentCall(JSContext *cx, JSObject *target);

extern JS_PUBLIC_API(void)
JS_LeaveCrossCompartmentCall(JSCrossCompartmentCall *call);

extern JS_PUBLIC_API(void *)
JS_SetCompartmentPrivate(JSContext *cx, JSCompartment *compartment, void *data);

extern JS_PUBLIC_API(void *)
JS_GetCompartmentPrivate(JSContext *cx, JSCompartment *compartment);

extern JS_PUBLIC_API(JSBool)
JS_WrapObject(JSContext *cx, JSObject **objp);

extern JS_PUBLIC_API(JSBool)
JS_WrapValue(JSContext *cx, jsval *vp);

extern JS_PUBLIC_API(JSObject *)
JS_TransplantObject(JSContext *cx, JSObject *origobj, JSObject *target);

extern JS_FRIEND_API(JSObject *)
js_TransplantObjectWithWrapper(JSContext *cx,
                               JSObject *origobj,
                               JSObject *origwrapper,
                               JSObject *targetobj,
                               JSObject *targetwrapper);

extern JS_FRIEND_API(JSObject *)
js_TransplantObjectWithWrapper(JSContext *cx,
                               JSObject *origobj,
                               JSObject *origwrapper,
                               JSObject *targetobj,
                               JSObject *targetwrapper);

extern JS_FRIEND_API(void *)
js_GetCompartmentPrivate(JSCompartment *compartment);

#ifdef __cplusplus
JS_END_EXTERN_C

class JS_PUBLIC_API(JSAutoEnterCompartment)
{
    /*
     * This is a poor man's Maybe<AutoCompartment>, because we don't have
     * access to the AutoCompartment definition here.  We statically assert in
     * jsapi.cpp that we have the right size here.
     *
     * In practice, 32-bit Windows and Android get 16-word |bytes|, while
     * other platforms get 13-word |bytes|.
     */
    void* bytes[sizeof(void*) == 4 && MOZ_ALIGNOF(JSUint64) == 8 ? 16 : 13];

    /*
     * This object may be in one of three states.  If enter() or
     * enterAndIgnoreErrors() hasn't been called, it's in STATE_UNENTERED.
     * Otherwise, if we were asked to enter into the current compartment, our
     * state is STATE_SAME_COMPARTMENT.  If we actually created an
     * AutoCompartment and entered another compartment, our state is
     * STATE_OTHER_COMPARTMENT.
     */
    enum State {
        STATE_UNENTERED,
        STATE_SAME_COMPARTMENT,
        STATE_OTHER_COMPARTMENT
    } state;

  public:
    JSAutoEnterCompartment() : state(STATE_UNENTERED) {}

    bool enter(JSContext *cx, JSObject *target);

    void enterAndIgnoreErrors(JSContext *cx, JSObject *target);

    bool entered() const { return state != STATE_UNENTERED; }

    ~JSAutoEnterCompartment();
};

JS_BEGIN_EXTERN_C
#endif

extern JS_PUBLIC_API(JSObject *)
JS_GetGlobalObject(JSContext *cx);

extern JS_PUBLIC_API(void)
JS_SetGlobalObject(JSContext *cx, JSObject *obj);

/*
 * Initialize standard JS class constructors, prototypes, and any top-level
 * functions and constants associated with the standard classes (e.g. isNaN
 * for Number).
 *
 * NB: This sets cx's global object to obj if it was null.
 */
extern JS_PUBLIC_API(JSBool)
JS_InitStandardClasses(JSContext *cx, JSObject *obj);

/*
 * Resolve id, which must contain either a string or an int, to a standard
 * class name in obj if possible, defining the class's constructor and/or
 * prototype and storing true in *resolved.  If id does not name a standard
 * class or a top-level property induced by initializing a standard class,
 * store false in *resolved and just return true.  Return false on error,
 * as usual for JSBool result-typed API entry points.
 *
 * This API can be called directly from a global object class's resolve op,
 * to define standard classes lazily.  The class's enumerate op should call
 * JS_EnumerateStandardClasses(cx, obj), to define eagerly during for..in
 * loops any classes not yet resolved lazily.
 */
extern JS_PUBLIC_API(JSBool)
JS_ResolveStandardClass(JSContext *cx, JSObject *obj, jsid id,
                        JSBool *resolved);

extern JS_PUBLIC_API(JSBool)
JS_EnumerateStandardClasses(JSContext *cx, JSObject *obj);

/*
 * Enumerate any already-resolved standard class ids into ida, or into a new
 * JSIdArray if ida is null.  Return the augmented array on success, null on
 * failure with ida (if it was non-null on entry) destroyed.
 */
extern JS_PUBLIC_API(JSIdArray *)
JS_EnumerateResolvedStandardClasses(JSContext *cx, JSObject *obj,
                                    JSIdArray *ida);

extern JS_PUBLIC_API(JSBool)
JS_GetClassObject(JSContext *cx, JSObject *obj, JSProtoKey key,
                  JSObject **objp);

extern JS_PUBLIC_API(JSObject *)
JS_GetGlobalForObject(JSContext *cx, JSObject *obj);

extern JS_PUBLIC_API(JSObject *)
JS_GetGlobalForScopeChain(JSContext *cx);

/*
 * Initialize the 'Reflect' object on a global object.
 */
extern JS_PUBLIC_API(JSObject *)
JS_InitReflect(JSContext *cx, JSObject *global);

#ifdef JS_HAS_CTYPES
/*
 * Initialize the 'ctypes' object on a global variable 'obj'. The 'ctypes'
 * object will be sealed.
 */
extern JS_PUBLIC_API(JSBool)
JS_InitCTypesClass(JSContext *cx, JSObject *global);

/*
 * Convert a unicode string 'source' of length 'slen' to the platform native
 * charset, returning a null-terminated string allocated with JS_malloc. On
 * failure, this function should report an error.
 */
typedef char *
(* JSCTypesUnicodeToNativeFun)(JSContext *cx, const jschar *source, size_t slen);

/*
 * Set of function pointers that ctypes can use for various internal functions.
 * See JS_SetCTypesCallbacks below. Providing NULL for a function is safe,
 * and will result in the applicable ctypes functionality not being available.
 */
struct JSCTypesCallbacks {
    JSCTypesUnicodeToNativeFun unicodeToNative;
};

typedef struct JSCTypesCallbacks JSCTypesCallbacks;

/*
 * Set the callbacks on the provided 'ctypesObj' object. 'callbacks' should be a
 * pointer to static data that exists for the lifetime of 'ctypesObj', but it
 * may safely be altered after calling this function and without having
 * to call this function again.
 */
extern JS_PUBLIC_API(JSBool)
JS_SetCTypesCallbacks(JSContext *cx, JSObject *ctypesObj, JSCTypesCallbacks *callbacks);
#endif

typedef JSBool
(* JSEnumerateDiagnosticMemoryCallback)(void *ptr, size_t length);

/*
 * Enumerate memory regions that contain diagnostic information
 * intended to be included in crash report minidumps.
 */
extern JS_PUBLIC_API(void)
JS_EnumerateDiagnosticMemoryRegions(JSEnumerateDiagnosticMemoryCallback callback);

/*
 * Macros to hide interpreter stack layout details from a JSFastNative using
 * its jsval *vp parameter. The stack layout underlying invocation can't change
 * without breaking source and binary compatibility (argv[-2] is well-known to
 * be the callee jsval, and argv[-1] is as well known to be |this|).
 *
 * Note well: However, argv[-1] may be JSVAL_NULL where with slow natives it
 * is the global object, so embeddings implementing fast natives *must* call
 * JS_THIS or JS_THIS_OBJECT and test for failure indicated by a null return,
 * which should propagate as a false return from native functions and hooks.
 *
 * To reduce boilerplace checks, JS_InstanceOf and JS_GetInstancePrivate now
 * handle a null obj parameter by returning false (throwing a TypeError if
 * given non-null argv), so most native functions that type-check their |this|
 * parameter need not add null checking.
 *
 * NB: there is an anti-dependency between JS_CALLEE and JS_SET_RVAL: native
 * methods that may inspect their callee must defer setting their return value
 * until after any such possible inspection. Otherwise the return value will be
 * inspected instead of the callee function object.
 *
 * WARNING: These are not (yet) mandatory macros, but new code outside of the
 * engine should use them. In the Mozilla 2.0 milestone their definitions may
 * change incompatibly.
 *
 * N.B. constructors must not use JS_THIS, as no 'this' object has been created.
 */

#define JS_CALLEE(cx,vp)        ((vp)[0])
#define JS_THIS(cx,vp)          JS_ComputeThis(cx, vp)
#define JS_THIS_OBJECT(cx,vp)   (JSVAL_TO_OBJECT(JS_THIS(cx,vp)))
#define JS_ARGV(cx,vp)          ((vp) + 2)
#define JS_RVAL(cx,vp)          (*(vp))
#define JS_SET_RVAL(cx,vp,v)    (*(vp) = (v))

extern JS_PUBLIC_API(jsval)
JS_ComputeThis(JSContext *cx, jsval *vp);

#ifdef __cplusplus
#undef JS_THIS
static inline jsval
JS_THIS(JSContext *cx, jsval *vp)
{
    return JSVAL_IS_PRIMITIVE(vp[1]) ? JS_ComputeThis(cx, vp) : vp[1];
}
#endif

/*
 * |this| is passed to functions in ES5 without change.  Functions themselves
 * do any post-processing they desire to box |this|, compute the global object,
 * &c.  Use this macro to retrieve a function's unboxed |this| value.
 *
 * This macro must not be used in conjunction with JS_THIS or JS_THIS_OBJECT,
 * or vice versa.  Either use the provided this value with this macro, or
 * compute the boxed this value using those.
 *
 * N.B. constructors must not use JS_THIS_VALUE, as no 'this' object has been
 * created.
 */
#define JS_THIS_VALUE(cx,vp)    ((vp)[1])

extern JS_PUBLIC_API(void *)
JS_malloc(JSContext *cx, size_t nbytes);

extern JS_PUBLIC_API(void *)
JS_realloc(JSContext *cx, void *p, size_t nbytes);

extern JS_PUBLIC_API(void)
JS_free(JSContext *cx, void *p);

extern JS_PUBLIC_API(void)
JS_updateMallocCounter(JSContext *cx, size_t nbytes);

extern JS_PUBLIC_API(char *)
JS_strdup(JSContext *cx, const char *s);

extern JS_PUBLIC_API(JSBool)
JS_NewNumberValue(JSContext *cx, jsdouble d, jsval *rval);

/*
 * A GC root is a pointer to a jsval, JSObject * or JSString * that itself
 * points into the GC heap. JS_AddValueRoot takes a pointer to a jsval and
 * JS_AddGCThingRoot takes a pointer to a JSObject * or JString *.
 *
 * Note that, since JS_Add*Root stores the address of a variable (of type
 * jsval, JSString *, or JSObject *), that variable must live until
 * JS_Remove*Root is called to remove that variable. For example, after:
 *
 *   void some_function() {
 *     jsval v;
 *     JS_AddNamedRootedValue(cx, &v, "name");
 *
 * the caller must perform
 *
 *     JS_RemoveRootedValue(cx, &v);
 *
 * before some_function() returns.
 *
 * Also, use JS_AddNamed*Root(cx, &structPtr->memberObj, "structPtr->memberObj")
 * in preference to JS_Add*Root(cx, &structPtr->memberObj), in order to identify
 * roots by their source callsites.  This way, you can find the callsite while
 * debugging if you should fail to do JS_Remove*Root(cx, &structPtr->memberObj)
 * before freeing structPtr's memory.
 */
extern JS_PUBLIC_API(JSBool)
JS_AddValueRoot(JSContext *cx, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_AddStringRoot(JSContext *cx, JSString **rp);

extern JS_PUBLIC_API(JSBool)
JS_AddObjectRoot(JSContext *cx, JSObject **rp);

extern JS_PUBLIC_API(JSBool)
JS_AddGCThingRoot(JSContext *cx, void **rp);

#ifdef NAME_ALL_GC_ROOTS
#define JS_DEFINE_TO_TOKEN(def) #def
#define JS_DEFINE_TO_STRING(def) JS_DEFINE_TO_TOKEN(def)
#define JS_AddValueRoot(cx,vp) JS_AddNamedValueRoot((cx), (vp), (__FILE__ ":" JS_TOKEN_TO_STRING(__LINE__))
#define JS_AddStringRoot(cx,rp) JS_AddNamedStringRoot((cx), (rp), (__FILE__ ":" JS_TOKEN_TO_STRING(__LINE__))
#define JS_AddObjectRoot(cx,rp) JS_AddNamedObjectRoot((cx), (rp), (__FILE__ ":" JS_TOKEN_TO_STRING(__LINE__))
#define JS_AddGCThingRoot(cx,rp) JS_AddNamedGCThingRoot((cx), (rp), (__FILE__ ":" JS_TOKEN_TO_STRING(__LINE__))
#endif

extern JS_PUBLIC_API(JSBool)
JS_AddNamedValueRoot(JSContext *cx, jsval *vp, const char *name);

extern JS_PUBLIC_API(JSBool)
JS_AddNamedStringRoot(JSContext *cx, JSString **rp, const char *name);

extern JS_PUBLIC_API(JSBool)
JS_AddNamedObjectRoot(JSContext *cx, JSObject **rp, const char *name);

extern JS_PUBLIC_API(JSBool)
JS_AddNamedScriptRoot(JSContext *cx, JSScript **rp, const char *name);

extern JS_PUBLIC_API(JSBool)
JS_AddNamedGCThingRoot(JSContext *cx, void **rp, const char *name);

extern JS_PUBLIC_API(JSBool)
JS_RemoveValueRoot(JSContext *cx, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_RemoveStringRoot(JSContext *cx, JSString **rp);

extern JS_PUBLIC_API(JSBool)
JS_RemoveObjectRoot(JSContext *cx, JSObject **rp);

extern JS_PUBLIC_API(JSBool)
JS_RemoveScriptRoot(JSContext *cx, JSScript **rp);

extern JS_PUBLIC_API(JSBool)
JS_RemoveGCThingRoot(JSContext *cx, void **rp);

/* TODO: remove these APIs */

extern JS_FRIEND_API(JSBool)
js_AddRootRT(JSRuntime *rt, jsval *vp, const char *name);

extern JS_FRIEND_API(JSBool)
js_AddGCThingRootRT(JSRuntime *rt, void **rp, const char *name);

extern JS_FRIEND_API(JSBool)
js_RemoveRoot(JSRuntime *rt, void *rp);

/*
 * C-compatible version of the Anchor class. It should be called after the last
 * use of the variable it protects.
 */
extern JS_NEVER_INLINE JS_PUBLIC_API(void)
JS_AnchorPtr(void *p);

/*
 * This symbol may be used by embedders to detect the change from the old
 * JS_AddRoot(JSContext *, void *) APIs to the new ones above.
 */
#define JS_TYPED_ROOTING_API

/* Obsolete rooting APIs. */
#define JS_EnterLocalRootScope(cx) (JS_TRUE)
#define JS_LeaveLocalRootScope(cx) ((void) 0)
#define JS_LeaveLocalRootScopeWithResult(cx, rval) ((void) 0)
#define JS_ForgetLocalRoot(cx, thing) ((void) 0)

typedef enum JSGCRootType {
    JS_GC_ROOT_VALUE_PTR,
    JS_GC_ROOT_GCTHING_PTR
} JSGCRootType;

#ifdef DEBUG
extern JS_PUBLIC_API(void)
JS_DumpNamedRoots(JSRuntime *rt,
                  void (*dump)(const char *name, void *rp, JSGCRootType type, void *data),
                  void *data);
#endif

/*
 * Call JS_MapGCRoots to map the GC's roots table using map(rp, name, data).
 * The root is pointed at by rp; if the root is unnamed, name is null; data is
 * supplied from the third parameter to JS_MapGCRoots.
 *
 * The map function should return JS_MAP_GCROOT_REMOVE to cause the currently
 * enumerated root to be removed.  To stop enumeration, set JS_MAP_GCROOT_STOP
 * in the return value.  To keep on mapping, return JS_MAP_GCROOT_NEXT.  These
 * constants are flags; you can OR them together.
 *
 * This function acquires and releases rt's GC lock around the mapping of the
 * roots table, so the map function should run to completion in as few cycles
 * as possible.  Of course, map cannot call JS_GC, JS_MaybeGC, JS_BeginRequest,
 * or any JS API entry point that acquires locks, without double-tripping or
 * deadlocking on the GC lock.
 *
 * The JSGCRootType parameter indicates whether rp is a pointer to a Value
 * (which is obtained by '(Value *)rp') or a pointer to a GC-thing pointer
 * (which is obtained by '(void **)rp').
 *
 * JS_MapGCRoots returns the count of roots that were successfully mapped.
 */
#define JS_MAP_GCROOT_NEXT      0       /* continue mapping entries */
#define JS_MAP_GCROOT_STOP      1       /* stop mapping entries */
#define JS_MAP_GCROOT_REMOVE    2       /* remove and free the current entry */

typedef intN
(* JSGCRootMapFun)(void *rp, JSGCRootType type, const char *name, void *data);

extern JS_PUBLIC_API(uint32)
JS_MapGCRoots(JSRuntime *rt, JSGCRootMapFun map, void *data);

extern JS_PUBLIC_API(JSBool)
JS_LockGCThing(JSContext *cx, void *thing);

extern JS_PUBLIC_API(JSBool)
JS_LockGCThingRT(JSRuntime *rt, void *thing);

extern JS_PUBLIC_API(JSBool)
JS_UnlockGCThing(JSContext *cx, void *thing);

extern JS_PUBLIC_API(JSBool)
JS_UnlockGCThingRT(JSRuntime *rt, void *thing);

/*
 * Register externally maintained GC roots.
 *
 * traceOp: the trace operation. For each root the implementation should call
 *          JS_CallTracer whenever the root contains a traceable thing.
 * data:    the data argument to pass to each invocation of traceOp.
 */
extern JS_PUBLIC_API(void)
JS_SetExtraGCRootsTracer(JSRuntime *rt, JSTraceDataOp traceOp, void *data);

/*
 * JS_CallTracer API and related macros for implementors of JSTraceOp, to
 * enumerate all references to traceable things reachable via a property or
 * other strong ref identified for debugging purposes by name or index or
 * a naming callback.
 *
 * See the JSTraceOp typedef.
 */

/*
 * Use the following macros to check if a particular jsval is a traceable
 * thing and to extract the thing and its kind to pass to JS_CallTracer.
 */
static JS_ALWAYS_INLINE JSBool
JSVAL_IS_TRACEABLE(jsval v)
{
    return JSVAL_IS_TRACEABLE_IMPL(JSVAL_TO_IMPL(v));
}

static JS_ALWAYS_INLINE void *
JSVAL_TO_TRACEABLE(jsval v)
{
    return JSVAL_TO_GCTHING(v);
}

static JS_ALWAYS_INLINE JSGCTraceKind
JSVAL_TRACE_KIND(jsval v)
{
    JS_ASSERT(JSVAL_IS_GCTHING(v));
    return (JSGCTraceKind) JSVAL_TRACE_KIND_IMPL(JSVAL_TO_IMPL(v));
}

/*
 * Tracer callback, called for each traceable thing directly referenced by a
 * particular object or runtime structure. It is the callback responsibility
 * to ensure the traversal of the full object graph via calling eventually
 * JS_TraceChildren on the passed thing. In this case the callback must be
 * prepared to deal with cycles in the traversal graph.
 *
 * kind argument is one of JSTRACE_OBJECT, JSTRACE_STRING or a tag denoting
 * internal implementation-specific traversal kind. In the latter case the only
 * operations on thing that the callback can do is to call JS_TraceChildren or
 * DEBUG-only JS_PrintTraceThingInfo.
 *
 * If eagerlyTraceWeakMaps is true, when we trace a WeakMap visit all
 * of its mappings.  This should be used in cases where the tracer
 * wants to use the existing liveness of entries.
 */
typedef void
(* JSTraceCallback)(JSTracer *trc, void *thing, JSGCTraceKind kind);

struct JSTracer {
    JSContext           *context;
    JSTraceCallback     callback;
    JSTraceNamePrinter  debugPrinter;
    const void          *debugPrintArg;
    size_t              debugPrintIndex;
    JSBool              eagerlyTraceWeakMaps;
};

/*
 * The method to call on each reference to a traceable thing stored in a
 * particular JSObject or other runtime structure. With DEBUG defined the
 * caller before calling JS_CallTracer must initialize JSTracer fields
 * describing the reference using the macros below.
 */
extern JS_PUBLIC_API(void)
JS_CallTracer(JSTracer *trc, void *thing, JSGCTraceKind kind);

/*
 * Set debugging information about a reference to a traceable thing to prepare
 * for the following call to JS_CallTracer.
 *
 * When printer is null, arg must be const char * or char * C string naming
 * the reference and index must be either (size_t)-1 indicating that the name
 * alone describes the reference or it must be an index into some array vector
 * that stores the reference.
 *
 * When printer callback is not null, the arg and index arguments are
 * available to the callback as debugPrintArg and debugPrintIndex fields
 * of JSTracer.
 *
 * The storage for name or callback's arguments needs to live only until
 * the following call to JS_CallTracer returns.
 */
#ifdef DEBUG
# define JS_SET_TRACING_DETAILS(trc, printer, arg, index)                     \
    JS_BEGIN_MACRO                                                            \
        (trc)->debugPrinter = (printer);                                      \
        (trc)->debugPrintArg = (arg);                                         \
        (trc)->debugPrintIndex = (index);                                     \
    JS_END_MACRO
#else
# define JS_SET_TRACING_DETAILS(trc, printer, arg, index)                     \
    JS_BEGIN_MACRO                                                            \
    JS_END_MACRO
#endif

/*
 * Convenience macro to describe the argument of JS_CallTracer using C string
 * and index.
 */
# define JS_SET_TRACING_INDEX(trc, name, index)                               \
    JS_SET_TRACING_DETAILS(trc, NULL, name, index)

/*
 * Convenience macro to describe the argument of JS_CallTracer using C string.
 */
# define JS_SET_TRACING_NAME(trc, name)                                       \
    JS_SET_TRACING_DETAILS(trc, NULL, name, (size_t)-1)

/*
 * Convenience macro to invoke JS_CallTracer using C string as the name for
 * the reference to a traceable thing.
 */
# define JS_CALL_TRACER(trc, thing, kind, name)                               \
    JS_BEGIN_MACRO                                                            \
        JS_SET_TRACING_NAME(trc, name);                                       \
        JS_CallTracer((trc), (thing), (kind));                                \
    JS_END_MACRO

/*
 * Convenience macros to invoke JS_CallTracer when jsval represents a
 * reference to a traceable thing.
 */
#define JS_CALL_VALUE_TRACER(trc, val, name)                                  \
    JS_BEGIN_MACRO                                                            \
        if (JSVAL_IS_TRACEABLE(val)) {                                        \
            JS_CALL_TRACER((trc), JSVAL_TO_GCTHING(val),                      \
                           JSVAL_TRACE_KIND(val), name);                      \
        }                                                                     \
    JS_END_MACRO

#define JS_CALL_OBJECT_TRACER(trc, object, name)                              \
    JS_BEGIN_MACRO                                                            \
        JSObject *obj_ = (object);                                            \
        JS_ASSERT(obj_);                                                      \
        JS_CALL_TRACER((trc), obj_, JSTRACE_OBJECT, name);                    \
    JS_END_MACRO

#define JS_CALL_STRING_TRACER(trc, string, name)                              \
    JS_BEGIN_MACRO                                                            \
        JSString *str_ = (string);                                            \
        JS_ASSERT(str_);                                                      \
        JS_CALL_TRACER((trc), str_, JSTRACE_STRING, name);                    \
    JS_END_MACRO

/*
 * API for JSTraceCallback implementations.
 */
# define JS_TRACER_INIT(trc, cx_, callback_)                                  \
    JS_BEGIN_MACRO                                                            \
        (trc)->context = (cx_);                                               \
        (trc)->callback = (callback_);                                        \
        (trc)->debugPrinter = NULL;                                           \
        (trc)->debugPrintArg = NULL;                                          \
        (trc)->debugPrintIndex = (size_t)-1;                                  \
        (trc)->eagerlyTraceWeakMaps = JS_TRUE;                                \
    JS_END_MACRO

extern JS_PUBLIC_API(void)
JS_TraceChildren(JSTracer *trc, void *thing, JSGCTraceKind kind);

extern JS_PUBLIC_API(void)
JS_TraceRuntime(JSTracer *trc);

#ifdef DEBUG

extern JS_PUBLIC_API(void)
JS_PrintTraceThingInfo(char *buf, size_t bufsize, JSTracer *trc,
                       void *thing, JSGCTraceKind kind, JSBool includeDetails);

extern JS_PUBLIC_API(const char *)
JS_GetTraceEdgeName(JSTracer *trc, char *buffer, int bufferSize);

/*
 * DEBUG-only method to dump the object graph of heap-allocated things.
 *
 * fp:              file for the dump output.
 * start:           when non-null, dump only things reachable from start
 *                  thing. Otherwise dump all things reachable from the
 *                  runtime roots.
 * startKind:       trace kind of start if start is not null. Must be
 *                  JSTRACE_OBJECT when start is null.
 * thingToFind:     dump only paths in the object graph leading to thingToFind
 *                  when non-null.
 * maxDepth:        the upper bound on the number of edges to descend from the
 *                  graph roots.
 * thingToIgnore:   thing to ignore during the graph traversal when non-null.
 */
extern JS_PUBLIC_API(JSBool)
JS_DumpHeap(JSContext *cx, FILE *fp, void* startThing, JSGCTraceKind kind,
            void *thingToFind, size_t maxDepth, void *thingToIgnore);

#endif

/*
 * Garbage collector API.
 */
extern JS_PUBLIC_API(void)
JS_GC(JSContext *cx);

extern JS_PUBLIC_API(void)
JS_CompartmentGC(JSContext *cx, JSCompartment *comp);

extern JS_PUBLIC_API(void)
JS_MaybeGC(JSContext *cx);

extern JS_PUBLIC_API(JSGCCallback)
JS_SetGCCallback(JSContext *cx, JSGCCallback cb);

extern JS_PUBLIC_API(JSGCCallback)
JS_SetGCCallbackRT(JSRuntime *rt, JSGCCallback cb);

extern JS_PUBLIC_API(JSBool)
JS_IsGCMarkingTracer(JSTracer *trc);

extern JS_PUBLIC_API(JSBool)
JS_IsAboutToBeFinalized(JSContext *cx, void *thing);

typedef enum JSGCParamKey {
    /* Maximum nominal heap before last ditch GC. */
    JSGC_MAX_BYTES          = 0,

    /* Number of JS_malloc bytes before last ditch GC. */
    JSGC_MAX_MALLOC_BYTES   = 1,

    /* Hoard stackPools for this long, in ms, default is 30 seconds. */
    JSGC_STACKPOOL_LIFESPAN = 2,

    /* Amount of bytes allocated by the GC. */
    JSGC_BYTES = 3,

    /* Number of times when GC was invoked. */
    JSGC_NUMBER = 4,

    /* Max size of the code cache in bytes. */
    JSGC_MAX_CODE_CACHE_BYTES = 5,

    /* Select GC mode. */
    JSGC_MODE = 6,

    /* Number of cached empty GC chunks. */
    JSGC_UNUSED_CHUNKS = 7,

    /* Total number of allocated GC chunks. */
    JSGC_TOTAL_CHUNKS = 8
} JSGCParamKey;

typedef enum JSGCMode {
    /* Perform only global GCs. */
    JSGC_MODE_GLOBAL = 0,

    /* Perform per-compartment GCs until too much garbage has accumulated. */
    JSGC_MODE_COMPARTMENT = 1
} JSGCMode;

extern JS_PUBLIC_API(void)
JS_SetGCParameter(JSRuntime *rt, JSGCParamKey key, uint32 value);

extern JS_PUBLIC_API(uint32)
JS_GetGCParameter(JSRuntime *rt, JSGCParamKey key);

extern JS_PUBLIC_API(void)
JS_SetGCParameterForThread(JSContext *cx, JSGCParamKey key, uint32 value);

extern JS_PUBLIC_API(uint32)
JS_GetGCParameterForThread(JSContext *cx, JSGCParamKey key);

/*
 * Flush the code cache for the current thread. The operation might be
 * delayed if the cache cannot be flushed currently because native
 * code is currently executing.
 */

extern JS_PUBLIC_API(void)
JS_FlushCaches(JSContext *cx);

/*
 * Add a finalizer for external strings created by JS_NewExternalString (see
 * below) using a type-code returned from this function, and that understands
 * how to free or release the memory pointed at by JS_GetStringChars(str).
 *
 * Return a nonnegative type index if there is room for finalizer in the
 * global GC finalizers table, else return -1.  If the engine is compiled
 * JS_THREADSAFE and used in a multi-threaded environment, this function must
 * be invoked on the primordial thread only, at startup -- or else the entire
 * program must single-thread itself while loading a module that calls this
 * function.
 */
extern JS_PUBLIC_API(intN)
JS_AddExternalStringFinalizer(JSStringFinalizeOp finalizer);

/*
 * Remove finalizer from the global GC finalizers table, returning its type
 * code if found, -1 if not found.
 *
 * As with JS_AddExternalStringFinalizer, there is a threading restriction
 * if you compile the engine JS_THREADSAFE: this function may be called for a
 * given finalizer pointer on only one thread; different threads may call to
 * remove distinct finalizers safely.
 *
 * You must ensure that all strings with finalizer's type have been collected
 * before calling this function.  Otherwise, string data will be leaked by the
 * GC, for want of a finalizer to call.
 */
extern JS_PUBLIC_API(intN)
JS_RemoveExternalStringFinalizer(JSStringFinalizeOp finalizer);

/*
 * Create a new JSString whose chars member refers to external memory, i.e.,
 * memory requiring type-specific finalization.  The type code must be a
 * nonnegative return value from JS_AddExternalStringFinalizer.
 */
extern JS_PUBLIC_API(JSString *)
JS_NewExternalString(JSContext *cx, const jschar *chars, size_t length, intN type);

/*
 * Like JS_NewExternalString, except that 'closure' can be retrieved later via
 * JS_GetExternalStringClosure. This closure data is a black blox to the JS
 * engine and may be used by the embedding to associate extra data with an
 * external string. E.g., an embedding may want to associate a pointer to the
 * object that owns the chars of an external string so that, when this external
 * string is finalized, the owner object can be deleted.
 */
extern JS_PUBLIC_API(JSString *)
JS_NewExternalStringWithClosure(JSContext *cx, const jschar *chars, size_t length,
                                intN type, void *closure);

/*
 * Return whether 'str' was created with JS_NewExternalString or
 * JS_NewExternalStringWithClosure.
 */
extern JS_PUBLIC_API(JSBool)
JS_IsExternalString(JSContext *cx, JSString *str);

/*
 * Return the 'closure' arg passed to JS_NewExternalStringWithClosure or NULL
 * if the external string was created via JS_NewExternalString.
 */
extern JS_PUBLIC_API(void *)
JS_GetExternalStringClosure(JSContext *cx, JSString *str);

/*
 * Deprecated. Use JS_SetNativeStackQuoata instead.
 */
extern JS_PUBLIC_API(void)
JS_SetThreadStackLimit(JSContext *cx, jsuword limitAddr);

/*
 * Set the size of the native stack that should not be exceed. To disable
 * stack size checking pass 0.
 */
extern JS_PUBLIC_API(void)
JS_SetNativeStackQuota(JSContext *cx, size_t stackSize);

/************************************************************************/

/*
 * Classes, objects, and properties.
 */
typedef void (*JSClassInternal)();

struct JSClass {
    const char          *name;
    uint32              flags;

    /* Mandatory non-null function pointer members. */
    JSPropertyOp        addProperty;
    JSPropertyOp        delProperty;
    JSPropertyOp        getProperty;
    JSStrictPropertyOp  setProperty;
    JSEnumerateOp       enumerate;
    JSResolveOp         resolve;
    JSConvertOp         convert;
    JSFinalizeOp        finalize;

    /* Optionally non-null members start here. */
    JSClassInternal     reserved0;
    JSCheckAccessOp     checkAccess;
    JSNative            call;
    JSNative            construct;
    JSXDRObjectOp       xdrObject;
    JSHasInstanceOp     hasInstance;
    JSTraceOp           trace;

    JSClassInternal     reserved1;
    void                *reserved[40];
};

#define JSCLASS_HAS_PRIVATE             (1<<0)  /* objects have private slot */
#define JSCLASS_NEW_ENUMERATE           (1<<1)  /* has JSNewEnumerateOp hook */
#define JSCLASS_NEW_RESOLVE             (1<<2)  /* has JSNewResolveOp hook */
#define JSCLASS_PRIVATE_IS_NSISUPPORTS  (1<<3)  /* private is (nsISupports *) */
#define JSCLASS_CONCURRENT_FINALIZER    (1<<4)  /* finalize is called on background thread */
#define JSCLASS_NEW_RESOLVE_GETS_START  (1<<5)  /* JSNewResolveOp gets starting
                                                   object in prototype chain
                                                   passed in via *objp in/out
                                                   parameter */
#define JSCLASS_CONSTRUCT_PROTOTYPE     (1<<6)  /* call constructor on class
                                                   prototype */
#define JSCLASS_DOCUMENT_OBSERVER       (1<<7)  /* DOM document observer */

/*
 * To reserve slots fetched and stored via JS_Get/SetReservedSlot, bitwise-or
 * JSCLASS_HAS_RESERVED_SLOTS(n) into the initializer for JSClass.flags, where
 * n is a constant in [1, 255].  Reserved slots are indexed from 0 to n-1.
 */
#define JSCLASS_RESERVED_SLOTS_SHIFT    8       /* room for 8 flags below */
#define JSCLASS_RESERVED_SLOTS_WIDTH    8       /* and 16 above this field */
#define JSCLASS_RESERVED_SLOTS_MASK     JS_BITMASK(JSCLASS_RESERVED_SLOTS_WIDTH)
#define JSCLASS_HAS_RESERVED_SLOTS(n)   (((n) & JSCLASS_RESERVED_SLOTS_MASK)  \
                                         << JSCLASS_RESERVED_SLOTS_SHIFT)
#define JSCLASS_RESERVED_SLOTS(clasp)   (((clasp)->flags                      \
                                          >> JSCLASS_RESERVED_SLOTS_SHIFT)    \
                                         & JSCLASS_RESERVED_SLOTS_MASK)

#define JSCLASS_HIGH_FLAGS_SHIFT        (JSCLASS_RESERVED_SLOTS_SHIFT +       \
                                         JSCLASS_RESERVED_SLOTS_WIDTH)

#define JSCLASS_INTERNAL_FLAG1          (1<<(JSCLASS_HIGH_FLAGS_SHIFT+0))
#define JSCLASS_IS_ANONYMOUS            (1<<(JSCLASS_HIGH_FLAGS_SHIFT+1))
#define JSCLASS_IS_GLOBAL               (1<<(JSCLASS_HIGH_FLAGS_SHIFT+2))
#define JSCLASS_INTERNAL_FLAG2          (1<<(JSCLASS_HIGH_FLAGS_SHIFT+3))
#define JSCLASS_INTERNAL_FLAG3          (1<<(JSCLASS_HIGH_FLAGS_SHIFT+4))

/* Indicate whether the proto or ctor should be frozen. */
#define JSCLASS_FREEZE_PROTO            (1<<(JSCLASS_HIGH_FLAGS_SHIFT+5))
#define JSCLASS_FREEZE_CTOR             (1<<(JSCLASS_HIGH_FLAGS_SHIFT+6))

#define JSCLASS_XPCONNECT_GLOBAL        (1<<(JSCLASS_HIGH_FLAGS_SHIFT+7))

/* Global flags. */
#define JSGLOBAL_FLAGS_CLEARED          0x1

/*
 * ECMA-262 requires that most constructors used internally create objects
 * with "the original Foo.prototype value" as their [[Prototype]] (__proto__)
 * member initial value.  The "original ... value" verbiage is there because
 * in ECMA-262, global properties naming class objects are read/write and
 * deleteable, for the most part.
 *
 * Implementing this efficiently requires that global objects have classes
 * with the following flags. Failure to use JSCLASS_GLOBAL_FLAGS was
 * prevously allowed, but is now an ES5 violation and thus unsupported.
 */
#define JSCLASS_GLOBAL_SLOT_COUNT      (JSProto_LIMIT * 3 + 8)
#define JSCLASS_GLOBAL_FLAGS_WITH_SLOTS(n)                                    \
    (JSCLASS_IS_GLOBAL | JSCLASS_HAS_RESERVED_SLOTS(JSCLASS_GLOBAL_SLOT_COUNT + (n)))
#define JSCLASS_GLOBAL_FLAGS                                                  \
    JSCLASS_GLOBAL_FLAGS_WITH_SLOTS(0)
#define JSCLASS_HAS_GLOBAL_FLAG_AND_SLOTS(clasp)                              \
  (((clasp)->flags & JSCLASS_IS_GLOBAL)                                       \
   && JSCLASS_RESERVED_SLOTS(clasp) >= JSCLASS_GLOBAL_SLOT_COUNT)

/* Fast access to the original value of each standard class's prototype. */
#define JSCLASS_CACHED_PROTO_SHIFT      (JSCLASS_HIGH_FLAGS_SHIFT + 8)
#define JSCLASS_CACHED_PROTO_WIDTH      8
#define JSCLASS_CACHED_PROTO_MASK       JS_BITMASK(JSCLASS_CACHED_PROTO_WIDTH)
#define JSCLASS_HAS_CACHED_PROTO(key)   ((key) << JSCLASS_CACHED_PROTO_SHIFT)
#define JSCLASS_CACHED_PROTO_KEY(clasp) ((JSProtoKey)                         \
                                         (((clasp)->flags                     \
                                           >> JSCLASS_CACHED_PROTO_SHIFT)     \
                                          & JSCLASS_CACHED_PROTO_MASK))

/* Initializer for unused members of statically initialized JSClass structs. */
#define JSCLASS_NO_INTERNAL_MEMBERS     0,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
#define JSCLASS_NO_OPTIONAL_MEMBERS     0,0,0,0,0,0,0,JSCLASS_NO_INTERNAL_MEMBERS

struct JSIdArray {
    jsint length;
    jsid  vector[1];    /* actually, length jsid words */
};

extern JS_PUBLIC_API(void)
JS_DestroyIdArray(JSContext *cx, JSIdArray *ida);

extern JS_PUBLIC_API(JSBool)
JS_ValueToId(JSContext *cx, jsval v, jsid *idp);

extern JS_PUBLIC_API(JSBool)
JS_IdToValue(JSContext *cx, jsid id, jsval *vp);

/*
 * JSNewResolveOp flag bits.
 */
#define JSRESOLVE_QUALIFIED     0x01    /* resolve a qualified property id */
#define JSRESOLVE_ASSIGNING     0x02    /* resolve on the left of assignment */
#define JSRESOLVE_DETECTING     0x04    /* 'if (o.p)...' or '(o.p) ?...:...' */
#define JSRESOLVE_DECLARING     0x08    /* var, const, or function prolog op */
#define JSRESOLVE_CLASSNAME     0x10    /* class name used when constructing */
#define JSRESOLVE_WITH          0x20    /* resolve inside a with statement */

/*
 * Invoke the [[DefaultValue]] hook (see ES5 8.6.2) with the provided hint on
 * the specified object, computing a primitive default value for the object.
 * The hint must be JSTYPE_STRING, JSTYPE_NUMBER, or JSTYPE_VOID (no hint).  On
 * success the resulting value is stored in *vp.
 */
extern JS_PUBLIC_API(JSBool)
JS_DefaultValue(JSContext *cx, JSObject *obj, JSType hint, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_PropertyStub(JSContext *cx, JSObject *obj, jsid id, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_StrictPropertyStub(JSContext *cx, JSObject *obj, jsid id, JSBool strict, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_EnumerateStub(JSContext *cx, JSObject *obj);

extern JS_PUBLIC_API(JSBool)
JS_ResolveStub(JSContext *cx, JSObject *obj, jsid id);

extern JS_PUBLIC_API(JSBool)
JS_ConvertStub(JSContext *cx, JSObject *obj, JSType type, jsval *vp);

extern JS_PUBLIC_API(void)
JS_FinalizeStub(JSContext *cx, JSObject *obj);

struct JSConstDoubleSpec {
    jsdouble        dval;
    const char      *name;
    uint8           flags;
    uint8           spare[3];
};

/*
 * To define an array element rather than a named property member, cast the
 * element's index to (const char *) and initialize name with it, and set the
 * JSPROP_INDEX bit in flags.
 */
struct JSPropertySpec {
    const char            *name;
    int8                  tinyid;
    uint8                 flags;
    JSPropertyOp          getter;
    JSStrictPropertyOp    setter;
};

struct JSFunctionSpec {
    const char      *name;
    JSNative        call;
    uint16          nargs;
    uint16          flags;
};

/*
 * Terminating sentinel initializer to put at the end of a JSFunctionSpec array
 * that's passed to JS_DefineFunctions or JS_InitClass.
 */
#define JS_FS_END JS_FS(NULL,NULL,0,0)

/*
 * Initializer macros for a JSFunctionSpec array element. JS_FN (whose name
 * pays homage to the old JSNative/JSFastNative split) simply adds the flag
 * JSFUN_STUB_GSOPS.
 */
#define JS_FS(name,call,nargs,flags)                                          \
    {name, call, nargs, flags}
#define JS_FN(name,call,nargs,flags)                                          \
    {name, call, nargs, (flags) | JSFUN_STUB_GSOPS}

extern JS_PUBLIC_API(JSObject *)
JS_InitClass(JSContext *cx, JSObject *obj, JSObject *parent_proto,
             JSClass *clasp, JSNative constructor, uintN nargs,
             JSPropertySpec *ps, JSFunctionSpec *fs,
             JSPropertySpec *static_ps, JSFunctionSpec *static_fs);

#ifdef JS_THREADSAFE
extern JS_PUBLIC_API(JSClass *)
JS_GetClass(JSContext *cx, JSObject *obj);

#define JS_GET_CLASS(cx,obj) JS_GetClass(cx, obj)
#else
extern JS_PUBLIC_API(JSClass *)
JS_GetClass(JSObject *obj);

#define JS_GET_CLASS(cx,obj) JS_GetClass(obj)
#endif

extern JS_PUBLIC_API(JSBool)
JS_InstanceOf(JSContext *cx, JSObject *obj, JSClass *clasp, jsval *argv);

extern JS_PUBLIC_API(JSBool)
JS_HasInstance(JSContext *cx, JSObject *obj, jsval v, JSBool *bp);

extern JS_PUBLIC_API(void *)
JS_GetPrivate(JSContext *cx, JSObject *obj);

extern JS_PUBLIC_API(JSBool)
JS_SetPrivate(JSContext *cx, JSObject *obj, void *data);

extern JS_PUBLIC_API(void *)
JS_GetInstancePrivate(JSContext *cx, JSObject *obj, JSClass *clasp,
                      jsval *argv);

extern JS_PUBLIC_API(JSObject *)
JS_GetPrototype(JSContext *cx, JSObject *obj);

extern JS_PUBLIC_API(JSBool)
JS_SetPrototype(JSContext *cx, JSObject *obj, JSObject *proto);

extern JS_PUBLIC_API(JSObject *)
JS_GetParent(JSContext *cx, JSObject *obj);

extern JS_PUBLIC_API(JSBool)
JS_SetParent(JSContext *cx, JSObject *obj, JSObject *parent);

extern JS_PUBLIC_API(JSObject *)
JS_GetConstructor(JSContext *cx, JSObject *proto);

extern JS_PUBLIC_API(JSBool)
JS_GetClassPrototype(JSContext *cx, JSProtoKey key, JSObject **objp);

extern JS_PUBLIC_API(JSProtoKey)
JS_IdentifyClassPrototype(JSContext *cx, JSObject *obj);

/*
 * Get a unique identifier for obj, good for the lifetime of obj (even if it
 * is moved by a copying GC).  Return false on failure (likely out of memory),
 * and true with *idp containing the unique id on success.
 */
extern JS_PUBLIC_API(JSBool)
JS_GetObjectId(JSContext *cx, JSObject *obj, jsid *idp);

extern JS_PUBLIC_API(JSObject *)
JS_NewGlobalObject(JSContext *cx, JSClass *clasp);

extern JS_PUBLIC_API(JSObject *)
JS_NewCompartmentAndGlobalObject(JSContext *cx, JSClass *clasp, JSPrincipals *principals);

extern JS_PUBLIC_API(JSObject *)
JS_NewObject(JSContext *cx, JSClass *clasp, JSObject *proto, JSObject *parent);

/* Queries the [[Extensible]] property of the object. */
extern JS_PUBLIC_API(JSBool)
JS_IsExtensible(JSObject *obj);

extern JS_PUBLIC_API(JSBool)
JS_IsNative(JSObject *obj);

/*
 * Unlike JS_NewObject, JS_NewObjectWithGivenProto does not compute a default
 * proto if proto's actual parameter value is null.
 */
extern JS_PUBLIC_API(JSObject *)
JS_NewObjectWithGivenProto(JSContext *cx, JSClass *clasp, JSObject *proto,
                           JSObject *parent);

/*
 * Freeze obj, and all objects it refers to, recursively. This will not recurse
 * through non-extensible objects, on the assumption that those are already
 * deep-frozen.
 */
extern JS_PUBLIC_API(JSBool)
JS_DeepFreezeObject(JSContext *cx, JSObject *obj);

/*
 * Freezes an object; see ES5's Object.freeze(obj) method.
 */
extern JS_PUBLIC_API(JSBool)
JS_FreezeObject(JSContext *cx, JSObject *obj);

extern JS_PUBLIC_API(JSObject *)
JS_ConstructObject(JSContext *cx, JSClass *clasp, JSObject *proto,
                   JSObject *parent);

extern JS_PUBLIC_API(JSObject *)
JS_ConstructObjectWithArguments(JSContext *cx, JSClass *clasp, JSObject *proto,
                                JSObject *parent, uintN argc, jsval *argv);

extern JS_PUBLIC_API(JSObject *)
JS_New(JSContext *cx, JSObject *ctor, uintN argc, jsval *argv);

extern JS_PUBLIC_API(JSObject *)
JS_DefineObject(JSContext *cx, JSObject *obj, const char *name, JSClass *clasp,
                JSObject *proto, uintN attrs);

extern JS_PUBLIC_API(JSBool)
JS_DefineConstDoubles(JSContext *cx, JSObject *obj, JSConstDoubleSpec *cds);

extern JS_PUBLIC_API(JSBool)
JS_DefineProperties(JSContext *cx, JSObject *obj, JSPropertySpec *ps);

extern JS_PUBLIC_API(JSBool)
JS_DefineProperty(JSContext *cx, JSObject *obj, const char *name, jsval value,
                  JSPropertyOp getter, JSStrictPropertyOp setter, uintN attrs);

extern JS_PUBLIC_API(JSBool)
JS_DefinePropertyById(JSContext *cx, JSObject *obj, jsid id, jsval value,
                      JSPropertyOp getter, JSStrictPropertyOp setter, uintN attrs);

extern JS_PUBLIC_API(JSBool)
JS_DefineOwnProperty(JSContext *cx, JSObject *obj, jsid id, jsval descriptor, JSBool *bp);

/*
 * Determine the attributes (JSPROP_* flags) of a property on a given object.
 *
 * If the object does not have a property by that name, *foundp will be
 * JS_FALSE and the value of *attrsp is undefined.
 */
extern JS_PUBLIC_API(JSBool)
JS_GetPropertyAttributes(JSContext *cx, JSObject *obj, const char *name,
                         uintN *attrsp, JSBool *foundp);

/*
 * The same, but if the property is native, return its getter and setter via
 * *getterp and *setterp, respectively (and only if the out parameter pointer
 * is not null).
 */
extern JS_PUBLIC_API(JSBool)
JS_GetPropertyAttrsGetterAndSetter(JSContext *cx, JSObject *obj,
                                   const char *name,
                                   uintN *attrsp, JSBool *foundp,
                                   JSPropertyOp *getterp,
                                   JSStrictPropertyOp *setterp);

extern JS_PUBLIC_API(JSBool)
JS_GetPropertyAttrsGetterAndSetterById(JSContext *cx, JSObject *obj,
                                       jsid id,
                                       uintN *attrsp, JSBool *foundp,
                                       JSPropertyOp *getterp,
                                       JSStrictPropertyOp *setterp);

/*
 * Set the attributes of a property on a given object.
 *
 * If the object does not have a property by that name, *foundp will be
 * JS_FALSE and nothing will be altered.
 */
extern JS_PUBLIC_API(JSBool)
JS_SetPropertyAttributes(JSContext *cx, JSObject *obj, const char *name,
                         uintN attrs, JSBool *foundp);

extern JS_PUBLIC_API(JSBool)
JS_DefinePropertyWithTinyId(JSContext *cx, JSObject *obj, const char *name,
                            int8 tinyid, jsval value,
                            JSPropertyOp getter, JSStrictPropertyOp setter,
                            uintN attrs);

extern JS_PUBLIC_API(JSBool)
JS_AlreadyHasOwnProperty(JSContext *cx, JSObject *obj, const char *name,
                         JSBool *foundp);

extern JS_PUBLIC_API(JSBool)
JS_AlreadyHasOwnPropertyById(JSContext *cx, JSObject *obj, jsid id,
                             JSBool *foundp);

extern JS_PUBLIC_API(JSBool)
JS_HasProperty(JSContext *cx, JSObject *obj, const char *name, JSBool *foundp);

extern JS_PUBLIC_API(JSBool)
JS_HasPropertyById(JSContext *cx, JSObject *obj, jsid id, JSBool *foundp);

extern JS_PUBLIC_API(JSBool)
JS_LookupProperty(JSContext *cx, JSObject *obj, const char *name, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_LookupPropertyById(JSContext *cx, JSObject *obj, jsid id, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_LookupPropertyWithFlags(JSContext *cx, JSObject *obj, const char *name,
                           uintN flags, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_LookupPropertyWithFlagsById(JSContext *cx, JSObject *obj, jsid id,
                               uintN flags, JSObject **objp, jsval *vp);

struct JSPropertyDescriptor {
    JSObject           *obj;
    uintN              attrs;
    JSPropertyOp       getter;
    JSStrictPropertyOp setter;
    jsval              value;
    uintN              shortid;
};

/*
 * Like JS_GetPropertyAttrsGetterAndSetterById but will return a property on
 * an object on the prototype chain (returned in objp). If data->obj is null,
 * then this property was not found on the prototype chain.
 */
extern JS_PUBLIC_API(JSBool)
JS_GetPropertyDescriptorById(JSContext *cx, JSObject *obj, jsid id, uintN flags,
                             JSPropertyDescriptor *desc);

extern JS_PUBLIC_API(JSBool)
JS_GetOwnPropertyDescriptor(JSContext *cx, JSObject *obj, jsid id, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_GetProperty(JSContext *cx, JSObject *obj, const char *name, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_GetPropertyDefault(JSContext *cx, JSObject *obj, const char *name, jsval def, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_GetPropertyById(JSContext *cx, JSObject *obj, jsid id, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_GetPropertyByIdDefault(JSContext *cx, JSObject *obj, jsid id, jsval def, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_ForwardGetPropertyTo(JSContext *cx, JSObject *obj, jsid id, JSObject *onBehalfOf, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_GetMethodById(JSContext *cx, JSObject *obj, jsid id, JSObject **objp,
                 jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_GetMethod(JSContext *cx, JSObject *obj, const char *name, JSObject **objp,
             jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_SetProperty(JSContext *cx, JSObject *obj, const char *name, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_SetPropertyById(JSContext *cx, JSObject *obj, jsid id, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_DeleteProperty(JSContext *cx, JSObject *obj, const char *name);

extern JS_PUBLIC_API(JSBool)
JS_DeleteProperty2(JSContext *cx, JSObject *obj, const char *name,
                   jsval *rval);

extern JS_PUBLIC_API(JSBool)
JS_DeletePropertyById(JSContext *cx, JSObject *obj, jsid id);

extern JS_PUBLIC_API(JSBool)
JS_DeletePropertyById2(JSContext *cx, JSObject *obj, jsid id, jsval *rval);

extern JS_PUBLIC_API(JSBool)
JS_DefineUCProperty(JSContext *cx, JSObject *obj,
                    const jschar *name, size_t namelen, jsval value,
                    JSPropertyOp getter, JSStrictPropertyOp setter,
                    uintN attrs);

/*
 * Determine the attributes (JSPROP_* flags) of a property on a given object.
 *
 * If the object does not have a property by that name, *foundp will be
 * JS_FALSE and the value of *attrsp is undefined.
 */
extern JS_PUBLIC_API(JSBool)
JS_GetUCPropertyAttributes(JSContext *cx, JSObject *obj,
                           const jschar *name, size_t namelen,
                           uintN *attrsp, JSBool *foundp);

/*
 * The same, but if the property is native, return its getter and setter via
 * *getterp and *setterp, respectively (and only if the out parameter pointer
 * is not null).
 */
extern JS_PUBLIC_API(JSBool)
JS_GetUCPropertyAttrsGetterAndSetter(JSContext *cx, JSObject *obj,
                                     const jschar *name, size_t namelen,
                                     uintN *attrsp, JSBool *foundp,
                                     JSPropertyOp *getterp,
                                     JSStrictPropertyOp *setterp);

/*
 * Set the attributes of a property on a given object.
 *
 * If the object does not have a property by that name, *foundp will be
 * JS_FALSE and nothing will be altered.
 */
extern JS_PUBLIC_API(JSBool)
JS_SetUCPropertyAttributes(JSContext *cx, JSObject *obj,
                           const jschar *name, size_t namelen,
                           uintN attrs, JSBool *foundp);


extern JS_PUBLIC_API(JSBool)
JS_DefineUCPropertyWithTinyId(JSContext *cx, JSObject *obj,
                              const jschar *name, size_t namelen,
                              int8 tinyid, jsval value,
                              JSPropertyOp getter, JSStrictPropertyOp setter,
                              uintN attrs);

extern JS_PUBLIC_API(JSBool)
JS_AlreadyHasOwnUCProperty(JSContext *cx, JSObject *obj, const jschar *name,
                           size_t namelen, JSBool *foundp);

extern JS_PUBLIC_API(JSBool)
JS_HasUCProperty(JSContext *cx, JSObject *obj,
                 const jschar *name, size_t namelen,
                 JSBool *vp);

extern JS_PUBLIC_API(JSBool)
JS_LookupUCProperty(JSContext *cx, JSObject *obj,
                    const jschar *name, size_t namelen,
                    jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_GetUCProperty(JSContext *cx, JSObject *obj,
                 const jschar *name, size_t namelen,
                 jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_SetUCProperty(JSContext *cx, JSObject *obj,
                 const jschar *name, size_t namelen,
                 jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_DeleteUCProperty2(JSContext *cx, JSObject *obj,
                     const jschar *name, size_t namelen,
                     jsval *rval);

extern JS_PUBLIC_API(JSObject *)
JS_NewArrayObject(JSContext *cx, jsint length, jsval *vector);

extern JS_PUBLIC_API(JSBool)
JS_IsArrayObject(JSContext *cx, JSObject *obj);

extern JS_PUBLIC_API(JSBool)
JS_GetArrayLength(JSContext *cx, JSObject *obj, jsuint *lengthp);

extern JS_PUBLIC_API(JSBool)
JS_SetArrayLength(JSContext *cx, JSObject *obj, jsuint length);

extern JS_PUBLIC_API(JSBool)
JS_DefineElement(JSContext *cx, JSObject *obj, uint32 index, jsval value,
                 JSPropertyOp getter, JSStrictPropertyOp setter, uintN attrs);

extern JS_PUBLIC_API(JSBool)
JS_AlreadyHasOwnElement(JSContext *cx, JSObject *obj, uint32 index, JSBool *foundp);

extern JS_PUBLIC_API(JSBool)
JS_HasElement(JSContext *cx, JSObject *obj, uint32 index, JSBool *foundp);

extern JS_PUBLIC_API(JSBool)
JS_LookupElement(JSContext *cx, JSObject *obj, uint32 index, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_GetElement(JSContext *cx, JSObject *obj, uint32 index, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_ForwardGetElementTo(JSContext *cx, JSObject *obj, uint32 index, JSObject *onBehalfOf, jsval *vp);

/*
 * Get the property with name given by |index|, if it has one.  If
 * not, |*present| will be set to false and the value of |vp| must not
 * be relied on.
 */
extern JS_PUBLIC_API(JSBool)
JS_GetElementIfPresent(JSContext *cx, JSObject *obj, uint32 index, JSObject *onBehalfOf,
                       jsval *vp, JSBool* present);

extern JS_PUBLIC_API(JSBool)
JS_SetElement(JSContext *cx, JSObject *obj, uint32 index, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_DeleteElement(JSContext *cx, JSObject *obj, uint32 index);

extern JS_PUBLIC_API(JSBool)
JS_DeleteElement2(JSContext *cx, JSObject *obj, uint32 index, jsval *rval);

extern JS_PUBLIC_API(void)
JS_ClearScope(JSContext *cx, JSObject *obj);

extern JS_PUBLIC_API(JSIdArray *)
JS_Enumerate(JSContext *cx, JSObject *obj);

/*
 * Create an object to iterate over enumerable properties of obj, in arbitrary
 * property definition order.  NB: This differs from longstanding for..in loop
 * order, which uses order of property definition in obj.
 */
extern JS_PUBLIC_API(JSObject *)
JS_NewPropertyIterator(JSContext *cx, JSObject *obj);

/*
 * Return true on success with *idp containing the id of the next enumerable
 * property to visit using iterobj, or JSID_IS_VOID if there is no such property
 * left to visit.  Return false on error.
 */
extern JS_PUBLIC_API(JSBool)
JS_NextProperty(JSContext *cx, JSObject *iterobj, jsid *idp);

extern JS_PUBLIC_API(JSBool)
JS_CheckAccess(JSContext *cx, JSObject *obj, jsid id, JSAccessMode mode,
               jsval *vp, uintN *attrsp);

extern JS_PUBLIC_API(JSBool)
JS_GetReservedSlot(JSContext *cx, JSObject *obj, uint32 index, jsval *vp);

extern JS_PUBLIC_API(JSBool)
JS_SetReservedSlot(JSContext *cx, JSObject *obj, uint32 index, jsval v);

/************************************************************************/

/*
 * Security protocol.
 */
struct JSPrincipals {
    char *codebase;

    /* XXX unspecified and unused by Mozilla code -- can we remove these? */
    void * (* getPrincipalArray)(JSContext *cx, JSPrincipals *);
    JSBool (* globalPrivilegesEnabled)(JSContext *cx, JSPrincipals *);

    /* Don't call "destroy"; use reference counting macros below. */
    jsrefcount refcount;

    void   (* destroy)(JSContext *cx, JSPrincipals *);
    JSBool (* subsume)(JSPrincipals *, JSPrincipals *);
};

#ifdef JS_THREADSAFE
#define JSPRINCIPALS_HOLD(cx, principals)   JS_HoldPrincipals(cx,principals)
#define JSPRINCIPALS_DROP(cx, principals)   JS_DropPrincipals(cx,principals)

extern JS_PUBLIC_API(jsrefcount)
JS_HoldPrincipals(JSContext *cx, JSPrincipals *principals);

extern JS_PUBLIC_API(jsrefcount)
JS_DropPrincipals(JSContext *cx, JSPrincipals *principals);

#else
#define JSPRINCIPALS_HOLD(cx, principals)   (++(principals)->refcount)
#define JSPRINCIPALS_DROP(cx, principals)                                     \
    ((--(principals)->refcount == 0)                                          \
     ? ((*(principals)->destroy)((cx), (principals)), 0)                      \
     : (principals)->refcount)
#endif


struct JSSecurityCallbacks {
    JSCheckAccessOp            checkObjectAccess;
    JSPrincipalsTranscoder     principalsTranscoder;
    JSObjectPrincipalsFinder   findObjectPrincipals;
    JSCSPEvalChecker           contentSecurityPolicyAllows;
};

extern JS_PUBLIC_API(JSSecurityCallbacks *)
JS_SetRuntimeSecurityCallbacks(JSRuntime *rt, JSSecurityCallbacks *callbacks);

extern JS_PUBLIC_API(JSSecurityCallbacks *)
JS_GetRuntimeSecurityCallbacks(JSRuntime *rt);

extern JS_PUBLIC_API(JSSecurityCallbacks *)
JS_SetContextSecurityCallbacks(JSContext *cx, JSSecurityCallbacks *callbacks);

extern JS_PUBLIC_API(JSSecurityCallbacks *)
JS_GetSecurityCallbacks(JSContext *cx);

/*
 * Code running with "trusted" principals will be given a deeper stack
 * allocation than ordinary scripts. This allows trusted script to run after
 * untrusted script has exhausted the stack. This function sets the
 * runtime-wide trusted principal.
 *
 * This principals is not held (via JS_HoldPrincipals/JS_DropPrincipals) since
 * there is no available JSContext. Instead, the caller must ensure that the
 * given principals stays valid for as long as 'rt' may point to it. If the
 * principals would be destroyed before 'rt', JS_SetTrustedPrincipals must be
 * called again, passing NULL for 'prin'.
 */
extern JS_PUBLIC_API(void)
JS_SetTrustedPrincipals(JSRuntime *rt, JSPrincipals *prin);

/************************************************************************/

/*
 * Functions and scripts.
 */
extern JS_PUBLIC_API(JSFunction *)
JS_NewFunction(JSContext *cx, JSNative call, uintN nargs, uintN flags,
               JSObject *parent, const char *name);

/*
 * Create the function with the name given by the id. JSID_IS_STRING(id) must
 * be true.
 */
extern JS_PUBLIC_API(JSFunction *)
JS_NewFunctionById(JSContext *cx, JSNative call, uintN nargs, uintN flags,
                   JSObject *parent, jsid id);

extern JS_PUBLIC_API(JSObject *)
JS_GetFunctionObject(JSFunction *fun);

/*
 * Return the function's identifier as a JSString, or null if fun is unnamed.
 * The returned string lives as long as fun, so you don't need to root a saved
 * reference to it if fun is well-connected or rooted, and provided you bound
 * the use of the saved reference by fun's lifetime.
 */
extern JS_PUBLIC_API(JSString *)
JS_GetFunctionId(JSFunction *fun);

/*
 * Return JSFUN_* flags for fun.
 */
extern JS_PUBLIC_API(uintN)
JS_GetFunctionFlags(JSFunction *fun);

/*
 * Return the arity (length) of fun.
 */
extern JS_PUBLIC_API(uint16)
JS_GetFunctionArity(JSFunction *fun);

/*
 * Infallible predicate to test whether obj is a function object (faster than
 * comparing obj's class name to "Function", but equivalent unless someone has
 * overwritten the "Function" identifier with a different constructor and then
 * created instances using that constructor that might be passed in as obj).
 */
extern JS_PUBLIC_API(JSBool)
JS_ObjectIsFunction(JSContext *cx, JSObject *obj);

extern JS_PUBLIC_API(JSBool)
JS_ObjectIsCallable(JSContext *cx, JSObject *obj);

extern JS_PUBLIC_API(JSBool)
JS_IsNativeFunction(JSObject *funobj, JSNative call);

extern JS_PUBLIC_API(JSBool)
JS_DefineFunctions(JSContext *cx, JSObject *obj, JSFunctionSpec *fs);

extern JS_PUBLIC_API(JSFunction *)
JS_DefineFunction(JSContext *cx, JSObject *obj, const char *name, JSNative call,
                  uintN nargs, uintN attrs);

extern JS_PUBLIC_API(JSFunction *)
JS_DefineUCFunction(JSContext *cx, JSObject *obj,
                    const jschar *name, size_t namelen, JSNative call,
                    uintN nargs, uintN attrs);

extern JS_PUBLIC_API(JSFunction *)
JS_DefineFunctionById(JSContext *cx, JSObject *obj, jsid id, JSNative call,
                      uintN nargs, uintN attrs);

extern JS_PUBLIC_API(JSObject *)
JS_CloneFunctionObject(JSContext *cx, JSObject *funobj, JSObject *parent);

/*
 * Given a buffer, return JS_FALSE if the buffer might become a valid
 * javascript statement with the addition of more lines.  Otherwise return
 * JS_TRUE.  The intent is to support interactive compilation - accumulate
 * lines in a buffer until JS_BufferIsCompilableUnit is true, then pass it to
 * the compiler.
 */
extern JS_PUBLIC_API(JSBool)
JS_BufferIsCompilableUnit(JSContext *cx, JSBool bytes_are_utf8,
                          JSObject *obj, const char *bytes, size_t length);

extern JS_PUBLIC_API(JSScript *)
JS_CompileScript(JSContext *cx, JSObject *obj,
                 const char *bytes, size_t length,
                 const char *filename, uintN lineno);

extern JS_PUBLIC_API(JSScript *)
JS_CompileScriptForPrincipals(JSContext *cx, JSObject *obj,
                              JSPrincipals *principals,
                              const char *bytes, size_t length,
                              const char *filename, uintN lineno);

extern JS_PUBLIC_API(JSScript *)
JS_CompileScriptForPrincipalsVersion(JSContext *cx, JSObject *obj,
                                     JSPrincipals *principals,
                                     const char *bytes, size_t length,
                                     const char *filename, uintN lineno,
                                     JSVersion version);

extern JS_PUBLIC_API(JSScript *)
JS_CompileUCScript(JSContext *cx, JSObject *obj,
                   const jschar *chars, size_t length,
                   const char *filename, uintN lineno);

extern JS_PUBLIC_API(JSScript *)
JS_CompileUCScriptForPrincipals(JSContext *cx, JSObject *obj,
                                JSPrincipals *principals,
                                const jschar *chars, size_t length,
                                const char *filename, uintN lineno);

extern JS_PUBLIC_API(JSScript *)
JS_CompileUCScriptForPrincipalsVersion(JSContext *cx, JSObject *obj,
                                       JSPrincipals *principals,
                                       const jschar *chars, size_t length,
                                       const char *filename, uintN lineno,
                                       JSVersion version);

extern JS_PUBLIC_API(JSScript *)
JS_CompileFile(JSContext *cx, JSObject *obj, const char *filename);

extern JS_PUBLIC_API(JSScript *)
JS_CompileFileHandle(JSContext *cx, JSObject *obj, const char *filename,
                     FILE *fh);

extern JS_PUBLIC_API(JSScript *)
JS_CompileFileHandleForPrincipals(JSContext *cx, JSObject *obj,
                                  const char *filename, FILE *fh,
                                  JSPrincipals *principals);

extern JS_PUBLIC_API(JSScript *)
JS_CompileFileHandleForPrincipalsVersion(JSContext *cx, JSObject *obj,
                                         const char *filename, FILE *fh,
                                         JSPrincipals *principals,
                                         JSVersion version);

extern JS_PUBLIC_API(JSObject *)
JS_GetGlobalFromScript(JSScript *script);

extern JS_PUBLIC_API(JSFunction *)
JS_CompileFunction(JSContext *cx, JSObject *obj, const char *name,
                   uintN nargs, const char **argnames,
                   const char *bytes, size_t length,
                   const char *filename, uintN lineno);

extern JS_PUBLIC_API(JSFunction *)
JS_CompileFunctionForPrincipals(JSContext *cx, JSObject *obj,
                                JSPrincipals *principals, const char *name,
                                uintN nargs, const char **argnames,
                                const char *bytes, size_t length,
                                const char *filename, uintN lineno);

extern JS_PUBLIC_API(JSFunction *)
JS_CompileUCFunction(JSContext *cx, JSObject *obj, const char *name,
                     uintN nargs, const char **argnames,
                     const jschar *chars, size_t length,
                     const char *filename, uintN lineno);

extern JS_PUBLIC_API(JSFunction *)
JS_CompileUCFunctionForPrincipals(JSContext *cx, JSObject *obj,
                                  JSPrincipals *principals, const char *name,
                                  uintN nargs, const char **argnames,
                                  const jschar *chars, size_t length,
                                  const char *filename, uintN lineno);

extern JS_PUBLIC_API(JSFunction *)
JS_CompileUCFunctionForPrincipalsVersion(JSContext *cx, JSObject *obj,
                                         JSPrincipals *principals, const char *name,
                                         uintN nargs, const char **argnames,
                                         const jschar *chars, size_t length,
                                         const char *filename, uintN lineno,
                                         JSVersion version);

extern JS_PUBLIC_API(JSString *)
JS_DecompileScript(JSContext *cx, JSScript *script, const char *name, uintN indent);

/*
 * API extension: OR this into indent to avoid pretty-printing the decompiled
 * source resulting from JS_DecompileFunction{,Body}.
 */
#define JS_DONT_PRETTY_PRINT    ((uintN)0x8000)

extern JS_PUBLIC_API(JSString *)
JS_DecompileFunction(JSContext *cx, JSFunction *fun, uintN indent);

extern JS_PUBLIC_API(JSString *)
JS_DecompileFunctionBody(JSContext *cx, JSFunction *fun, uintN indent);

/*
 * NB: JS_ExecuteScript and the JS_Evaluate*Script* quadruplets use the obj
 * parameter as the initial scope chain header, the 'this' keyword value, and
 * the variables object (ECMA parlance for where 'var' and 'function' bind
 * names) of the execution context for script.
 *
 * Using obj as the variables object is problematic if obj's parent (which is
 * the scope chain link; see JS_SetParent and JS_NewObject) is not null: in
 * this case, variables created by 'var x = 0', e.g., go in obj, but variables
 * created by assignment to an unbound id, 'x = 0', go in the last object on
 * the scope chain linked by parent.
 *
 * ECMA calls that last scoping object the "global object", but note that many
 * embeddings have several such objects.  ECMA requires that "global code" be
 * executed with the variables object equal to this global object.  But these
 * JS API entry points provide freedom to execute code against a "sub-global",
 * i.e., a parented or scoped object, in which case the variables object will
 * differ from the last object on the scope chain, resulting in confusing and
 * non-ECMA explicit vs. implicit variable creation.
 *
 * Caveat embedders: unless you already depend on this buggy variables object
 * binding behavior, you should call JS_SetOptions(cx, JSOPTION_VAROBJFIX) or
 * JS_SetOptions(cx, JS_GetOptions(cx) | JSOPTION_VAROBJFIX) -- the latter if
 * someone may have set other options on cx already -- for each context in the
 * application, if you pass parented objects as the obj parameter, or may ever
 * pass such objects in the future.
 *
 * Why a runtime option?  The alternative is to add six or so new API entry
 * points with signatures matching the following six, and that doesn't seem
 * worth the code bloat cost.  Such new entry points would probably have less
 * obvious names, too, so would not tend to be used.  The JS_SetOption call,
 * OTOH, can be more easily hacked into existing code that does not depend on
 * the bug; such code can continue to use the familiar JS_EvaluateScript,
 * etc., entry points.
 */
extern JS_PUBLIC_API(JSBool)
JS_ExecuteScript(JSContext *cx, JSObject *obj, JSScript *script, jsval *rval);

extern JS_PUBLIC_API(JSBool)
JS_ExecuteScriptVersion(JSContext *cx, JSObject *obj, JSScript *script, jsval *rval,
                        JSVersion version);

/*
 * Execute either the function-defining prolog of a script, or the script's
 * main body, but not both.
 */
typedef enum JSExecPart { JSEXEC_PROLOG, JSEXEC_MAIN } JSExecPart;

extern JS_PUBLIC_API(JSBool)
JS_EvaluateScript(JSContext *cx, JSObject *obj,
                  const char *bytes, uintN length,
                  const char *filename, uintN lineno,
                  jsval *rval);

extern JS_PUBLIC_API(JSBool)
JS_EvaluateScriptForPrincipals(JSContext *cx, JSObject *obj,
                               JSPrincipals *principals,
                               const char *bytes, uintN length,
                               const char *filename, uintN lineno,
                               jsval *rval);

extern JS_PUBLIC_API(JSBool)
JS_EvaluateScriptForPrincipalsVersion(JSContext *cx, JSObject *obj,
                                      JSPrincipals *principals,
                                      const char *bytes, uintN length,
                                      const char *filename, uintN lineno,
                                      jsval *rval, JSVersion version);

extern JS_PUBLIC_API(JSBool)
JS_EvaluateUCScript(JSContext *cx, JSObject *obj,
                    const jschar *chars, uintN length,
                    const char *filename, uintN lineno,
                    jsval *rval);

extern JS_PUBLIC_API(JSBool)
JS_EvaluateUCScriptForPrincipalsVersion(JSContext *cx, JSObject *obj,
                                        JSPrincipals *principals,
                                        const jschar *chars, uintN length,
                                        const char *filename, uintN lineno,
                                        jsval *rval, JSVersion version);

extern JS_PUBLIC_API(JSBool)
JS_EvaluateUCScriptForPrincipals(JSContext *cx, JSObject *obj,
                                 JSPrincipals *principals,
                                 const jschar *chars, uintN length,
                                 const char *filename, uintN lineno,
                                 jsval *rval);

extern JS_PUBLIC_API(JSBool)
JS_CallFunction(JSContext *cx, JSObject *obj, JSFunction *fun, uintN argc,
                jsval *argv, jsval *rval);

extern JS_PUBLIC_API(JSBool)
JS_CallFunctionName(JSContext *cx, JSObject *obj, const char *name, uintN argc,
                    jsval *argv, jsval *rval);

extern JS_PUBLIC_API(JSBool)
JS_CallFunctionValue(JSContext *cx, JSObject *obj, jsval fval, uintN argc,
                     jsval *argv, jsval *rval);

#ifdef __cplusplus
JS_END_EXTERN_C

namespace JS {

static inline bool
Call(JSContext *cx, JSObject *thisObj, JSFunction *fun, uintN argc, jsval *argv, jsval *rval) {
    return !!JS_CallFunction(cx, thisObj, fun, argc, argv, rval);
}

static inline bool
Call(JSContext *cx, JSObject *thisObj, const char *name, uintN argc, jsval *argv, jsval *rval) {
    return !!JS_CallFunctionName(cx, thisObj, name, argc, argv, rval);
}

static inline bool
Call(JSContext *cx, JSObject *thisObj, jsval fun, uintN argc, jsval *argv, jsval *rval) {
    return !!JS_CallFunctionValue(cx, thisObj, fun, argc, argv, rval);
}

extern JS_PUBLIC_API(bool)
Call(JSContext *cx, jsval thisv, jsval fun, uintN argc, jsval *argv, jsval *rval);

static inline bool
Call(JSContext *cx, jsval thisv, JSObject *funObj, uintN argc, jsval *argv, jsval *rval) {
    return Call(cx, thisv, OBJECT_TO_JSVAL(funObj), argc, argv, rval);
}

} /* namespace JS */

JS_BEGIN_EXTERN_C
#endif /* __cplusplus */

/*
 * These functions allow setting an operation callback that will be called
 * from the thread the context is associated with some time after any thread
 * triggered the callback using JS_TriggerOperationCallback(cx).
 *
 * In a threadsafe build the engine internally triggers operation callbacks
 * under certain circumstances (i.e. GC and title transfer) to force the
 * context to yield its current request, which the engine always
 * automatically does immediately prior to calling the callback function.
 * The embedding should thus not rely on callbacks being triggered through
 * the external API only.
 *
 * Important note: Additional callbacks can occur inside the callback handler
 * if it re-enters the JS engine. The embedding must ensure that the callback
 * is disconnected before attempting such re-entry.
 */

extern JS_PUBLIC_API(JSOperationCallback)
JS_SetOperationCallback(JSContext *cx, JSOperationCallback callback);

extern JS_PUBLIC_API(JSOperationCallback)
JS_GetOperationCallback(JSContext *cx);

extern JS_PUBLIC_API(void)
JS_TriggerOperationCallback(JSContext *cx);

extern JS_PUBLIC_API(void)
JS_TriggerAllOperationCallbacks(JSRuntime *rt);

extern JS_PUBLIC_API(JSBool)
JS_IsRunning(JSContext *cx);

/*
 * Saving and restoring frame chains.
 *
 * These two functions are used to set aside cx's call stack while that stack
 * is inactive. After a call to JS_SaveFrameChain, it looks as if there is no
 * code running on cx. Before calling JS_RestoreFrameChain, cx's call stack
 * must be balanced and all nested calls to JS_SaveFrameChain must have had
 * matching JS_RestoreFrameChain calls.
 *
 * JS_SaveFrameChain deals with cx not having any code running on it.
 */
extern JS_PUBLIC_API(JSBool)
JS_SaveFrameChain(JSContext *cx);

extern JS_PUBLIC_API(void)
JS_RestoreFrameChain(JSContext *cx);

/************************************************************************/

/*
 * Strings.
 *
 * NB: JS_NewUCString takes ownership of bytes on success, avoiding a copy;
 * but on error (signified by null return), it leaves chars owned by the
 * caller. So the caller must free bytes in the error case, if it has no use
 * for them. In contrast, all the JS_New*StringCopy* functions do not take
 * ownership of the character memory passed to them -- they copy it.
 */
extern JS_PUBLIC_API(JSString *)
JS_NewStringCopyN(JSContext *cx, const char *s, size_t n);

extern JS_PUBLIC_API(JSString *)
JS_NewStringCopyZ(JSContext *cx, const char *s);

extern JS_PUBLIC_API(JSString *)
JS_InternJSString(JSContext *cx, JSString *str);

extern JS_PUBLIC_API(JSString *)
JS_InternString(JSContext *cx, const char *s);

extern JS_PUBLIC_API(JSString *)
JS_NewUCString(JSContext *cx, jschar *chars, size_t length);

extern JS_PUBLIC_API(JSString *)
JS_NewUCStringCopyN(JSContext *cx, const jschar *s, size_t n);

extern JS_PUBLIC_API(JSString *)
JS_NewUCStringCopyZ(JSContext *cx, const jschar *s);

extern JS_PUBLIC_API(JSString *)
JS_InternUCStringN(JSContext *cx, const jschar *s, size_t length);

extern JS_PUBLIC_API(JSString *)
JS_InternUCString(JSContext *cx, const jschar *s);

extern JS_PUBLIC_API(JSBool)
JS_CompareStrings(JSContext *cx, JSString *str1, JSString *str2, int32 *result);

extern JS_PUBLIC_API(JSBool)
JS_StringEqualsAscii(JSContext *cx, JSString *str, const char *asciiBytes, JSBool *match);

extern JS_PUBLIC_API(size_t)
JS_PutEscapedString(JSContext *cx, char *buffer, size_t size, JSString *str, char quote);

extern JS_PUBLIC_API(JSBool)
JS_FileEscapedString(FILE *fp, JSString *str, char quote);

/*
 * Extracting string characters and length.
 *
 * While getting the length of a string is infallible, getting the chars can
 * fail. As indicated by the lack of a JSContext parameter, there are two
 * special cases where getting the chars is infallible:
 *
 * The first case is interned strings, i.e., strings from JS_InternString or
 * JSID_TO_STRING(id), using JS_GetInternedStringChars*.
 *
 * The second case is "flat" strings that have been explicitly prepared in a
 * fallible context by JS_FlattenString. To catch errors, a separate opaque
 * JSFlatString type is returned by JS_FlattenString and expected by
 * JS_GetFlatStringChars. Note, though, that this is purely a syntactic
 * distinction: the input and output of JS_FlattenString are the same actual
 * GC-thing so only one needs to be rooted. If a JSString is known to be flat,
 * JS_ASSERT_STRING_IS_FLAT can be used to make a debug-checked cast. Example:
 *
 *   // in a fallible context
 *   JSFlatString *fstr = JS_FlattenString(cx, str);
 *   if (!fstr)
 *     return JS_FALSE;
 *   JS_ASSERT(fstr == JS_ASSERT_STRING_IS_FLAT(str));
 *
 *   // in an infallible context, for the same 'str'
 *   const jschar *chars = JS_GetFlatStringChars(fstr)
 *   JS_ASSERT(chars);
 *
 * The CharsZ APIs guarantee that the returned array has a null character at
 * chars[length]. This can require additional copying so clients should prefer
 * APIs without CharsZ if possible. The infallible functions also return
 * null-terminated arrays. (There is no additional cost or non-Z alternative
 * for the infallible functions, so 'Z' is left out of the identifier.)
 */

extern JS_PUBLIC_API(size_t)
JS_GetStringLength(JSString *str);

extern JS_PUBLIC_API(const jschar *)
JS_GetStringCharsAndLength(JSContext *cx, JSString *str, size_t *length);

extern JS_PUBLIC_API(const jschar *)
JS_GetInternedStringChars(JSString *str);

extern JS_PUBLIC_API(const jschar *)
JS_GetInternedStringCharsAndLength(JSString *str, size_t *length);

extern JS_PUBLIC_API(const jschar *)
JS_GetStringCharsZ(JSContext *cx, JSString *str);

extern JS_PUBLIC_API(const jschar *)
JS_GetStringCharsZAndLength(JSContext *cx, JSString *str, size_t *length);

extern JS_PUBLIC_API(JSFlatString *)
JS_FlattenString(JSContext *cx, JSString *str);

extern JS_PUBLIC_API(const jschar *)
JS_GetFlatStringChars(JSFlatString *str);

static JS_ALWAYS_INLINE JSFlatString *
JSID_TO_FLAT_STRING(jsid id)
{
    JS_ASSERT(JSID_IS_STRING(id));
    return (JSFlatString *)(JSID_BITS(id));
}

static JS_ALWAYS_INLINE JSFlatString *
JS_ASSERT_STRING_IS_FLAT(JSString *str)
{
    JS_ASSERT(JS_GetFlatStringChars((JSFlatString *)str));
    return (JSFlatString *)str;
}

static JS_ALWAYS_INLINE JSString *
JS_FORGET_STRING_FLATNESS(JSFlatString *fstr)
{
    return (JSString *)fstr;
}

/*
 * Additional APIs that avoid fallibility when given a flat string.
 */

extern JS_PUBLIC_API(JSBool)
JS_FlatStringEqualsAscii(JSFlatString *str, const char *asciiBytes);

extern JS_PUBLIC_API(size_t)
JS_PutEscapedFlatString(char *buffer, size_t size, JSFlatString *str, char quote);

/*
 * This function is now obsolete and behaves the same as JS_NewUCString.  Use
 * JS_NewUCString instead.
 */
extern JS_PUBLIC_API(JSString *)
JS_NewGrowableString(JSContext *cx, jschar *chars, size_t length);

/*
 * Mutable string support.  A string's characters are never mutable in this JS
 * implementation, but a dependent string is a substring of another dependent
 * or immutable string, and a rope is a lazily concatenated string that creates
 * its underlying buffer the first time it is accessed.  Even after a rope
 * creates its underlying buffer, it still considered mutable.  The direct data
 * members of the (opaque to API clients) JSString struct may be changed in a
 * single-threaded way for dependent strings and ropes.
 *
 * Therefore mutable strings (ropes and dependent strings) cannot be used by
 * more than one thread at a time.  You may call JS_MakeStringImmutable to
 * convert the string from a mutable string to an immutable (and therefore
 * thread-safe) string.  The engine takes care of converting ropes and dependent
 * strings to immutable for you if you store strings in multi-threaded objects
 * using JS_SetProperty or kindred API entry points.
 *
 * If you store a JSString pointer in a native data structure that is (safely)
 * accessible to multiple threads, you must call JS_MakeStringImmutable before
 * retiring the store.
 */

/*
 * Create a dependent string, i.e., a string that owns no character storage,
 * but that refers to a slice of another string's chars.  Dependent strings
 * are mutable by definition, so the thread safety comments above apply.
 */
extern JS_PUBLIC_API(JSString *)
JS_NewDependentString(JSContext *cx, JSString *str, size_t start,
                      size_t length);

/*
 * Concatenate two strings, possibly resulting in a rope.
 * See above for thread safety comments.
 */
extern JS_PUBLIC_API(JSString *)
JS_ConcatStrings(JSContext *cx, JSString *left, JSString *right);

/*
 * Convert a dependent string into an independent one.  This function does not
 * change the string's mutability, so the thread safety comments above apply.
 */
extern JS_PUBLIC_API(const jschar *)
JS_UndependString(JSContext *cx, JSString *str);

/*
 * Convert a mutable string (either rope or dependent) into an immutable,
 * thread-safe one.
 */
extern JS_PUBLIC_API(JSBool)
JS_MakeStringImmutable(JSContext *cx, JSString *str);

/*
 * Return JS_TRUE if C (char []) strings passed via the API and internally
 * are UTF-8.
 */
JS_PUBLIC_API(JSBool)
JS_CStringsAreUTF8(void);

/*
 * Update the value to be returned by JS_CStringsAreUTF8(). Once set, it
 * can never be changed. This API must be called before the first call to
 * JS_NewRuntime.
 */
JS_PUBLIC_API(void)
JS_SetCStringsAreUTF8(void);

/*
 * Character encoding support.
 *
 * For both JS_EncodeCharacters and JS_DecodeBytes, set *dstlenp to the size
 * of the destination buffer before the call; on return, *dstlenp contains the
 * number of bytes (JS_EncodeCharacters) or jschars (JS_DecodeBytes) actually
 * stored.  To determine the necessary destination buffer size, make a sizing
 * call that passes NULL for dst.
 *
 * On errors, the functions report the error. In that case, *dstlenp contains
 * the number of characters or bytes transferred so far.  If cx is NULL, no
 * error is reported on failure, and the functions simply return JS_FALSE.
 *
 * NB: Neither function stores an additional zero byte or jschar after the
 * transcoded string.
 *
 * If JS_CStringsAreUTF8() is true then JS_EncodeCharacters encodes to
 * UTF-8, and JS_DecodeBytes decodes from UTF-8, which may create additional
 * errors if the character sequence is malformed.  If UTF-8 support is
 * disabled, the functions deflate and inflate, respectively.
 *
 * JS_DecodeUTF8() always behaves the same independently of JS_CStringsAreUTF8().
 */
JS_PUBLIC_API(JSBool)
JS_EncodeCharacters(JSContext *cx, const jschar *src, size_t srclen, char *dst,
                    size_t *dstlenp);

JS_PUBLIC_API(JSBool)
JS_DecodeBytes(JSContext *cx, const char *src, size_t srclen, jschar *dst,
               size_t *dstlenp);

JS_PUBLIC_API(JSBool)
JS_DecodeUTF8(JSContext *cx, const char *src, size_t srclen, jschar *dst,
              size_t *dstlenp);

/*
 * A variation on JS_EncodeCharacters where a null terminated string is
 * returned that you are expected to call JS_free on when done.
 */
JS_PUBLIC_API(char *)
JS_EncodeString(JSContext *cx, JSString *str);

/*
 * Get number of bytes in the string encoding (without accounting for a
 * terminating zero bytes. The function returns (size_t) -1 if the string
 * can not be encoded into bytes and reports an error using cx accordingly.
 */
JS_PUBLIC_API(size_t)
JS_GetStringEncodingLength(JSContext *cx, JSString *str);

/*
 * Encode string into a buffer. The function does not stores an additional
 * zero byte. The function returns (size_t) -1 if the string can not be
 * encoded into bytes with no error reported. Otherwise it returns the number
 * of bytes that are necessary to encode the string. If that exceeds the
 * length parameter, the string will be cut and only length bytes will be
 * written into the buffer.
 *
 * If JS_CStringsAreUTF8() is true, the string does not fit into the buffer
 * and the the first length bytes ends in the middle of utf-8 encoding for
 * some character, then such partial utf-8 encoding is replaced by zero bytes.
 * This way the result always represents the valid UTF-8 sequence.
 */
JS_PUBLIC_API(size_t)
JS_EncodeStringToBuffer(JSString *str, char *buffer, size_t length);

#ifdef __cplusplus

class JSAutoByteString {
  public:
    JSAutoByteString(JSContext *cx, JSString *str JS_GUARD_OBJECT_NOTIFIER_PARAM)
      : mBytes(JS_EncodeString(cx, str)) {
        JS_ASSERT(cx);
        JS_GUARD_OBJECT_NOTIFIER_INIT;
    }

    JSAutoByteString(JS_GUARD_OBJECT_NOTIFIER_PARAM0)
      : mBytes(NULL) {
        JS_GUARD_OBJECT_NOTIFIER_INIT;
    }

    ~JSAutoByteString() {
        js::UnwantedForeground::free_(mBytes);
    }

    /* Take ownership of the given byte array. */
    void initBytes(char *bytes) {
        JS_ASSERT(!mBytes);
        mBytes = bytes;
    }

    char *encode(JSContext *cx, JSString *str) {
        JS_ASSERT(!mBytes);
        JS_ASSERT(cx);
        mBytes = JS_EncodeString(cx, str);
        return mBytes;
    }

    void clear() {
        js::UnwantedForeground::free_(mBytes);
        mBytes = NULL;
    }

    char *ptr() const {
        return mBytes;
    }

    bool operator!() const {
        return !mBytes;
    }

  private:
    char        *mBytes;
    JS_DECL_USE_GUARD_OBJECT_NOTIFIER

    /* Copy and assignment are not supported. */
    JSAutoByteString(const JSAutoByteString &another);
    JSAutoByteString &operator=(const JSAutoByteString &another);
};

#endif

/************************************************************************/
/*
 * JSON functions
 */
typedef JSBool (* JSONWriteCallback)(const jschar *buf, uint32 len, void *data);

/*
 * JSON.stringify as specified by ES5.
 */
JS_PUBLIC_API(JSBool)
JS_Stringify(JSContext *cx, jsval *vp, JSObject *replacer, jsval space,
             JSONWriteCallback callback, void *data);

/*
 * JSON.parse as specified by ES5.
 */
JS_PUBLIC_API(JSBool)
JS_ParseJSON(JSContext *cx, const jschar *chars, uint32 len, jsval *vp);

JS_PUBLIC_API(JSBool)
JS_ParseJSONWithReviver(JSContext *cx, const jschar *chars, uint32 len, jsval reviver, jsval *vp);

/************************************************************************/

/* API for the HTML5 internal structured cloning algorithm. */

/* The maximum supported structured-clone serialization format version. */
#define JS_STRUCTURED_CLONE_VERSION 1

struct JSStructuredCloneCallbacks {
    ReadStructuredCloneOp read;
    WriteStructuredCloneOp write;
    StructuredCloneErrorOp reportError;
};

JS_PUBLIC_API(JSBool)
JS_ReadStructuredClone(JSContext *cx, const uint64 *data, size_t nbytes,
                       uint32 version, jsval *vp,
                       const JSStructuredCloneCallbacks *optionalCallbacks,
                       void *closure);

/* Note: On success, the caller is responsible for calling js::Foreground::free(*datap). */
JS_PUBLIC_API(JSBool)
JS_WriteStructuredClone(JSContext *cx, jsval v, uint64 **datap, size_t *nbytesp,
                        const JSStructuredCloneCallbacks *optionalCallbacks,
                        void *closure);

JS_PUBLIC_API(JSBool)
JS_StructuredClone(JSContext *cx, jsval v, jsval *vp,
                   const JSStructuredCloneCallbacks *optionalCallbacks,
                   void *closure);

#ifdef __cplusplus
JS_END_EXTERN_C

/* RAII sugar for JS_WriteStructuredClone. */
class JS_PUBLIC_API(JSAutoStructuredCloneBuffer) {
    JSUint64 *data_;
    size_t nbytes_;
    JSUint32 version_;

  public:
    JSAutoStructuredCloneBuffer()
        : data_(NULL), nbytes_(0), version_(JS_STRUCTURED_CLONE_VERSION) {}

    ~JSAutoStructuredCloneBuffer() { clear(); }

    JSUint64 *data() const { return data_; }
    size_t nbytes() const { return nbytes_; }

    void clear();

    /* Copy some memory. It will be automatically freed by the destructor. */
    bool copy(const JSUint64 *data, size_t nbytes, JSUint32 version=JS_STRUCTURED_CLONE_VERSION);

    /*
     * Adopt some memory. It will be automatically freed by the destructor.
     * data must have been allocated by the JS engine (e.g., extracted via
     * JSAutoStructuredCloneBuffer::steal).
     */
    void adopt(JSUint64 *data, size_t nbytes, JSUint32 version=JS_STRUCTURED_CLONE_VERSION);

    /*
     * Remove the buffer so that it will not be automatically freed.
     * After this, the caller is responsible for feeding the memory back to
     * JSAutoStructuredCloneBuffer::adopt.
     */
    void steal(JSUint64 **datap, size_t *nbytesp, JSUint32 *versionp=NULL);

    bool read(JSContext *cx, jsval *vp,
              const JSStructuredCloneCallbacks *optionalCallbacks=NULL,
              void *closure=NULL) const;

    bool write(JSContext *cx, jsval v,
               const JSStructuredCloneCallbacks *optionalCallbacks=NULL,
               void *closure=NULL);

    /**
     * Swap ownership with another JSAutoStructuredCloneBuffer.
     */
    void swap(JSAutoStructuredCloneBuffer &other);

  private:
    /* Copy and assignment are not supported. */
    JSAutoStructuredCloneBuffer(const JSAutoStructuredCloneBuffer &other);
    JSAutoStructuredCloneBuffer &operator=(const JSAutoStructuredCloneBuffer &other);
};

JS_BEGIN_EXTERN_C
#endif

/* API for implementing custom serialization behavior (for ImageData, File, etc.) */

/* The range of tag values the application may use for its own custom object types. */
#define JS_SCTAG_USER_MIN  ((uint32) 0xFFFF8000)
#define JS_SCTAG_USER_MAX  ((uint32) 0xFFFFFFFF)

#define JS_SCERR_RECURSION 0

JS_PUBLIC_API(void)
JS_SetStructuredCloneCallbacks(JSRuntime *rt, const JSStructuredCloneCallbacks *callbacks);

JS_PUBLIC_API(JSBool)
JS_ReadUint32Pair(JSStructuredCloneReader *r, uint32 *p1, uint32 *p2);

JS_PUBLIC_API(JSBool)
JS_ReadBytes(JSStructuredCloneReader *r, void *p, size_t len);

JS_PUBLIC_API(JSBool)
JS_WriteUint32Pair(JSStructuredCloneWriter *w, uint32 tag, uint32 data);

JS_PUBLIC_API(JSBool)
JS_WriteBytes(JSStructuredCloneWriter *w, const void *p, size_t len);

/************************************************************************/

/*
 * Locale specific string conversion and error message callbacks.
 */
struct JSLocaleCallbacks {
    JSLocaleToUpperCase     localeToUpperCase;
    JSLocaleToLowerCase     localeToLowerCase;
    JSLocaleCompare         localeCompare;
    JSLocaleToUnicode       localeToUnicode;
    JSErrorCallback         localeGetErrorMessage;
};

/*
 * Establish locale callbacks. The pointer must persist as long as the
 * JSContext.  Passing NULL restores the default behaviour.
 */
extern JS_PUBLIC_API(void)
JS_SetLocaleCallbacks(JSContext *cx, JSLocaleCallbacks *callbacks);

/*
 * Return the address of the current locale callbacks struct, which may
 * be NULL.
 */
extern JS_PUBLIC_API(JSLocaleCallbacks *)
JS_GetLocaleCallbacks(JSContext *cx);

/************************************************************************/

/*
 * Error reporting.
 */

/*
 * Report an exception represented by the sprintf-like conversion of format
 * and its arguments.  This exception message string is passed to a pre-set
 * JSErrorReporter function (set by JS_SetErrorReporter).
 */
extern JS_PUBLIC_API(void)
JS_ReportError(JSContext *cx, const char *format, ...);

/*
 * Use an errorNumber to retrieve the format string, args are char *
 */
extern JS_PUBLIC_API(void)
JS_ReportErrorNumber(JSContext *cx, JSErrorCallback errorCallback,
                     void *userRef, const uintN errorNumber, ...);

/*
 * Use an errorNumber to retrieve the format string, args are jschar *
 */
extern JS_PUBLIC_API(void)
JS_ReportErrorNumberUC(JSContext *cx, JSErrorCallback errorCallback,
                     void *userRef, const uintN errorNumber, ...);

/*
 * As above, but report a warning instead (JSREPORT_IS_WARNING(report.flags)).
 * Return true if there was no error trying to issue the warning, and if the
 * warning was not converted into an error due to the JSOPTION_WERROR option
 * being set, false otherwise.
 */
extern JS_PUBLIC_API(JSBool)
JS_ReportWarning(JSContext *cx, const char *format, ...);

extern JS_PUBLIC_API(JSBool)
JS_ReportErrorFlagsAndNumber(JSContext *cx, uintN flags,
                             JSErrorCallback errorCallback, void *userRef,
                             const uintN errorNumber, ...);

extern JS_PUBLIC_API(JSBool)
JS_ReportErrorFlagsAndNumberUC(JSContext *cx, uintN flags,
                               JSErrorCallback errorCallback, void *userRef,
                               const uintN errorNumber, ...);

/*
 * Complain when out of memory.
 */
extern JS_PUBLIC_API(void)
JS_ReportOutOfMemory(JSContext *cx);

/*
 * Complain when an allocation size overflows the maximum supported limit.
 */
extern JS_PUBLIC_API(void)
JS_ReportAllocationOverflow(JSContext *cx);

struct JSErrorReport {
    const char      *filename;      /* source file name, URL, etc., or null */
    uintN           lineno;         /* source line number */
    const char      *linebuf;       /* offending source line without final \n */
    const char      *tokenptr;      /* pointer to error token in linebuf */
    const jschar    *uclinebuf;     /* unicode (original) line buffer */
    const jschar    *uctokenptr;    /* unicode (original) token pointer */
    uintN           flags;          /* error/warning, etc. */
    uintN           errorNumber;    /* the error number, e.g. see js.msg */
    const jschar    *ucmessage;     /* the (default) error message */
    const jschar    **messageArgs;  /* arguments for the error message */
};

/*
 * JSErrorReport flag values.  These may be freely composed.
 */
#define JSREPORT_ERROR      0x0     /* pseudo-flag for default case */
#define JSREPORT_WARNING    0x1     /* reported via JS_ReportWarning */
#define JSREPORT_EXCEPTION  0x2     /* exception was thrown */
#define JSREPORT_STRICT     0x4     /* error or warning due to strict option */

/*
 * This condition is an error in strict mode code, a warning if
 * JS_HAS_STRICT_OPTION(cx), and otherwise should not be reported at
 * all.  We check the strictness of the context's top frame's script;
 * where that isn't appropriate, the caller should do the right checks
 * itself instead of using this flag.
 */
#define JSREPORT_STRICT_MODE_ERROR 0x8

/*
 * If JSREPORT_EXCEPTION is set, then a JavaScript-catchable exception
 * has been thrown for this runtime error, and the host should ignore it.
 * Exception-aware hosts should also check for JS_IsExceptionPending if
 * JS_ExecuteScript returns failure, and signal or propagate the exception, as
 * appropriate.
 */
#define JSREPORT_IS_WARNING(flags)      (((flags) & JSREPORT_WARNING) != 0)
#define JSREPORT_IS_EXCEPTION(flags)    (((flags) & JSREPORT_EXCEPTION) != 0)
#define JSREPORT_IS_STRICT(flags)       (((flags) & JSREPORT_STRICT) != 0)
#define JSREPORT_IS_STRICT_MODE_ERROR(flags) (((flags) &                      \
                                              JSREPORT_STRICT_MODE_ERROR) != 0)

extern JS_PUBLIC_API(JSErrorReporter)
JS_SetErrorReporter(JSContext *cx, JSErrorReporter er);

/************************************************************************/

/*
 * Dates.
 */

extern JS_PUBLIC_API(JSObject *)
JS_NewDateObject(JSContext *cx, int year, int mon, int mday, int hour, int min, int sec);

extern JS_PUBLIC_API(JSObject *)
JS_NewDateObjectMsec(JSContext *cx, jsdouble msec);

/*
 * Infallible predicate to test whether obj is a date object.
 */
extern JS_PUBLIC_API(JSBool)
JS_ObjectIsDate(JSContext *cx, JSObject *obj);

/************************************************************************/

/*
 * Regular Expressions.
 */
#define JSREG_FOLD      0x01    /* fold uppercase to lowercase */
#define JSREG_GLOB      0x02    /* global exec, creates array of matches */
#define JSREG_MULTILINE 0x04    /* treat ^ and $ as begin and end of line */
#define JSREG_STICKY    0x08    /* only match starting at lastIndex */

extern JS_PUBLIC_API(JSObject *)
JS_NewRegExpObject(JSContext *cx, JSObject *obj, char *bytes, size_t length, uintN flags);

extern JS_PUBLIC_API(JSObject *)
JS_NewUCRegExpObject(JSContext *cx, JSObject *obj, jschar *chars, size_t length, uintN flags);

extern JS_PUBLIC_API(void)
JS_SetRegExpInput(JSContext *cx, JSObject *obj, JSString *input, JSBool multiline);

extern JS_PUBLIC_API(void)
JS_ClearRegExpStatics(JSContext *cx, JSObject *obj);

extern JS_PUBLIC_API(JSBool)
JS_ExecuteRegExp(JSContext *cx, JSObject *obj, JSObject *reobj, jschar *chars, size_t length,
                 size_t *indexp, JSBool test, jsval *rval);

/* RegExp interface for clients without a global object. */

extern JS_PUBLIC_API(JSObject *)
JS_NewRegExpObjectNoStatics(JSContext *cx, char *bytes, size_t length, uintN flags);

extern JS_PUBLIC_API(JSObject *)
JS_NewUCRegExpObjectNoStatics(JSContext *cx, jschar *chars, size_t length, uintN flags);

extern JS_PUBLIC_API(JSBool)
JS_ExecuteRegExpNoStatics(JSContext *cx, JSObject *reobj, jschar *chars, size_t length,
                          size_t *indexp, JSBool test, jsval *rval);

extern JS_PUBLIC_API(JSBool)
JS_ObjectIsRegExp(JSContext *cx, JSObject *obj);

extern JS_PUBLIC_API(uintN)
JS_GetRegExpFlags(JSContext *cx, JSObject *obj);

extern JS_PUBLIC_API(JSString *)
JS_GetRegExpSource(JSContext *cx, JSObject *obj);

/************************************************************************/

extern JS_PUBLIC_API(JSBool)
JS_IsExceptionPending(JSContext *cx);

extern JS_PUBLIC_API(JSBool)
JS_GetPendingException(JSContext *cx, jsval *vp);

extern JS_PUBLIC_API(void)
JS_SetPendingException(JSContext *cx, jsval v);

extern JS_PUBLIC_API(void)
JS_ClearPendingException(JSContext *cx);

extern JS_PUBLIC_API(JSBool)
JS_ReportPendingException(JSContext *cx);

/*
 * Save the current exception state.  This takes a snapshot of cx's current
 * exception state without making any change to that state.
 *
 * The returned state pointer MUST be passed later to JS_RestoreExceptionState
 * (to restore that saved state, overriding any more recent state) or else to
 * JS_DropExceptionState (to free the state struct in case it is not correct
 * or desirable to restore it).  Both Restore and Drop free the state struct,
 * so callers must stop using the pointer returned from Save after calling the
 * Release or Drop API.
 */
extern JS_PUBLIC_API(JSExceptionState *)
JS_SaveExceptionState(JSContext *cx);

extern JS_PUBLIC_API(void)
JS_RestoreExceptionState(JSContext *cx, JSExceptionState *state);

extern JS_PUBLIC_API(void)
JS_DropExceptionState(JSContext *cx, JSExceptionState *state);

/*
 * If the given value is an exception object that originated from an error,
 * the exception will contain an error report struct, and this API will return
 * the address of that struct.  Otherwise, it returns NULL.  The lifetime of
 * the error report struct that might be returned is the same as the lifetime
 * of the exception object.
 */
extern JS_PUBLIC_API(JSErrorReport *)
JS_ErrorFromException(JSContext *cx, jsval v);

/*
 * Given a reported error's message and JSErrorReport struct pointer, throw
 * the corresponding exception on cx.
 */
extern JS_PUBLIC_API(JSBool)
JS_ThrowReportedError(JSContext *cx, const char *message,
                      JSErrorReport *reportp);

/*
 * Throws a StopIteration exception on cx.
 */
extern JS_PUBLIC_API(JSBool)
JS_ThrowStopIteration(JSContext *cx);

/*
 * Associate the current thread with the given context.  This is done
 * implicitly by JS_NewContext.
 *
 * Returns the old thread id for this context, which should be treated as
 * an opaque value.  This value is provided for comparison to 0, which
 * indicates that ClearContextThread has been called on this context
 * since the last SetContextThread, or non-0, which indicates the opposite.
 */
extern JS_PUBLIC_API(jsword)
JS_GetContextThread(JSContext *cx);

extern JS_PUBLIC_API(jsword)
JS_SetContextThread(JSContext *cx);

extern JS_PUBLIC_API(jsword)
JS_ClearContextThread(JSContext *cx);

/*
 * A JS runtime always has an "owner thread". The owner thread is set when the
 * runtime is created (to the current thread) and practically all entry points
 * into the JS engine check that a runtime (or anything contained in the
 * runtime: context, compartment, object, etc) is only touched by its owner
 * thread. Embeddings may check this invariant outside the JS engine by calling
 * JS_AbortIfWrongThread (which will abort if not on the owner thread, even for
 * non-debug builds).
 *
 * It is possible to "move" a runtime between threads. This is accomplished by
 * calling JS_ClearRuntimeThread on a runtime's owner thread and then calling
 * JS_SetRuntimeThread on the new owner thread. The runtime must not be
 * accessed between JS_ClearRuntimeThread and JS_SetRuntimeThread. Also, the
 * caller is responsible for synchronizing the calls to Set/Clear.
 */

extern JS_PUBLIC_API(void)
JS_AbortIfWrongThread(JSRuntime *rt);

extern JS_PUBLIC_API(void)
JS_ClearRuntimeThread(JSRuntime *rt);

extern JS_PUBLIC_API(void)
JS_SetRuntimeThread(JSRuntime *rt);

#ifdef __cplusplus
JS_END_EXTERN_C

class JSAutoSetRuntimeThread
{
    JSRuntime *runtime;

  public:
    JSAutoSetRuntimeThread(JSRuntime *runtime) : runtime(runtime) {
        JS_SetRuntimeThread(runtime);
    }

    ~JSAutoSetRuntimeThread() {
        JS_ClearRuntimeThread(runtime);
    }
};

JS_BEGIN_EXTERN_C
#endif

/************************************************************************/

/*
 * JS_IsConstructing must be called from within a native given the
 * native's original cx and vp arguments. If JS_IsConstructing is true,
 * JS_THIS must not be used; the constructor should construct and return a
 * new object. Otherwise, the native is called as an ordinary function and
 * JS_THIS may be used.
 */
static JS_ALWAYS_INLINE JSBool
JS_IsConstructing(JSContext *cx, const jsval *vp)
{
#ifdef DEBUG
    JSObject *callee = JSVAL_TO_OBJECT(JS_CALLEE(cx, vp));
    if (JS_ObjectIsFunction(cx, callee)) {
        JSFunction *fun = JS_ValueToFunction(cx, JS_CALLEE(cx, vp));
        JS_ASSERT((JS_GetFunctionFlags(fun) & JSFUN_CONSTRUCTOR) != 0);
    } else {
        JS_ASSERT(JS_GET_CLASS(cx, callee)->construct != NULL);
    }
#endif

    return JSVAL_IS_MAGIC_IMPL(JSVAL_TO_IMPL(vp[1]));
}

/*
 * In the case of a constructor called from JS_ConstructObject and
 * JS_InitClass where the class has the JSCLASS_CONSTRUCT_PROTOTYPE flag set,
 * the JS engine passes the constructor a non-standard 'this' object. In such
 * cases, the following query provides the additional information of whether a
 * special 'this' was supplied. E.g.:
 *
 *   JSBool foo_native(JSContext *cx, uintN argc, jsval *vp) {
 *     JSObject *maybeThis;
 *     if (JS_IsConstructing_PossiblyWithGivenThisObject(cx, vp, &maybeThis)) {
 *       // native called as a constructor
 *       if (maybeThis)
 *         // native called as a constructor with maybeThis as 'this'
 *     } else {
 *       // native called as function, maybeThis is still uninitialized
 *     }
 *   }
 *
 * Note that embeddings do not need to use this query unless they use the
 * aforementioned API/flags.
 */
static JS_ALWAYS_INLINE JSBool
JS_IsConstructing_PossiblyWithGivenThisObject(JSContext *cx, const jsval *vp,
                                              JSObject **maybeThis)
{
    jsval_layout l;
    JSBool isCtor;

#ifdef DEBUG
    JSObject *callee = JSVAL_TO_OBJECT(JS_CALLEE(cx, vp));
    if (JS_ObjectIsFunction(cx, callee)) {
        JSFunction *fun = JS_ValueToFunction(cx, JS_CALLEE(cx, vp));
        JS_ASSERT((JS_GetFunctionFlags(fun) & JSFUN_CONSTRUCTOR) != 0);
    } else {
        JS_ASSERT(JS_GET_CLASS(cx, callee)->construct != NULL);
    }
#endif

    isCtor = JSVAL_IS_MAGIC_IMPL(JSVAL_TO_IMPL(vp[1]));
    if (isCtor)
        *maybeThis = MAGIC_JSVAL_TO_OBJECT_OR_NULL_IMPL(l);
    return isCtor;
}

/*
 * If a constructor does not have any static knowledge about the type of
 * object to create, it can request that the JS engine create a default new
 * 'this' object, as is done for non-constructor natives when called with new.
 */
extern JS_PUBLIC_API(JSObject *)
JS_NewObjectForConstructor(JSContext *cx, const jsval *vp);

/************************************************************************/

#ifdef DEBUG
#define JS_GC_ZEAL 1
#endif

#ifdef JS_GC_ZEAL
#define JS_DEFAULT_ZEAL_FREQ 100

extern JS_PUBLIC_API(void)
JS_SetGCZeal(JSContext *cx, uint8 zeal, uint32 frequency, JSBool compartment);

extern JS_PUBLIC_API(void)
JS_ScheduleGC(JSContext *cx, uint32 count, JSBool compartment);
#endif

/*
 * Convert a uint32 index into a jsid.
 */
extern JS_PUBLIC_API(JSBool)
JS_IndexToId(JSContext *cx, uint32 index, jsid *id);

JS_END_EXTERN_C

#endif /* jsapi_h___ */
