/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
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
 * The Original Code is SpiderMonkey code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
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

#ifndef jsgcmark_h___
#define jsgcmark_h___

#include "jsgc.h"
#include "jscntxt.h"
#include "jscompartment.h"
#include "jslock.h"


#include "js/TemplateLib.h"

namespace js {
namespace gc {

void
MarkString(JSTracer *trc, JSString *str);

void
MarkString(JSTracer *trc, JSString *str, const char *name);

void
MarkObject(JSTracer *trc, JSObject &obj, const char *name);

/*
 * Mark an object that may be in a different compartment from the compartment
 * being GC'd. (Although it won't be marked if it's in the wrong compartment.)
 */
void
MarkCrossCompartmentObject(JSTracer *trc, JSObject &obj, const char *name);

void
MarkObjectWithPrinter(JSTracer *trc, JSObject &obj, JSTraceNamePrinter printer,
		      const void *arg, size_t index);

void
MarkScript(JSTracer *trc, JSScript *script, const char *name);

void
MarkShape(JSTracer *trc, const Shape *shape, const char *name);

void
MarkTypeObject(JSTracer *trc, types::TypeObject *type, const char *name);

void
MarkXML(JSTracer *trc, JSXML *xml, const char *name);

void
MarkObjectRange(JSTracer *trc, size_t len, JSObject **vec, const char *name);

void
MarkXMLRange(JSTracer *trc, size_t len, JSXML **vec, const char *name);

void
MarkId(JSTracer *trc, jsid id);

void
MarkId(JSTracer *trc, jsid id, const char *name);

void
MarkIdRange(JSTracer *trc, jsid *beg, jsid *end, const char *name);

void
MarkIdRange(JSTracer *trc, size_t len, jsid *vec, const char *name);

void
MarkKind(JSTracer *trc, void *thing, JSGCTraceKind kind);

void
MarkValueRaw(JSTracer *trc, const js::Value &v);

void
MarkValue(JSTracer *trc, const js::Value &v, const char *name);

/*
 * Mark a value that may be in a different compartment from the compartment
 * being GC'd. (Although it won't be marked if it's in the wrong compartment.)
 */
void
MarkCrossCompartmentValue(JSTracer *trc, const js::Value &v, const char *name);

void
MarkValueRange(JSTracer *trc, const Value *beg, const Value *end, const char *name);

void
MarkValueRange(JSTracer *trc, size_t len, const Value *vec, const char *name);

void
MarkShapeRange(JSTracer *trc, const Shape **beg, const Shape **end, const char *name);

void
MarkShapeRange(JSTracer *trc, size_t len, const Shape **vec, const char *name);

/* N.B. Assumes JS_SET_TRACING_NAME/INDEX has already been called. */
void
MarkGCThing(JSTracer *trc, void *thing, uint32 kind);

void
MarkGCThing(JSTracer *trc, void *thing);

void
MarkGCThing(JSTracer *trc, void *thing, const char *name);

void
MarkGCThing(JSTracer *trc, void *thing, const char *name, size_t index);

void
Mark(JSTracer *trc, void *thing, uint32 kind, const char *name);

void
MarkRoot(JSTracer *trc, JSObject *thing, const char *name);

void
MarkRoot(JSTracer *trc, JSString *thing, const char *name);

void
MarkRoot(JSTracer *trc, JSScript *thing, const char *name);

void
MarkRoot(JSTracer *trc, const Shape *thing, const char *name);

void
MarkRoot(JSTracer *trc, types::TypeObject *thing, const char *name);

void
MarkRoot(JSTracer *trc, JSXML *thing, const char *name);

void
MarkChildren(JSTracer *trc, JSObject *obj);

void
MarkChildren(JSTracer *trc, JSString *str);

void
MarkChildren(JSTracer *trc, const Shape *shape);

void
MarkChildren(JSTracer *trc, JSScript *script);

void
MarkChildren(JSTracer *trc, JSXML *xml);

/*
 * Marks all the children of a shape except the parent, which avoids using
 * unbounded stack space. Returns the parent.
 */
const Shape *
MarkShapeChildrenAcyclic(JSTracer *trc, const Shape *shape);

/*
 * Use function overloading to decide which function should be called based on
 * the type of the object. The static type is used at compile time to link to
 * the corresponding Mark/IsMarked function.
 */
inline void
Mark(JSTracer *trc, const js::Value &v, const char *name)
{
    MarkValue(trc, v, name);
}

inline void
Mark(JSTracer *trc, JSObject *o, const char *name)
{
    MarkObject(trc, *o, name);
}

inline bool
IsMarked(JSContext *cx, const js::Value &v)
{
    if (v.isMarkable())
        return !IsAboutToBeFinalized(cx, v.toGCThing());
    return true;
}

inline bool
IsMarked(JSContext *cx, JSObject *o)
{
    return !IsAboutToBeFinalized(cx, o);
}

inline bool
IsMarked(JSContext *cx, Cell *cell)
{
    return !IsAboutToBeFinalized(cx, cell);
}

}
}

#endif
