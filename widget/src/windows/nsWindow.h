/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Robert O'Callahan <roc+moz@cs.cmu.edu>
 *   Dean Tessman <dean_tessman@hotmail.com>
 *   Makoto Kato  <m_kato@ga2.so-net.ne.jp>
 *   Dainis Jonitis <Dainis_Jonitis@swh-t.lv>
 *   Masayuki Nakano <masayuki@d-toybox.com>
 *   Ningjie Chen <chenn@email.uc.edu>
 *   Jim Mathies <jmathies@mozilla.com>.
 *   Mats Palmgren <matspal@gmail.com>
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

#ifndef Window_h__
#define Window_h__

/*
 * nsWindow - Native window management and event handling.
 */

#include "nsAutoPtr.h"
#include "nsBaseWidget.h"
#include "nsdefs.h"
#include "nsIdleService.h"
#include "nsToolkit.h"
#include "nsString.h"
#include "nsTArray.h"
#include "gfxWindowsSurface.h"
#include "nsWindowDbg.h"
#include "cairo.h"
#include "nsITimer.h"
#ifdef CAIRO_HAS_D2D_SURFACE
#include "gfxD2DSurface.h"
#endif

#include "nsWinGesture.h"

#include "WindowHook.h"
#include "TaskbarWindowPreview.h"

#ifdef ACCESSIBILITY
#include "OLEACC.H"
#include "nsAccessible.h"
#endif

#include "nsUXThemeData.h"

#include "nsIDOMMouseEvent.h"

/**
 * Forward class definitions
 */

class nsNativeDragTarget;
class nsIRollupListener;
class nsIFile;
class imgIContainer;

/**
 * Native WIN32 window wrapper.
 */

class nsWindow : public nsBaseWidget
{
  typedef mozilla::widget::WindowHook WindowHook;
#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_WIN7
  typedef mozilla::widget::TaskbarWindowPreview TaskbarWindowPreview;
#endif
public:
  nsWindow();
  virtual ~nsWindow();

  NS_DECL_ISUPPORTS_INHERITED

  friend class nsWindowGfx;

  /**
   * nsIWidget interface
   */
  NS_IMETHOD              Create(nsIWidget *aParent,
                                 nsNativeWidget aNativeParent,
                                 const nsIntRect &aRect,
                                 EVENT_CALLBACK aHandleEventFunction,
                                 nsDeviceContext *aContext,
                                 nsWidgetInitData *aInitData = nsnull);
  NS_IMETHOD              Destroy();
  NS_IMETHOD              SetParent(nsIWidget *aNewParent);
  virtual nsIWidget*      GetParent(void);
  virtual float           GetDPI();
  NS_IMETHOD              Show(bool bState);
  NS_IMETHOD              IsVisible(bool & aState);
  NS_IMETHOD              ConstrainPosition(bool aAllowSlop, PRInt32 *aX, PRInt32 *aY);
  NS_IMETHOD              Move(PRInt32 aX, PRInt32 aY);
  NS_IMETHOD              Resize(PRInt32 aWidth, PRInt32 aHeight, bool aRepaint);
  NS_IMETHOD              Resize(PRInt32 aX, PRInt32 aY, PRInt32 aWidth, PRInt32 aHeight, bool aRepaint);
  NS_IMETHOD              ResizeClient(PRInt32 aX, PRInt32 aY, PRInt32 aWidth, PRInt32 aHeight, bool aRepaint);
  NS_IMETHOD              BeginResizeDrag(nsGUIEvent* aEvent, PRInt32 aHorizontal, PRInt32 aVertical);
  NS_IMETHOD              PlaceBehind(nsTopLevelWidgetZPlacement aPlacement, nsIWidget *aWidget, bool aActivate);
  NS_IMETHOD              SetSizeMode(PRInt32 aMode);
  NS_IMETHOD              Enable(bool aState);
  NS_IMETHOD              IsEnabled(bool *aState);
  NS_IMETHOD              SetFocus(bool aRaise);
  NS_IMETHOD              GetBounds(nsIntRect &aRect);
  NS_IMETHOD              GetScreenBounds(nsIntRect &aRect);
  NS_IMETHOD              GetClientBounds(nsIntRect &aRect);
  virtual nsIntPoint      GetClientOffset();
  NS_IMETHOD              SetBackgroundColor(const nscolor &aColor);
  NS_IMETHOD              SetCursor(imgIContainer* aCursor,
                                    PRUint32 aHotspotX, PRUint32 aHotspotY);
  NS_IMETHOD              SetCursor(nsCursor aCursor);
  virtual nsresult        ConfigureChildren(const nsTArray<Configuration>& aConfigurations);
  NS_IMETHOD              MakeFullScreen(bool aFullScreen);
  NS_IMETHOD              HideWindowChrome(bool aShouldHide);
  NS_IMETHOD              Invalidate(bool aIsSynchronous, 
                                     bool aEraseBackground = false,
                                     bool aUpdateNCArea = false,
                                     bool aIncludeChildren = false);
  NS_IMETHOD              Invalidate(const nsIntRect & aRect, bool aIsSynchronous);
  NS_IMETHOD              Update();
  virtual void*           GetNativeData(PRUint32 aDataType);
  virtual void            FreeNativeData(void * data, PRUint32 aDataType);
  NS_IMETHOD              SetTitle(const nsAString& aTitle);
  NS_IMETHOD              SetIcon(const nsAString& aIconSpec);
  virtual nsIntPoint      WidgetToScreenOffset();
  virtual nsIntSize       ClientToWindowSize(const nsIntSize& aClientSize);
  NS_IMETHOD              DispatchEvent(nsGUIEvent* event, nsEventStatus & aStatus);
  NS_IMETHOD              EnableDragDrop(bool aEnable);
  NS_IMETHOD              CaptureMouse(bool aCapture);
  NS_IMETHOD              CaptureRollupEvents(nsIRollupListener * aListener, nsIMenuRollup * aMenuRollup,
                                              bool aDoCapture, bool aConsumeRollupEvent);
  NS_IMETHOD              GetAttention(PRInt32 aCycleCount);
  virtual bool            HasPendingInputEvent();
  virtual LayerManager*   GetLayerManager(PLayersChild* aShadowManager = nsnull,
                                          LayersBackend aBackendHint = LayerManager::LAYERS_NONE,
                                          LayerManagerPersistence aPersistence = LAYER_MANAGER_CURRENT,
                                          bool* aAllowRetaining = nsnull);
  gfxASurface             *GetThebesSurface();
  NS_IMETHOD              OnDefaultButtonLoaded(const nsIntRect &aButtonRect);
  NS_IMETHOD              OverrideSystemMouseScrollSpeed(PRInt32 aOriginalDelta, bool aIsHorizontal, PRInt32 &aOverriddenDelta);

  virtual nsresult        SynthesizeNativeKeyEvent(PRInt32 aNativeKeyboardLayout,
                                                   PRInt32 aNativeKeyCode,
                                                   PRUint32 aModifierFlags,
                                                   const nsAString& aCharacters,
                                                   const nsAString& aUnmodifiedCharacters);
  virtual nsresult        SynthesizeNativeMouseEvent(nsIntPoint aPoint,
                                                     PRUint32 aNativeMessage,
                                                     PRUint32 aModifierFlags);
  NS_IMETHOD              ResetInputState();
  NS_IMETHOD              SetIMEOpenState(bool aState);
  NS_IMETHOD              GetIMEOpenState(bool* aState);
  NS_IMETHOD              SetInputMode(const IMEContext& aContext);
  NS_IMETHOD              GetInputMode(IMEContext& aContext);
  NS_IMETHOD              CancelIMEComposition();
  NS_IMETHOD              GetToggledKeyState(PRUint32 aKeyCode, bool* aLEDState);
  NS_IMETHOD              RegisterTouchWindow();
  NS_IMETHOD              UnregisterTouchWindow();
#ifdef MOZ_XUL
  virtual void            SetTransparencyMode(nsTransparencyMode aMode);
  virtual nsTransparencyMode GetTransparencyMode();
  virtual void            UpdateOpaqueRegion(const nsIntRegion& aOpaqueRegion);
#endif // MOZ_XUL
#ifdef NS_ENABLE_TSF
  NS_IMETHOD              OnIMEFocusChange(bool aFocus);
  NS_IMETHOD              OnIMETextChange(PRUint32 aStart, PRUint32 aOldEnd, PRUint32 aNewEnd);
  NS_IMETHOD              OnIMESelectionChange(void);
#endif // NS_ENABLE_TSF
  NS_IMETHOD              GetNonClientMargins(nsIntMargin &margins);
  NS_IMETHOD              SetNonClientMargins(nsIntMargin &margins);
  void                    SetDrawsInTitlebar(bool aState);

  /**
   * Statics used in other classes
   */
  static PRInt32          GetWindowsVersion();
  static bool             GetRegistryKey(HKEY aRoot,
                                         const PRUnichar* aKeyName,
                                         const PRUnichar* aValueName,
                                         PRUnichar* aBuffer,
                                         DWORD aBufferLength);

  /**
   * Event helpers
   */
  void                    InitEvent(nsGUIEvent& event, nsIntPoint* aPoint = nsnull);
  virtual bool            DispatchMouseEvent(PRUint32 aEventType, WPARAM wParam,
                                             LPARAM lParam,
                                             bool aIsContextMenuKey = false,
                                             PRInt16 aButton = nsMouseEvent::eLeftButton,
                                             PRUint16 aInputSource = nsIDOMMouseEvent::MOZ_SOURCE_MOUSE);
  virtual bool            DispatchWindowEvent(nsGUIEvent* event);
  virtual bool            DispatchWindowEvent(nsGUIEvent*event, nsEventStatus &aStatus);
  virtual bool            DispatchKeyEvent(PRUint32 aEventType, WORD aCharCode,
                                           const nsTArray<nsAlternativeCharCode>* aAlternativeChars,
                                           UINT aVirtualCharCode, const MSG *aMsg,
                                           const nsModifierKeyState &aModKeyState,
                                           PRUint32 aFlags = 0);
  void                    DispatchPendingEvents();
  bool                    DispatchPluginEvent(UINT aMessage,
                                              WPARAM aWParam,
                                              LPARAM aLParam,
                                              bool aDispatchPendingEvents);

  void                    SuppressBlurEvents(bool aSuppress); // Called from nsFilePicker
  bool                    BlurEventsSuppressed();
#ifdef ACCESSIBILITY
  nsAccessible* DispatchAccessibleEvent(PRUint32 aEventType);
  nsAccessible* GetRootAccessible();
#endif // ACCESSIBILITY

  /**
   * Window utilities
   */
  nsWindow*               GetTopLevelWindow(bool aStopOnDialogOrPopup);
  static HWND             GetTopLevelHWND(HWND aWnd, 
                                          bool aStopIfNotChild = false, 
                                          bool aStopIfNotPopup = true);
  HWND                    GetWindowHandle() { return mWnd; }
  WNDPROC                 GetPrevWindowProc() { return mPrevWndProc; }
  static nsWindow*        GetNSWindowPtr(HWND aWnd);
  WindowHook&             GetWindowHook() { return mWindowHook; }
  nsWindow*               GetParentWindow(bool aIncludeOwner);
  // Get an array of all nsWindow*s on the main thread.
  typedef void            (WindowEnumCallback)(nsWindow*);
  static void             EnumAllWindows(WindowEnumCallback aCallback);

  /**
   * Misc.
   */
  virtual bool            AutoErase(HDC dc);
  nsIntPoint*             GetLastPoint() { return &mLastPoint; }
  // needed in nsIMM32Handler.cpp
  bool                    PluginHasFocus() { return mIMEContext.mStatus == nsIWidget::IME_STATUS_PLUGIN; }
  bool                    IsTopLevelWidget() { return mIsTopWidgetWindow; }
  /**
   * Start allowing Direct3D9 to be used by widgets when GetLayerManager is
   * called.
   *
   * @param aReinitialize Call GetLayerManager on widgets to ensure D3D9 is
   *                      initialized, this is usually called when this function
   *                      is triggered by timeout and not user/web interaction.
   */
  static void             StartAllowingD3D9(bool aReinitialize);

  /**
   * AssociateDefaultIMC() associates or disassociates the default IMC for
   * the window.
   *
   * @param aAssociate    TRUE, associates the default IMC with the window.
   *                      Otherwise, disassociates the default IMC from the
   *                      window.
   * @return              TRUE if this method associated the default IMC with
   *                      disassociated window or disassociated the default IMC
   *                      from associated window.
   *                      Otherwise, i.e., if this method did nothing actually,
   *                      FALSE.
   */
  bool                    AssociateDefaultIMC(bool aAssociate);

#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_WIN7
  bool HasTaskbarIconBeenCreated() { return mHasTaskbarIconBeenCreated; }
  // Called when either the nsWindow or an nsITaskbarTabPreview receives the noticiation that this window
  // has its icon placed on the taskbar.
  void SetHasTaskbarIconBeenCreated(bool created = true) { mHasTaskbarIconBeenCreated = created; }

  // Getter/setter for the nsITaskbarWindowPreview for this nsWindow
  already_AddRefed<nsITaskbarWindowPreview> GetTaskbarPreview() {
    nsCOMPtr<nsITaskbarWindowPreview> preview(do_QueryReferent(mTaskbarPreview));
    return preview.forget();
  }
  void SetTaskbarPreview(nsITaskbarWindowPreview *preview) { mTaskbarPreview = do_GetWeakReference(preview); }
#endif

  NS_IMETHOD              ReparentNativeWidget(nsIWidget* aNewParent);

  // Open file picker tracking
  void                    PickerOpen();
  void                    PickerClosed();

protected:

  // A magic number to identify the FAKETRACKPOINTSCROLLABLE window created
  // when the trackpoint hack is enabled.
  enum { eFakeTrackPointScrollableID = 0x46545053 };

  /**
   * Callbacks
   */
  static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK WindowProcInternal(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

  static BOOL CALLBACK    BroadcastMsgToChildren(HWND aWnd, LPARAM aMsg);
  static BOOL CALLBACK    BroadcastMsg(HWND aTopWindow, LPARAM aMsg);
  static BOOL CALLBACK    DispatchStarvedPaints(HWND aTopWindow, LPARAM aMsg);
  static BOOL CALLBACK    RegisterTouchForDescendants(HWND aTopWindow, LPARAM aMsg);
  static BOOL CALLBACK    UnregisterTouchForDescendants(HWND aTopWindow, LPARAM aMsg);
  static LRESULT CALLBACK MozSpecialMsgFilter(int code, WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK MozSpecialWndProc(int code, WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK MozSpecialMouseProc(int code, WPARAM wParam, LPARAM lParam);
  static VOID    CALLBACK HookTimerForPopups( HWND hwnd, UINT uMsg, UINT idEvent, DWORD dwTime );
  static BOOL    CALLBACK ClearResourcesCallback(HWND aChild, LPARAM aParam);
  static BOOL    CALLBACK EnumAllChildWindProc(HWND aWnd, LPARAM aParam);
  static BOOL    CALLBACK EnumAllThreadWindowProc(HWND aWnd, LPARAM aParam);
  static void             AllowD3D9Callback(nsWindow *aWindow);
  static void             AllowD3D9WithReinitializeCallback(nsWindow *aWindow);
  static BOOL CALLBACK    FindOurWindowAtPointCallback(HWND aHWND, LPARAM aLPARAM);

  /**
   * Window utilities
   */
  static BOOL             SetNSWindowPtr(HWND aWnd, nsWindow * ptr);
  static PRInt32          GetMonitorCount();
  LPARAM                  lParamToScreen(LPARAM lParam);
  LPARAM                  lParamToClient(LPARAM lParam);
  virtual void            SubclassWindow(BOOL bState);
  bool                    CanTakeFocus();
  bool                    UpdateNonClientMargins(PRInt32 aSizeMode = -1, bool aReflowWindow = true);
  void                    UpdateGetWindowInfoCaptionStatus(bool aActiveCaption);
  void                    ResetLayout();
  void                    InvalidateNonClientRegion();
  HRGN                    ExcludeNonClientFromPaintRegion(HRGN aRegion);
  static void             InitInputWorkaroundPrefDefaults();
  static bool             GetInputWorkaroundPref(const char* aPrefName, bool aValueIfAutomatic);
  static bool             UseTrackPointHack();
  static void             PerformElantechSwipeGestureHack(UINT& aVirtualKeyCode, nsModifierKeyState& aModKeyState);
  static void             GetMainWindowClass(nsAString& aClass);
  bool                    HasGlass() const {
    return mTransparencyMode == eTransparencyGlass ||
           mTransparencyMode == eTransparencyBorderlessGlass;
  }
  static bool             IsOurProcessWindow(HWND aHWND);
  static HWND             FindOurProcessWindow(HWND aHWND);
  static HWND             FindOurWindowAtPoint(const POINT& aPoint);

  /**
   * Event processing helpers
   */
  bool                    DispatchPluginEvent(const MSG &aMsg);
  bool                    DispatchFocusToTopLevelWindow(PRUint32 aEventType);
  bool                    DispatchFocus(PRUint32 aEventType);
  bool                    DispatchStandardEvent(PRUint32 aMsg);
  bool                    DispatchCommandEvent(PRUint32 aEventCommand);
  void                    RelayMouseEvent(UINT aMsg, WPARAM wParam, LPARAM lParam);
  static void             RemoveNextCharMessage(HWND aWnd);
  void                    RemoveMessageAndDispatchPluginEvent(UINT aFirstMsg,
                            UINT aLastMsg,
                            nsFakeCharMessage* aFakeCharMessage = nsnull);
  static MSG              InitMSG(UINT aMessage, WPARAM wParam, LPARAM lParam);
  virtual bool            ProcessMessage(UINT msg, WPARAM &wParam,
                                         LPARAM &lParam, LRESULT *aRetValue);
  bool                    ProcessMessageForPlugin(const MSG &aMsg,
                                                  LRESULT *aRetValue, bool &aCallDefWndProc);
  LRESULT                 ProcessCharMessage(const MSG &aMsg,
                                             bool *aEventDispatched);
  LRESULT                 ProcessKeyUpMessage(const MSG &aMsg,
                                              bool *aEventDispatched);
  LRESULT                 ProcessKeyDownMessage(const MSG &aMsg,
                                                bool *aEventDispatched);
  static bool             EventIsInsideWindow(UINT Msg, nsWindow* aWindow);
  // Convert nsEventStatus value to a windows boolean
  static bool             ConvertStatus(nsEventStatus aStatus);
  static void             PostSleepWakeNotification(const char* aNotification);
  PRInt32                 ClientMarginHitTestPoint(PRInt32 mx, PRInt32 my);
  static WORD             GetScanCode(LPARAM aLParam)
  {
    return (aLParam >> 16) & 0xFF;
  }
  static bool             IsExtendedScanCode(LPARAM aLParam)
  {
    return (aLParam & 0x1000000) != 0;
  }
  static bool             IsRedirectedKeyDownMessage(const MSG &aMsg);
  static void             ForgetRedirectedKeyDownMessage()
  {
    sRedirectedKeyDown.message = WM_NULL;
  }

  /**
   * Event handlers
   */
  virtual void            OnDestroy();
  virtual bool            OnMove(PRInt32 aX, PRInt32 aY);
  virtual bool            OnResize(nsIntRect &aWindowRect);
  LRESULT                 OnChar(const MSG &aMsg,
                                 nsModifierKeyState &aModKeyState,
                                 bool *aEventDispatched,
                                 PRUint32 aFlags = 0);
  LRESULT                 OnKeyDown(const MSG &aMsg,
                                    nsModifierKeyState &aModKeyState,
                                    bool *aEventDispatched,
                                    nsFakeCharMessage* aFakeCharMessage);
  LRESULT                 OnKeyUp(const MSG &aMsg,
                                  nsModifierKeyState &aModKeyState,
                                  bool *aEventDispatched);
  LRESULT                 OnCharRaw(UINT charCode, UINT aScanCode,
                                    nsModifierKeyState &aModKeyState,
                                    PRUint32 aFlags = 0,
                                    const MSG *aMsg = nsnull,
                                    bool *aEventDispatched = nsnull);
  bool                    OnScroll(UINT aMsg, WPARAM aWParam, LPARAM aLParam);
  void                    OnScrollInternal(UINT aMsg, WPARAM aWParam,
                                           LPARAM aLParam);
  bool                    OnGesture(WPARAM wParam, LPARAM lParam);
#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_WIN7
  bool                    OnTouch(WPARAM wParam, LPARAM lParam);
#endif
  bool                    OnHotKey(WPARAM wParam, LPARAM lParam);
  BOOL                    OnInputLangChange(HKL aHKL);
  bool                    OnPaint(HDC aDC, PRUint32 aNestingLevel);
  void                    OnWindowPosChanged(WINDOWPOS *wp, bool& aResult);
  static UINT             GetInternalMessage(UINT aNativeMessage);
  static UINT             GetNativeMessage(UINT aInternalMessage);
  void                    OnMouseWheel(UINT aMsg, WPARAM aWParam,
                                       LPARAM aLParam, LRESULT *aRetValue);
  void                    OnMouseWheelInternal(UINT aMessage, WPARAM aWParam,
                                               LPARAM aLParam,
                                               LRESULT *aRetValue);
  void                    OnWindowPosChanging(LPWINDOWPOS& info);

  /**
   * Function that registers when the user has been active (used for detecting
   * when the user is idle).
   */
  void                    UserActivity();

  PRInt32                 GetHeight(PRInt32 aProposedHeight);
  void                    GetWindowClass(nsString& aWindowClass);
  void                    GetWindowPopupClass(nsString& aWindowClass);
  virtual DWORD           WindowStyle();
  DWORD                   WindowExStyle();

  void                    RegisterWindowClass(const nsString& aClassName,
                                              UINT aExtraStyle,
                                              LPWSTR aIconID);

  /**
   * XP and Vista theming support for windows with rounded edges
   */
  void                    ClearThemeRegion();
  void                    SetThemeRegion();

  /**
   * Popup hooks
   */
  static void             ScheduleHookTimer(HWND aWnd, UINT aMsgId);
  static void             RegisterSpecialDropdownHooks();
  static void             UnregisterSpecialDropdownHooks();
  static BOOL             DealWithPopups(HWND inWnd, UINT inMsg, WPARAM inWParam, LPARAM inLParam, LRESULT* outResult);

  /**
   * Window transparency helpers
   */
#ifdef MOZ_XUL
private:
  void                    SetWindowTranslucencyInner(nsTransparencyMode aMode);
  nsTransparencyMode      GetWindowTranslucencyInner() const { return mTransparencyMode; }
  void                    ResizeTranslucentWindow(PRInt32 aNewWidth, PRInt32 aNewHeight, bool force = false);
  nsresult                UpdateTranslucentWindow();
  void                    SetupTranslucentWindowMemoryBitmap(nsTransparencyMode aMode);
  void                    UpdateGlass();
protected:
#endif // MOZ_XUL

  static bool             IsAsyncResponseEvent(UINT aMsg, LRESULT& aResult);
  void                    IPCWindowProcHandler(UINT& msg, WPARAM& wParam, LPARAM& lParam);

  /**
   * Misc.
   */
  UINT                    MapFromNativeToDOM(UINT aNativeKeyCode);
  void                    StopFlashing();
  static bool             IsTopLevelMouseExit(HWND aWnd);
  static void             SetupKeyModifiersSequence(nsTArray<KeyPair>* aArray, PRUint32 aModifiers);
  nsresult                SetWindowClipRegion(const nsTArray<nsIntRect>& aRects,
                                              bool aIntersectWithExisting);
  nsIntRegion             GetRegionToPaint(bool aForceFullRepaint, 
                                           PAINTSTRUCT ps, HDC aDC);
  static void             ActivateOtherWindowHelper(HWND aWnd);
  static PRUint16         GetMouseInputSource();
#ifdef ACCESSIBILITY
  static STDMETHODIMP_(LRESULT) LresultFromObject(REFIID riid, WPARAM wParam, LPUNKNOWN pAcc);
#endif // ACCESSIBILITY
  void                    ClearCachedResources();

  nsPopupType PopupType() { return mPopupType; }

protected:
  nsCOMPtr<nsIWidget>   mParent;
  nsIntSize             mLastSize;
  nsIntPoint            mLastPoint;
  HWND                  mWnd;
  WNDPROC               mPrevWndProc;
  HBRUSH                mBrush;
  bool                  mIsTopWidgetWindow;
  bool                  mInDtor;
  bool                  mIsVisible;
  bool                  mUnicodeWidget;
  bool                  mPainting;
  bool                  mTouchWindow;
  bool                  mDisplayPanFeedback;
  bool                  mHideChrome;
  bool                  mIsRTL;
  bool                  mFullscreenMode;
  bool                  mMousePresent;
  bool                  mDestroyCalled;
  PRUint32              mBlurSuppressLevel;
  DWORD_PTR             mOldStyle;
  DWORD_PTR             mOldExStyle;
  IMEContext            mIMEContext;
  nsNativeDragTarget*   mNativeDragTarget;
  HKL                   mLastKeyboardLayout;
  nsPopupType           mPopupType;
  nsSizeMode            mOldSizeMode;
  WindowHook            mWindowHook;
  DWORD                 mAssumeWheelIsZoomUntil;
  PRUint32              mPickerDisplayCount;
  static bool           sDropShadowEnabled;
  static PRUint32       sInstanceCount;
  static TriStateBool   sCanQuit;
  static nsWindow*      sCurrentWindow;
  static BOOL           sIsOleInitialized;
  static HCURSOR        sHCursor;
  static imgIContainer* sCursorImgContainer;
  static bool           sSwitchKeyboardLayout;
  static bool           sJustGotDeactivate;
  static bool           sJustGotActivate;
  static bool           sIsInMouseCapture;
  static int            sTrimOnMinimize;
  static bool           sDefaultTrackPointHack;
  static const char*    sDefaultMainWindowClass;
  static bool           sUseElantechSwipeHack;
  static bool           sUseElantechPinchHack;
  static bool           sAllowD3D9;

  // Always use the helper method to read this property.  See bug 603793.
  static TriStateBool   sHasBogusPopupsDropShadowOnMultiMonitor;
  static bool           HasBogusPopupsDropShadowOnMultiMonitor();

  static PRUint32       sOOPPPluginFocusEvent;

  // Non-client margin settings
  // Pre-calculated outward offset applied to default frames
  nsIntMargin           mNonClientOffset;
  // Margins set by the owner
  nsIntMargin           mNonClientMargins;

  // Indicates custom frames are enabled
  bool                  mCustomNonClient;
  // Cached copy of L&F's resize border  
  PRInt32               mHorResizeMargin;
  PRInt32               mVertResizeMargin;
  // Height of the caption plus border
  PRInt32               mCaptionHeight;

  nsCOMPtr<nsIdleService> mIdleService;

  // Hook Data Memebers for Dropdowns. sProcessHook Tells the
  // hook methods whether they should be processing the hook
  // messages.
  static HHOOK          sMsgFilterHook;
  static HHOOK          sCallProcHook;
  static HHOOK          sCallMouseHook;
  static bool           sProcessHook;
  static UINT           sRollupMsgId;
  static HWND           sRollupMsgWnd;
  static UINT           sHookTimerId;

  // Rollup Listener
  static nsIWidget*     sRollupWidget;
  static bool           sRollupConsumeEvent;
  static nsIRollupListener* sRollupListener;
  static nsIMenuRollup* sMenuRollup;

  // Mouse Clicks - static variable definitions for figuring
  // out 1 - 3 Clicks.
  static POINT          sLastMousePoint;
  static POINT          sLastMouseMovePoint;
  static LONG           sLastMouseDownTime;
  static LONG           sLastClickCount;
  static BYTE           sLastMouseButton;

  // Graphics
  HDC                   mPaintDC; // only set during painting

#ifdef CAIRO_HAS_D2D_SURFACE
  nsRefPtr<gfxD2DSurface>    mD2DWindowSurface; // Surface for this window.
#endif

  // Transparency
#ifdef MOZ_XUL
  // Use layered windows to support full 256 level alpha translucency
  nsRefPtr<gfxASurface> mTransparentSurface;
  HDC                   mMemoryDC;
  nsTransparencyMode    mTransparencyMode;
#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
  nsIntRegion           mPossiblyTransparentRegion;
  MARGINS               mGlassMargins;
#endif // #if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
#endif // MOZ_XUL

  // Win7 Gesture processing and management
  nsWinGesture          mGesture;

#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_WIN7
  // Weak ref to the nsITaskbarWindowPreview associated with this window
  nsWeakPtr             mTaskbarPreview;
  // True if the taskbar (possibly through the tab preview) tells us that the
  // icon has been created on the taskbar.
  bool                  mHasTaskbarIconBeenCreated;
#endif

#ifdef ACCESSIBILITY
  static BOOL           sIsAccessibilityOn;
  static HINSTANCE      sAccLib;
  static LPFNLRESULTFROMOBJECT sLresultFromObject;
#endif // ACCESSIBILITY

  // sRedirectedKeyDown is WM_KEYDOWN message or WM_SYSKEYDOWN message which
  // was reirected to SendInput() API by OnKeyDown().
  static MSG            sRedirectedKeyDown;

  static bool sEnablePixelScrolling;
  static bool sNeedsToInitMouseWheelSettings;
  static ULONG sMouseWheelScrollLines;
  static ULONG sMouseWheelScrollChars;
  static void InitMouseWheelScrollData();

  static HWND sLastMouseWheelWnd;
  static PRInt32 sRemainingDeltaForScroll;
  static PRInt32 sRemainingDeltaForPixel;
  static bool sLastMouseWheelDeltaIsPositive;
  static bool sLastMouseWheelOrientationIsVertical;
  static bool sLastMouseWheelUnitIsPage;
  static PRUint32 sLastMouseWheelTime; // in milliseconds
  static void ResetRemainingWheelDelta();

  // If a window receives WM_KEYDOWN message or WM_SYSKEYDOWM message which is
  // redirected message, OnKeyDowm() prevents to dispatch NS_KEY_DOWN event
  // because it has been dispatched before the message was redirected.
  // However, in some cases, ProcessKeyDownMessage() doesn't call OnKeyDown().
  // Then, ProcessKeyDownMessage() needs to forget the redirected message and
  // remove WM_CHAR message or WM_SYSCHAR message for the redirected keydown
  // message.  AutoForgetRedirectedKeyDownMessage struct is a helper struct
  // for doing that.  This must be created in stack.
  struct AutoForgetRedirectedKeyDownMessage
  {
    AutoForgetRedirectedKeyDownMessage(nsWindow* aWindow, const MSG &aMsg) :
      mCancel(!nsWindow::IsRedirectedKeyDownMessage(aMsg)),
      mWindow(aWindow), mMsg(aMsg)
    {
    }

    ~AutoForgetRedirectedKeyDownMessage()
    {
      if (mCancel) {
        return;
      }
      // Prevent unnecessary keypress event
      if (!mWindow->mOnDestroyCalled) {
        nsWindow::RemoveNextCharMessage(mWindow->mWnd);
      }
      // Foreget the redirected message
      nsWindow::ForgetRedirectedKeyDownMessage();
    }

    bool mCancel;
    nsRefPtr<nsWindow> mWindow;
    const MSG &mMsg;
  };
};

/**
 * A child window is a window with different style.
 */
class ChildWindow : public nsWindow {

public:
  ChildWindow() {}

protected:
  virtual DWORD WindowStyle();
};

#endif // Window_h__
