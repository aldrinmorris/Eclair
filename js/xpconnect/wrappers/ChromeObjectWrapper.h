/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99 ft=cpp:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __ChromeObjectWrapper_h__
#define __ChromeObjectWrapper_h__

#include "FilteringWrapper.h"
#include "AccessCheck.h"

namespace xpc {

// When chrome JS objects are exposed to content, they get a ChromeObjectWrapper.
//
// The base filtering wrapper here does most of the work for us. We define a
// custom class here to introduce custom behavior with respect to the prototype
// chain.
#define ChromeObjectWrapperBase \
  FilteringWrapper<js::CrossCompartmentSecurityWrapper, ExposedPropertiesOnly>

class ChromeObjectWrapper : public ChromeObjectWrapperBase
{
  public:
    ChromeObjectWrapper() : ChromeObjectWrapperBase(0) {};

    /* Custom traps. */
    virtual bool getPropertyDescriptor(JSContext *cx, JSObject *wrapper,
                                       jsid id, bool set,
                                       js::PropertyDescriptor *desc);
    virtual bool has(JSContext *cx, JSObject *wrapper, jsid id, bool *bp);
    virtual bool get(JSContext *cx, JSObject *wrapper, JSObject *receiver,
                     jsid id, js::Value *vp);

    // NB: One might think we'd need to implement enumerate(), keys(), iterate(),
    // and getPropertyNames() here. However, ES5 built-in properties aren't
    // enumerable (and SpiderMonkey's implementation seems to match the spec
    // modulo Error.prototype.fileName and Error.prototype.lineNumber). Since
    // we're only remapping the prototypes of standard objects, there would
    // never be anything more to enumerate up the prototype chain. So we can
    // atually skip these.

    static ChromeObjectWrapper singleton;
};

} /* namespace xpc */

#endif /* __ChromeObjectWrapper_h__ */
