/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40 -*- */
/* ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is worker threads.
 *
 * The Initial Developer of the Original Code is
 *   Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ben Turner <bent.mozilla@gmail.com> (Original Author)
 *   Ben Newman <b{enjamn,newman}@mozilla.com>
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

#ifndef __NSAUTOJSVALHOLDER_H__
#define __NSAUTOJSVALHOLDER_H__

#include "jsapi.h"

#include "nsDebug.h"

/**
 * Simple class that looks and acts like a jsval except that it unroots
 * itself automatically if Root() is ever called. Designed to be rooted on the
 * context or runtime (but not both!).
 */
class nsAutoJSValHolder
{
public:
  nsAutoJSValHolder()
    : mVal(JSVAL_NULL), mRt(nsnull)
  {
    // nothing to do
  }

  /**
   * Always release on destruction.
   */
  virtual ~nsAutoJSValHolder() {
    Release();
  }

  nsAutoJSValHolder(const nsAutoJSValHolder& aOther)
    : mVal(JSVAL_NULL), mRt(nsnull)
  {
    *this = aOther;
  }

  nsAutoJSValHolder& operator=(const nsAutoJSValHolder& aOther) {
    if (this != &aOther) {
      if (aOther.IsHeld()) {
        // XXX No error handling here...
        this->Hold(aOther.mRt);
      }
      else {
        this->Release();
      }
      *this = static_cast<jsval>(aOther);
    }
    return *this;
  }

  /**
   * Hold by rooting on the context's runtime.
   */
  bool Hold(JSContext* aCx) {
    return Hold(JS_GetRuntime(aCx));
  }

  /**
   * Hold by rooting on the runtime.
   * Note that mVal may be JSVAL_NULL, which is not a problem.
   */
  bool Hold(JSRuntime* aRt) {
    // Do we really care about different runtimes?
    if (mRt && aRt != mRt) {
      js_RemoveRoot(mRt, &mVal);
      mRt = nsnull;
    }

    if (!mRt && js_AddRootRT(aRt, &mVal, "nsAutoJSValHolder")) {
      mRt = aRt;
    }

    return !!mRt;
  }

  /**
   * Manually release, nullifying mVal, and mRt, but returning
   * the original jsval.
   */
  jsval Release() {
    jsval oldval = mVal;

    if (mRt) {
      js_RemoveRoot(mRt, &mVal); // infallible
      mRt = nsnull;
    }

    mVal = JSVAL_NULL;

    return oldval;
  }

  /**
   * Determine if Hold has been called.
   */
  bool IsHeld() const {
    return !!mRt;
  }

  /**
   * Explicit JSObject* conversion.
   */
  JSObject* ToJSObject() const {
    return JSVAL_IS_OBJECT(mVal)
         ? JSVAL_TO_OBJECT(mVal)
         : nsnull;
  }

  jsval* ToJSValPtr() {
    return &mVal;
  }

  /**
   * Pretend to be a jsval.
   */
  operator jsval() const { return mVal; }

  nsAutoJSValHolder &operator=(JSObject* aOther) {
    return *this = OBJECT_TO_JSVAL(aOther);
  }

  nsAutoJSValHolder &operator=(jsval aOther) {
#ifdef DEBUG
    if (JSVAL_IS_GCTHING(aOther) && !JSVAL_IS_NULL(aOther)) {
      NS_ASSERTION(IsHeld(), "Not rooted!");
    }
#endif
    mVal = aOther;
    return *this;
  }

private:
  jsval mVal;
  JSRuntime* mRt;
};

#endif /* __NSAUTOJSVALHOLDER_H__ */
