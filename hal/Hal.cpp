/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: sw=2 ts=8 et ft=cpp : */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at:
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Code.
 *
 * The Initial Developer of the Original Code is
 *   The Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Chris Jones <jones.chris.g@gmail.com>
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

#include "Hal.h"
#include "mozilla/Util.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"
#include "mozilla/Observer.h"

#define PROXY_IF_SANDBOXED(_call)                 \
  do {                                            \
    if (InSandbox()) {                            \
      hal_sandbox::_call;                         \
    } else {                                      \
      hal_impl::_call;                            \
    }                                             \
  } while (0)

namespace mozilla {
namespace hal {

static void
AssertMainThread()
{
  MOZ_ASSERT(NS_IsMainThread());
}

static bool
InSandbox()
{
  return GeckoProcessType_Content == XRE_GetProcessType();
}

void
Vibrate(const nsTArray<uint32>& pattern)
{
  AssertMainThread();
  PROXY_IF_SANDBOXED(Vibrate(pattern));
}

class BatteryObserversManager
{
public:
  void AddObserver(BatteryObserver* aObserver) {
    if (!mObservers) {
      mObservers = new ObserverList<BatteryInformation>();
    }

    mObservers->AddObserver(aObserver);

    if (mObservers->Length() == 1) {
      PROXY_IF_SANDBOXED(EnableBatteryNotifications());
    }
  }

  void RemoveObserver(BatteryObserver* aObserver) {
    mObservers->RemoveObserver(aObserver);

    if (mObservers->Length() == 0) {
      PROXY_IF_SANDBOXED(DisableBatteryNotifications());

      delete mObservers;
      mObservers = 0;

      delete mBatteryInfo;
      mBatteryInfo = 0;
    }
  }

  void CacheBatteryInformation(const BatteryInformation& aBatteryInfo) {
    if (mBatteryInfo) {
      delete mBatteryInfo;
    }
    mBatteryInfo = new BatteryInformation(aBatteryInfo);
  }

  bool HasCachedBatteryInformation() const {
    return mBatteryInfo;
  }

  void GetCachedBatteryInformation(BatteryInformation* aBatteryInfo) const {
    *aBatteryInfo = *mBatteryInfo;
  }

  void Broadcast(const BatteryInformation& aBatteryInfo) {
    MOZ_ASSERT(mObservers);
    mObservers->Broadcast(aBatteryInfo);
  }

private:
  ObserverList<BatteryInformation>* mObservers;
  BatteryInformation*               mBatteryInfo;
};

static BatteryObserversManager sBatteryObservers;

void
RegisterBatteryObserver(BatteryObserver* aBatteryObserver)
{
  AssertMainThread();
  sBatteryObservers.AddObserver(aBatteryObserver);
}

void
UnregisterBatteryObserver(BatteryObserver* aBatteryObserver)
{
  AssertMainThread();
  sBatteryObservers.RemoveObserver(aBatteryObserver);
}

// EnableBatteryNotifications isn't defined on purpose.
// DisableBatteryNotifications isn't defined on purpose.

void
GetCurrentBatteryInformation(BatteryInformation* aBatteryInfo)
{
  AssertMainThread();

  if (sBatteryObservers.HasCachedBatteryInformation()) {
    sBatteryObservers.GetCachedBatteryInformation(aBatteryInfo);
  } else {
    PROXY_IF_SANDBOXED(GetCurrentBatteryInformation(aBatteryInfo));
    sBatteryObservers.CacheBatteryInformation(*aBatteryInfo);
  }
}

void NotifyBatteryChange(const BatteryInformation& aBatteryInfo)
{
  AssertMainThread();

  sBatteryObservers.CacheBatteryInformation(aBatteryInfo);
  sBatteryObservers.Broadcast(aBatteryInfo);
}

} // namespace hal
} // namespace mozilla
