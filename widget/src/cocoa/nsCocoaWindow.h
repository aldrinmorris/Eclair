/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 *   Josh Aas <josh@mozilla.com>
 *   Colin Barrett <cbarrett@mozilla.com>
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

#ifndef nsCocoaWindow_h_
#define nsCocoaWindow_h_

#undef DARWIN

#import <Cocoa/Cocoa.h>

#include "nsBaseWidget.h"
#include "nsPIWidgetCocoa.h"
#include "nsAutoPtr.h"
#include "nsCocoaUtils.h"

class nsCocoaWindow;
class nsChildView;
class nsMenuBarX;

// Value copied from BITMAP_MAX_AREA, used in nsNativeThemeCocoa.mm
#define CUIDRAW_MAX_AREA 500000

typedef struct _nsCocoaWindowList {
  _nsCocoaWindowList() : prev(NULL), window(NULL) {}
  struct _nsCocoaWindowList *prev;
  nsCocoaWindow *window; // Weak
} nsCocoaWindowList;

// NSWindow subclass that is the base class for all of our own window classes.
// Among other things, this class handles the storage of those settings that
// need to be persisted across window destruction and reconstruction, i.e. when
// switching to and from fullscreen mode.
// We don't save shadow, transparency mode or background color because it's not
// worth the hassle - Gecko will reset them anyway as soon as the window is
// resized.
@interface BaseWindow : NSWindow
{
  // Data Storage
  NSMutableDictionary* mState;
  BOOL mDrawsIntoWindowFrame;
  NSColor* mActiveTitlebarColor;
  NSColor* mInactiveTitlebarColor;

  // Shadow
  BOOL mScheduledShadowInvalidation;

  // DPI cache. Getting the physical screen size (CGDisplayScreenSize)
  // is ridiculously slow, so we cache it in the toplevel window for all
  // descendants to use.
  float mDPI;

  NSTrackingArea* mTrackingArea;
}

- (void)importState:(NSDictionary*)aState;
- (NSMutableDictionary*)exportState;
- (void)setDrawsContentsIntoWindowFrame:(BOOL)aState;
- (BOOL)drawsContentsIntoWindowFrame;
- (void)setTitlebarColor:(NSColor*)aColor forActiveWindow:(BOOL)aActive;
- (NSColor*)titlebarColorForActiveWindow:(BOOL)aActive;

- (void)deferredInvalidateShadow;
- (void)invalidateShadow;
- (float)getDPI;

- (void)mouseEntered:(NSEvent*)aEvent;
- (void)mouseExited:(NSEvent*)aEvent;
- (void)mouseMoved:(NSEvent*)aEvent;
- (void)updateTrackingArea;
- (NSView*)trackingAreaView;

@end

@interface NSWindow (Undocumented)

// If a window has been explicitly removed from the "window cache" (to
// deactivate it), it's sometimes necessary to "reset" it to reactivate it
// (and put it back in the "window cache").  One way to do this, which Apple
// often uses, is to set the "window number" to '-1' and then back to its
// original value.
- (void)_setWindowNumber:(NSInteger)aNumber;

// If we set the window's stylemask to be textured, the corners on the bottom of
// the window are rounded by default. We use this private method to make
// the corners square again, a la Safari.
- (void)setBottomCornerRounded:(BOOL)rounded;

@end

@interface PopupWindow : BaseWindow
{
@private
  BOOL mIsContextMenu;
}

- (id)initWithContentRect:(NSRect)contentRect styleMask:(NSUInteger)styleMask
      backing:(NSBackingStoreType)bufferingType defer:(BOOL)deferCreation;
- (BOOL)isContextMenu;
- (void)setIsContextMenu:(BOOL)flag;
- (BOOL)canBecomeMainWindow;

@end

@interface BorderlessWindow : BaseWindow
{
}

- (BOOL)canBecomeKeyWindow;
- (BOOL)canBecomeMainWindow;

@end

#if defined( MAC_OS_X_VERSION_10_6 ) && ( MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_6 )
@interface WindowDelegate : NSObject <NSWindowDelegate>
#else
@interface WindowDelegate : NSObject
#endif
{
  nsCocoaWindow* mGeckoWindow; // [WEAK] (we are owned by the window)
  // Used to avoid duplication when we send NS_ACTIVATE and
  // NS_DEACTIVATE to Gecko for toplevel widgets.  Starts out
  // false.
  bool mToplevelActiveState;
  BOOL mHasEverBeenZoomed;
}
+ (void)paintMenubarForWindow:(NSWindow*)aWindow;
- (id)initWithGeckoWindow:(nsCocoaWindow*)geckoWind;
- (void)windowDidResize:(NSNotification*)aNotification;
- (void)sendFocusEvent:(PRUint32)eventType;
- (nsCocoaWindow*)geckoWidget;
- (bool)toplevelActiveState;
- (void)sendToplevelActivateEvents;
- (void)sendToplevelDeactivateEvents;
@end

@class ToolbarWindow;

// NSColor subclass that allows us to draw separate colors both in the titlebar 
// and for background of the window.
@interface TitlebarAndBackgroundColor : NSColor
{
  ToolbarWindow *mWindow; // [WEAK] (we are owned by the window)
}

- (id)initWithWindow:(ToolbarWindow*)aWindow;

@end

// NSWindow subclass for handling windows with toolbars.
@interface ToolbarWindow : BaseWindow
{
  TitlebarAndBackgroundColor *mColor;
  float mUnifiedToolbarHeight;
  NSColor *mBackgroundColor;
}
// Pass nil here to get the default appearance.
- (void)setTitlebarColor:(NSColor*)aColor forActiveWindow:(BOOL)aActive;
- (void)setUnifiedToolbarHeight:(float)aHeight;
- (float)unifiedToolbarHeight;
- (float)titlebarHeight;
- (NSRect)titlebarRect;
- (void)setTitlebarNeedsDisplayInRect:(NSRect)aRect sync:(BOOL)aSync;
- (void)setTitlebarNeedsDisplayInRect:(NSRect)aRect;
- (void)setDrawsContentsIntoWindowFrame:(BOOL)aState;
@end

class nsCocoaWindow : public nsBaseWidget, public nsPIWidgetCocoa
{
private:
  
  typedef nsBaseWidget Inherited;

public:

    nsCocoaWindow();
    virtual ~nsCocoaWindow();

    NS_DECL_ISUPPORTS_INHERITED
    NS_DECL_NSPIWIDGETCOCOA
      
    NS_IMETHOD              Create(nsIWidget* aParent,
                                   nsNativeWidget aNativeParent,
                                   const nsIntRect &aRect,
                                   EVENT_CALLBACK aHandleEventFunction,
                                   nsDeviceContext *aContext,
                                   nsWidgetInitData *aInitData = nsnull);

    NS_IMETHOD              Destroy();

    NS_IMETHOD              Show(bool aState);
    virtual nsIWidget*      GetSheetWindowParent(void);
    NS_IMETHOD              Enable(bool aState);
    NS_IMETHOD              IsEnabled(bool *aState);
    NS_IMETHOD              SetModal(bool aState);
    NS_IMETHOD              IsVisible(bool & aState);
    NS_IMETHOD              SetFocus(bool aState=false);
    virtual nsIntPoint WidgetToScreenOffset();
    virtual nsIntPoint GetClientOffset();
    virtual nsIntSize ClientToWindowSize(const nsIntSize& aClientSize);

    virtual void* GetNativeData(PRUint32 aDataType) ;

    NS_IMETHOD              ConstrainPosition(bool aAllowSlop,
                                              PRInt32 *aX, PRInt32 *aY);
    NS_IMETHOD              Move(PRInt32 aX, PRInt32 aY);
    NS_IMETHOD              PlaceBehind(nsTopLevelWidgetZPlacement aPlacement,
                                        nsIWidget *aWidget, bool aActivate);
    NS_IMETHOD              SetSizeMode(PRInt32 aMode);
    NS_IMETHOD              HideWindowChrome(bool aShouldHide);
    NS_IMETHOD              MakeFullScreen(bool aFullScreen);
    NS_IMETHOD              Resize(PRInt32 aWidth,PRInt32 aHeight, bool aRepaint);
    NS_IMETHOD              Resize(PRInt32 aX, PRInt32 aY, PRInt32 aWidth, PRInt32 aHeight, bool aRepaint);
    NS_IMETHOD              GetClientBounds(nsIntRect &aRect);
    NS_IMETHOD              GetScreenBounds(nsIntRect &aRect);
    void                    ReportMoveEvent();
    void                    ReportSizeEvent();
    NS_IMETHOD              SetCursor(nsCursor aCursor);
    NS_IMETHOD              SetCursor(imgIContainer* aCursor, PRUint32 aHotspotX, PRUint32 aHotspotY);

    NS_IMETHOD              SetTitle(const nsAString& aTitle);

    NS_IMETHOD Invalidate(const nsIntRect &aRect, bool aIsSynchronous);
    NS_IMETHOD Update();
    virtual nsresult ConfigureChildren(const nsTArray<Configuration>& aConfigurations);
    virtual LayerManager* GetLayerManager(PLayersChild* aShadowManager = nsnull,
                                          LayersBackend aBackendHint = LayerManager::LAYERS_NONE,
                                          LayerManagerPersistence aPersistence = LAYER_MANAGER_CURRENT,
                                          bool* aAllowRetaining = nsnull);
    NS_IMETHOD DispatchEvent(nsGUIEvent* event, nsEventStatus & aStatus) ;
    NS_IMETHOD CaptureRollupEvents(nsIRollupListener * aListener, nsIMenuRollup * aMenuRollup,
                                   bool aDoCapture, bool aConsumeRollupEvent);
    NS_IMETHOD GetAttention(PRInt32 aCycleCount);
    virtual bool HasPendingInputEvent();
    virtual nsTransparencyMode GetTransparencyMode();
    virtual void SetTransparencyMode(nsTransparencyMode aMode);
    NS_IMETHOD SetWindowShadowStyle(PRInt32 aStyle);
    virtual void SetShowsToolbarButton(bool aShow);
    NS_IMETHOD SetWindowTitlebarColor(nscolor aColor, bool aActive);
    virtual void SetDrawsInTitlebar(bool aState);
    virtual nsresult SynthesizeNativeMouseEvent(nsIntPoint aPoint,
                                                PRUint32 aNativeMessage,
                                                PRUint32 aModifierFlags);

    void DispatchSizeModeEvent();

    virtual gfxASurface* GetThebesSurface();

    // be notified that a some form of drag event needs to go into Gecko
    virtual bool DragEvent(unsigned int aMessage, Point aMouseGlobal, UInt16 aKeyModifiers);

    bool HasModalDescendents() { return mNumModalDescendents > 0; }
    NSWindow *GetCocoaWindow() { return mWindow; }

    void SetMenuBar(nsMenuBarX* aMenuBar);
    nsMenuBarX *GetMenuBar();

    // nsIKBStateControl interface
    NS_IMETHOD ResetInputState();
    
    NS_IMETHOD BeginSecureKeyboardInput();
    NS_IMETHOD EndSecureKeyboardInput();

    void SetPopupWindowLevel();

    bool IsChildInFailingLeftClickThrough(NSView *aChild);
    bool ShouldFocusPlugin();

    NS_IMETHOD         ReparentNativeWidget(nsIWidget* aNewParent);
protected:

  nsresult             CreateNativeWindow(const NSRect &aRect,
                                          nsBorderStyle aBorderStyle,
                                          bool aRectIsFrameRect);
  nsresult             CreatePopupContentView(const nsIntRect &aRect,
                                              EVENT_CALLBACK aHandleEventFunction,
                                              nsDeviceContext *aContext);
  void                 DestroyNativeWindow();
  void                 AdjustWindowShadow();
  void                 SetUpWindowFilter();
  void                 CleanUpWindowFilter();
  void                 UpdateBounds();

  virtual already_AddRefed<nsIWidget>
  AllocateChildPopupWidget()
  {
    static NS_DEFINE_IID(kCPopUpCID, NS_POPUP_CID);
    nsCOMPtr<nsIWidget> widget = do_CreateInstance(kCPopUpCID);
    return widget.forget();
  }

  nsIWidget*           mParent;         // if we're a popup, this is our parent [WEAK]
  BaseWindow*          mWindow;         // our cocoa window [STRONG]
  WindowDelegate*      mDelegate;       // our delegate for processing window msgs [STRONG]
  nsRefPtr<nsMenuBarX> mMenuBar;
  NSWindow*            mSheetWindowParent; // if this is a sheet, this is the NSWindow it's attached to
  nsChildView*         mPopupContentView; // if this is a popup, this is its content widget
  PRInt32              mShadowStyle;
  NSUInteger           mWindowFilter;

  bool                 mWindowMadeHere; // true if we created the window, false for embedding
  bool                 mSheetNeedsShow; // if this is a sheet, are we waiting to be shown?
                                        // this is used for sibling sheet contention only
  bool                 mFullScreen;
  bool                 mModal;

  PRInt32              mNumModalDescendents;
};

#endif // nsCocoaWindow_h_
