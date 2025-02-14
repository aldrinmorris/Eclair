/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=99:
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
 * The Original Code is SpiderMonkey.
 *
 * The Initial Developer of the Original Code is
 * the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#ifndef jsfuninlines_h___
#define jsfuninlines_h___

#include "jsfun.h"
#include "jsscript.h"

#include "vm/GlobalObject.h"

inline bool
js::IsConstructing(CallReceiver call)
{
    return IsConstructing(call.base());
}

inline bool
JSFunction::inStrictMode() const
{
    return script()->strictModeCode;
}

inline void
JSFunction::setMethodAtom(JSAtom *atom)
{
    JS_ASSERT(joinable());
    setSlot(METHOD_ATOM_SLOT, js::StringValue(atom));
}

inline JSObject *
CloneFunctionObject(JSContext *cx, JSFunction *fun, JSObject *parent,
                    bool ignoreSingletonClone /* = false */)
{
    JS_ASSERT(parent);
    JSObject *proto = parent->getGlobal()->getOrCreateFunctionPrototype(cx);
    if (!proto)
        return NULL;

    /*
     * For attempts to clone functions at a function definition opcode or from
     * a method barrier, don't perform the clone if the function has singleton
     * type. CloneFunctionObject was called pessimistically, and we need to
     * preserve the type's property that if it is singleton there is only a
     * single object with its type in existence.
     */
    if (ignoreSingletonClone && fun->hasSingletonType()) {
        JS_ASSERT(fun->getProto() == proto);
        fun->setParent(parent);
        return fun;
    }

    return js_CloneFunctionObject(cx, fun, parent, proto);
}

#endif /* jsfuninlines_h___ */
