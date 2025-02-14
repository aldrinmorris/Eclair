#include "tests.h"
#include "jsatom.h"

using namespace mozilla;

BEGIN_TEST(testAtomizedIsNotInterned)
{
    /* Try to pick a string that won't be interned by other tests in this runtime. */
    static const char someChars[] = "blah blah blah? blah blah blah";
    JSAtom *atom = js_Atomize(cx, someChars, ArrayLength(someChars));
    CHECK(!JS_StringHasBeenInterned(cx, atom));
    CHECK(JS_InternJSString(cx, atom));
    CHECK(JS_StringHasBeenInterned(cx, atom));
    return true;
}
END_TEST(testAtomizedIsNotInterned)

struct StringWrapper
{
    JSString *str;
    bool     strOk;
} sw;

JSBool
GCCallback(JSContext *cx, JSGCStatus status, JSBool isFull)
{
    if (status == JSGC_MARK_END)
        sw.strOk = !JS_IsAboutToBeFinalized(cx, sw.str);
    return true;
}

BEGIN_TEST(testInternAcrossGC)
{
    sw.str = JS_InternString(cx, "wrapped chars that another test shouldn't be using");
    sw.strOk = false;
    CHECK(sw.str);
    JS_SetGCCallback(cx, GCCallback);
    JS_GC(cx);
    CHECK(sw.strOk);
    return true;
}
END_TEST(testInternAcrossGC)
