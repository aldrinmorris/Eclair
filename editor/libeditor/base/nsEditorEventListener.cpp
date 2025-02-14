/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=4 sw=2 et tw=78: */
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
 *   Pierre Phaneuf <pp@ludusdesign.com>
 *   Masayuki Nakano <masayuki@d-toybox.com>
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
#include "nsEditorEventListener.h"
#include "nsEditor.h"

#include "nsIDOMDOMStringList.h"
#include "nsIDOMEvent.h"
#include "nsIDOMNSEvent.h"
#include "nsIDOMDocument.h"
#include "nsIDOMEventTarget.h"
#include "nsIDocument.h"
#include "nsIPresShell.h"
#include "nsISelection.h"
#include "nsISelectionController.h"
#include "nsIDOMKeyEvent.h"
#include "nsIDOMMouseEvent.h"
#include "nsIPrivateTextEvent.h"
#include "nsIEditorMailSupport.h"
#include "nsFocusManager.h"
#include "nsEventListenerManager.h"
#include "mozilla/Preferences.h"

// Drag & Drop, Clipboard
#include "nsIServiceManager.h"
#include "nsIClipboard.h"
#include "nsIDragService.h"
#include "nsIDragSession.h"
#include "nsIContent.h"
#include "nsISupportsPrimitives.h"
#include "nsIDOMNSRange.h"
#include "nsEditorUtils.h"
#include "nsISelectionPrivate.h"
#include "nsIDOMDragEvent.h"
#include "nsIFocusManager.h"
#include "nsIDOMWindow.h"
#include "nsContentUtils.h"
#include "nsIBidiKeyboard.h"

using namespace mozilla;

class nsAutoEditorKeypressOperation {
public:
  nsAutoEditorKeypressOperation(nsEditor *aEditor, nsIDOMNSEvent *aEvent)
    : mEditor(aEditor) {
    mEditor->BeginKeypressHandling(aEvent);
  }
  ~nsAutoEditorKeypressOperation() {
    mEditor->EndKeypressHandling();
  }

private:
  nsEditor *mEditor;
};

nsEditorEventListener::nsEditorEventListener() :
  mEditor(nsnull), mCommitText(false),
  mInTransaction(false)
#ifdef HANDLE_NATIVE_TEXT_DIRECTION_SWITCH
  , mHaveBidiKeyboards(false)
  , mShouldSwitchTextDirection(false)
  , mSwitchToRTL(false)
#endif
{
}

nsEditorEventListener::~nsEditorEventListener() 
{
  if (mEditor) {
    NS_WARNING("We're not uninstalled");
    Disconnect();
  }
}

nsresult
nsEditorEventListener::Connect(nsEditor* aEditor)
{
  NS_ENSURE_ARG(aEditor);

#ifdef HANDLE_NATIVE_TEXT_DIRECTION_SWITCH
  nsIBidiKeyboard* bidiKeyboard = nsContentUtils::GetBidiKeyboard();
  if (bidiKeyboard) {
    bool haveBidiKeyboards = false;
    bidiKeyboard->GetHaveBidiKeyboards(&haveBidiKeyboards);
    mHaveBidiKeyboards = haveBidiKeyboards;
  }
#endif

  mEditor = aEditor;

  nsresult rv = InstallToEditor();
  if (NS_FAILED(rv)) {
    Disconnect();
  }
  return rv;
}

nsresult
nsEditorEventListener::InstallToEditor()
{
  NS_PRECONDITION(mEditor, "The caller must set mEditor");

  nsCOMPtr<nsIDOMEventTarget> piTarget = mEditor->GetDOMEventTarget();
  NS_ENSURE_TRUE(piTarget, NS_ERROR_FAILURE);

  // register the event listeners with the listener manager
  nsEventListenerManager* elmP = piTarget->GetListenerManager(true);
  NS_ENSURE_STATE(elmP);

#ifdef HANDLE_NATIVE_TEXT_DIRECTION_SWITCH
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("keydown"),
                               NS_EVENT_FLAG_BUBBLE |
                               NS_EVENT_FLAG_SYSTEM_EVENT);
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("keyup"),
                               NS_EVENT_FLAG_BUBBLE |
                               NS_EVENT_FLAG_SYSTEM_EVENT);
#endif
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("keypress"),
                               NS_EVENT_FLAG_BUBBLE |
                               NS_PRIV_EVENT_UNTRUSTED_PERMITTED |
                               NS_EVENT_FLAG_SYSTEM_EVENT);
  // See bug 455215, we cannot use the standard dragstart event yet
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("draggesture"),
                               NS_EVENT_FLAG_BUBBLE |
                               NS_EVENT_FLAG_SYSTEM_EVENT);
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("dragenter"),
                               NS_EVENT_FLAG_BUBBLE |
                               NS_EVENT_FLAG_SYSTEM_EVENT);
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("dragover"),
                               NS_EVENT_FLAG_BUBBLE |
                               NS_EVENT_FLAG_SYSTEM_EVENT);
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("dragexit"),
                               NS_EVENT_FLAG_BUBBLE |
                               NS_EVENT_FLAG_SYSTEM_EVENT);
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("drop"),
                               NS_EVENT_FLAG_BUBBLE |
                               NS_EVENT_FLAG_SYSTEM_EVENT);
  // XXX We should add the mouse event listeners as system event group.
  //     E.g., web applications cannot prevent middle mouse paste by
  //     preventDefault() of click event at bubble phase.
  //     However, if we do so, all click handlers in any frames and frontend
  //     code need to check if it's editable.  It makes easier create new bugs.
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("mousedown"),
                               NS_EVENT_FLAG_CAPTURE);
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("mouseup"),
                               NS_EVENT_FLAG_CAPTURE);
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("click"),
                               NS_EVENT_FLAG_CAPTURE);
// Focus event doesn't bubble so adding the listener to capturing phase.
// Make sure this works after bug 235441 gets fixed.
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("blur"),
                               NS_EVENT_FLAG_CAPTURE);
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("focus"),
                               NS_EVENT_FLAG_CAPTURE);
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("text"),
                               NS_EVENT_FLAG_BUBBLE);
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("compositionstart"),
                               NS_EVENT_FLAG_BUBBLE);
  elmP->AddEventListenerByType(this,
                               NS_LITERAL_STRING("compositionend"),
                               NS_EVENT_FLAG_BUBBLE);

  return NS_OK;
}

void
nsEditorEventListener::Disconnect()
{
  if (!mEditor) {
    return;
  }
  UninstallFromEditor();
  mEditor = nsnull;
}

void
nsEditorEventListener::UninstallFromEditor()
{
  nsCOMPtr<nsIDOMEventTarget> piTarget = mEditor->GetDOMEventTarget();
  if (!piTarget) {
    return;
  }

  nsEventListenerManager* elmP =
    piTarget->GetListenerManager(true);
  if (!elmP) {
    return;
  }

#ifdef HANDLE_NATIVE_TEXT_DIRECTION_SWITCH
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("keydown"),
                                  NS_EVENT_FLAG_BUBBLE |
                                  NS_EVENT_FLAG_SYSTEM_EVENT);
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("keyup"),
                                  NS_EVENT_FLAG_BUBBLE |
                                  NS_EVENT_FLAG_SYSTEM_EVENT);
#endif
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("keypress"),
                                  NS_EVENT_FLAG_BUBBLE |
                                  NS_EVENT_FLAG_SYSTEM_EVENT);
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("draggesture"),
                                  NS_EVENT_FLAG_BUBBLE |
                                  NS_EVENT_FLAG_SYSTEM_EVENT);
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("dragenter"),
                                  NS_EVENT_FLAG_BUBBLE |
                                  NS_EVENT_FLAG_SYSTEM_EVENT);
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("dragover"),
                                  NS_EVENT_FLAG_BUBBLE |
                                  NS_EVENT_FLAG_SYSTEM_EVENT);
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("dragexit"),
                                  NS_EVENT_FLAG_BUBBLE |
                                  NS_EVENT_FLAG_SYSTEM_EVENT);
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("drop"),
                                  NS_EVENT_FLAG_BUBBLE |
                                  NS_EVENT_FLAG_SYSTEM_EVENT);
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("mousedown"),
                                  NS_EVENT_FLAG_CAPTURE);
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("mouseup"),
                                  NS_EVENT_FLAG_CAPTURE);
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("click"),
                                  NS_EVENT_FLAG_CAPTURE);
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("blur"),
                                  NS_EVENT_FLAG_CAPTURE);
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("focus"),
                                  NS_EVENT_FLAG_CAPTURE);
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("text"),
                                  NS_EVENT_FLAG_BUBBLE);
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("compositionstart"),
                                  NS_EVENT_FLAG_BUBBLE);
  elmP->RemoveEventListenerByType(this,
                                  NS_LITERAL_STRING("compositionend"),
                                  NS_EVENT_FLAG_BUBBLE);
}

already_AddRefed<nsIPresShell>
nsEditorEventListener::GetPresShell()
{
  NS_PRECONDITION(mEditor,
    "The caller must check whether this is connected to an editor");
  return mEditor->GetPresShell();
}

/**
 *  nsISupports implementation
 */

NS_IMPL_ISUPPORTS1(nsEditorEventListener, nsIDOMEventListener)

/**
 *  nsIDOMEventListener implementation
 */

NS_IMETHODIMP
nsEditorEventListener::HandleEvent(nsIDOMEvent* aEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  nsCOMPtr<nsIEditor> kungFuDeathGrip = mEditor;

  nsAutoString eventType;
  aEvent->GetType(eventType);

  nsCOMPtr<nsIDOMDragEvent> dragEvent = do_QueryInterface(aEvent);
  if (dragEvent) {
    if (eventType.EqualsLiteral("draggesture"))
      return DragGesture(dragEvent);
    if (eventType.EqualsLiteral("dragenter"))
      return DragEnter(dragEvent);
    if (eventType.EqualsLiteral("dragover"))
      return DragOver(dragEvent);
    if (eventType.EqualsLiteral("dragexit"))
      return DragExit(dragEvent);
    if (eventType.EqualsLiteral("drop"))
      return Drop(dragEvent);
  }

#ifdef HANDLE_NATIVE_TEXT_DIRECTION_SWITCH
  if (eventType.EqualsLiteral("keydown"))
    return KeyDown(aEvent);
  if (eventType.EqualsLiteral("keyup"))
    return KeyUp(aEvent);
#endif
  if (eventType.EqualsLiteral("keypress"))
    return KeyPress(aEvent);
  if (eventType.EqualsLiteral("mousedown"))
    return MouseDown(aEvent);
  if (eventType.EqualsLiteral("mouseup"))
    return MouseUp(aEvent);
  if (eventType.EqualsLiteral("click"))
    return MouseClick(aEvent);
  if (eventType.EqualsLiteral("focus"))
    return Focus(aEvent);
  if (eventType.EqualsLiteral("blur"))
    return Blur(aEvent);
  if (eventType.EqualsLiteral("text"))
    return HandleText(aEvent);
  if (eventType.EqualsLiteral("compositionstart"))
    return HandleStartComposition(aEvent);
  if (eventType.EqualsLiteral("compositionend"))
    return HandleEndComposition(aEvent);

  return NS_OK;
}

#ifdef HANDLE_NATIVE_TEXT_DIRECTION_SWITCH
#include <windows.h>

namespace {

// This function is borrowed from Chromium's ImeInput::IsCtrlShiftPressed
bool IsCtrlShiftPressed(bool& isRTL)
{
  BYTE keystate[256];
  if (!::GetKeyboardState(keystate)) {
    return false;
  }

  // To check if a user is pressing only a control key and a right-shift key
  // (or a left-shift key), we use the steps below:
  // 1. Check if a user is pressing a control key and a right-shift key (or
  //    a left-shift key).
  // 2. If the condition 1 is true, we should check if there are any other
  //    keys pressed at the same time.
  //    To ignore the keys checked in 1, we set their status to 0 before
  //    checking the key status.
  const int kKeyDownMask = 0x80;
  if ((keystate[VK_CONTROL] & kKeyDownMask) == 0)
    return false;

  if (keystate[VK_RSHIFT] & kKeyDownMask) {
    keystate[VK_RSHIFT] = 0;
    isRTL = true;
  } else if (keystate[VK_LSHIFT] & kKeyDownMask) {
    keystate[VK_LSHIFT] = 0;
    isRTL = false;
  } else {
    return false;
  }

  // Scan the key status to find pressed keys. We should abandon changing the
  // text direction when there are other pressed keys.
  // This code is executed only when a user is pressing a control key and a
  // right-shift key (or a left-shift key), i.e. we should ignore the status of
  // the keys: VK_SHIFT, VK_CONTROL, VK_RCONTROL, and VK_LCONTROL.
  // So, we reset their status to 0 and ignore them.
  keystate[VK_SHIFT] = 0;
  keystate[VK_CONTROL] = 0;
  keystate[VK_RCONTROL] = 0;
  keystate[VK_LCONTROL] = 0;
  for (int i = 0; i <= VK_PACKET; ++i) {
    if (keystate[i] & kKeyDownMask)
      return false;
  }
  return true;
}

}

// This logic is mostly borrowed from Chromium's
// RenderWidgetHostViewWin::OnKeyEvent.

NS_IMETHODIMP
nsEditorEventListener::KeyUp(nsIDOMEvent* aKeyEvent)
{
  if (mHaveBidiKeyboards) {
    nsCOMPtr<nsIDOMKeyEvent> keyEvent = do_QueryInterface(aKeyEvent);
    if (!keyEvent) {
      // non-key event passed to keyup.  bad things.
      return NS_OK;
    }

    PRUint32 keyCode = 0;
    keyEvent->GetKeyCode(&keyCode);
    if (keyCode == nsIDOMKeyEvent::DOM_VK_SHIFT ||
        keyCode == nsIDOMKeyEvent::DOM_VK_CONTROL) {
      if (mShouldSwitchTextDirection && mEditor->IsPlaintextEditor()) {
        mEditor->SwitchTextDirectionTo(mSwitchToRTL ?
          nsIPlaintextEditor::eEditorRightToLeft :
          nsIPlaintextEditor::eEditorLeftToRight);
        mShouldSwitchTextDirection = false;
      }
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsEditorEventListener::KeyDown(nsIDOMEvent* aKeyEvent)
{
  if (mHaveBidiKeyboards) {
    nsCOMPtr<nsIDOMKeyEvent> keyEvent = do_QueryInterface(aKeyEvent);
    if (!keyEvent) {
      // non-key event passed to keydown.  bad things.
      return NS_OK;
    }

    PRUint32 keyCode = 0;
    keyEvent->GetKeyCode(&keyCode);
    if (keyCode == nsIDOMKeyEvent::DOM_VK_SHIFT) {
      bool switchToRTL;
      if (IsCtrlShiftPressed(switchToRTL)) {
        mShouldSwitchTextDirection = true;
        mSwitchToRTL = switchToRTL;
      }
    } else if (keyCode != nsIDOMKeyEvent::DOM_VK_CONTROL) {
      // In case the user presses any other key besides Ctrl and Shift
      mShouldSwitchTextDirection = false;
    }
  }

  return NS_OK;
}
#endif

NS_IMETHODIMP
nsEditorEventListener::KeyPress(nsIDOMEvent* aKeyEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);

  if (!mEditor->IsAcceptableInputEvent(aKeyEvent)) {
    return NS_OK;
  }

  // Transfer the event's trusted-ness to our editor
  nsCOMPtr<nsIDOMNSEvent> NSEvent = do_QueryInterface(aKeyEvent);
  nsAutoEditorKeypressOperation operation(mEditor, NSEvent);

  // DOM event handling happens in two passes, the client pass and the system
  // pass.  We do all of our processing in the system pass, to allow client
  // handlers the opportunity to cancel events and prevent typing in the editor.
  // If the client pass cancelled the event, defaultPrevented will be true
  // below.

  if (NSEvent) {
    bool defaultPrevented;
    NSEvent->GetPreventDefault(&defaultPrevented);
    if (defaultPrevented) {
      return NS_OK;
    }
  }

  nsCOMPtr<nsIDOMKeyEvent> keyEvent = do_QueryInterface(aKeyEvent);
  if (!keyEvent) {
    //non-key event passed to keypress.  bad things.
    return NS_OK;
  }

  return mEditor->HandleKeyPressEvent(keyEvent);
}

NS_IMETHODIMP
nsEditorEventListener::MouseClick(nsIDOMEvent* aMouseEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);

  nsCOMPtr<nsIDOMMouseEvent> mouseEvent = do_QueryInterface(aMouseEvent);
  NS_ENSURE_TRUE(mouseEvent, NS_OK);

  // nothing to do if editor isn't editable or clicked on out of the editor.
  if (mEditor->IsReadonly() || mEditor->IsDisabled() ||
      !mEditor->IsAcceptableInputEvent(aMouseEvent)) {
    return NS_OK;
  }

  nsCOMPtr<nsIDOMNSEvent> nsevent = do_QueryInterface(aMouseEvent);
  NS_ASSERTION(nsevent, "nsevent must not be NULL here");
  bool preventDefault;
  nsresult rv = nsevent->GetPreventDefault(&preventDefault);
  if (NS_FAILED(rv) || preventDefault) {
    // We're done if 'preventdefault' is true (see for example bug 70698).
    return rv;
  }

  // If we got a mouse down inside the editing area, we should force the 
  // IME to commit before we change the cursor position
  mEditor->ForceCompositionEnd();

  PRUint16 button = (PRUint16)-1;
  mouseEvent->GetButton(&button);
  // middle-mouse click (paste);
  if (button == 1)
  {
    if (Preferences::GetBool("middlemouse.paste", false))
    {
      // Set the selection to the point under the mouse cursor:
      nsCOMPtr<nsIDOMNode> parent;
      if (NS_FAILED(mouseEvent->GetRangeParent(getter_AddRefs(parent))))
        return NS_ERROR_NULL_POINTER;
      PRInt32 offset = 0;
      if (NS_FAILED(mouseEvent->GetRangeOffset(&offset)))
        return NS_ERROR_NULL_POINTER;

      nsCOMPtr<nsISelection> selection;
      if (NS_SUCCEEDED(mEditor->GetSelection(getter_AddRefs(selection))))
        (void)selection->Collapse(parent, offset);

      // If the ctrl key is pressed, we'll do paste as quotation.
      // Would've used the alt key, but the kde wmgr treats alt-middle specially. 
      bool ctrlKey = false;
      mouseEvent->GetCtrlKey(&ctrlKey);

      nsCOMPtr<nsIEditorMailSupport> mailEditor;
      if (ctrlKey)
        mailEditor = do_QueryObject(mEditor);

      PRInt32 clipboard;

#if defined(XP_OS2) || defined(XP_WIN32)
      clipboard = nsIClipboard::kGlobalClipboard;
#else
      clipboard = nsIClipboard::kSelectionClipboard;
#endif

      if (mailEditor)
        mailEditor->PasteAsQuotation(clipboard);
      else
        mEditor->Paste(clipboard);

      // Prevent the event from propagating up to be possibly handled
      // again by the containing window:
      mouseEvent->StopPropagation();
      mouseEvent->PreventDefault();

      // We processed the event, whether drop/paste succeeded or not
      return NS_OK;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsEditorEventListener::MouseDown(nsIDOMEvent* aMouseEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  mEditor->ForceCompositionEnd();
  return NS_OK;
}

NS_IMETHODIMP
nsEditorEventListener::HandleText(nsIDOMEvent* aTextEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);

  if (!mEditor->IsAcceptableInputEvent(aTextEvent)) {
    return NS_OK;
  }

  nsCOMPtr<nsIPrivateTextEvent> textEvent = do_QueryInterface(aTextEvent);
  if (!textEvent) {
     //non-ui event passed in.  bad things.
     return NS_OK;
  }

  nsAutoString                      composedText;
  nsCOMPtr<nsIPrivateTextRangeList> textRangeList;

  textEvent->GetText(composedText);
  textRangeList = textEvent->GetInputRange();

  // if we are readonly or disabled, then do nothing.
  if (mEditor->IsReadonly() || mEditor->IsDisabled()) {
    return NS_OK;
  }

  return mEditor->UpdateIMEComposition(composedText, textRangeList);
}

/**
 * Drag event implementation
 */

nsresult
nsEditorEventListener::DragGesture(nsIDOMDragEvent* aDragEvent)
{
  // ...figure out if a drag should be started...
  bool canDrag;
  nsresult rv = mEditor->CanDrag(aDragEvent, &canDrag);
  if ( NS_SUCCEEDED(rv) && canDrag )
    rv = mEditor->DoDrag(aDragEvent);

  return rv;
}

nsresult
nsEditorEventListener::DragEnter(nsIDOMDragEvent* aDragEvent)
{
  nsCOMPtr<nsIPresShell> presShell = GetPresShell();
  NS_ENSURE_TRUE(presShell, NS_OK);

  if (!mCaret) {
    mCaret = new nsCaret();
    mCaret->Init(presShell);
    mCaret->SetCaretReadOnly(true);
  }

  presShell->SetCaret(mCaret);

  return DragOver(aDragEvent);
}

nsresult
nsEditorEventListener::DragOver(nsIDOMDragEvent* aDragEvent)
{
  nsCOMPtr<nsIDOMNode> parent;
  nsCOMPtr<nsIDOMNSEvent> domNSEvent = do_QueryInterface(aDragEvent);
  if (domNSEvent) {
    bool defaultPrevented;
    domNSEvent->GetPreventDefault(&defaultPrevented);
    if (defaultPrevented)
      return NS_OK;
  }

  aDragEvent->GetRangeParent(getter_AddRefs(parent));
  nsCOMPtr<nsIContent> dropParent = do_QueryInterface(parent);
  NS_ENSURE_TRUE(dropParent, NS_ERROR_FAILURE);

  if (!dropParent->IsEditable()) {
    return NS_OK;
  }

  if (CanDrop(aDragEvent)) {
    aDragEvent->PreventDefault(); // consumed

    if (mCaret) {
      PRInt32 offset = 0;
      nsresult rv = aDragEvent->GetRangeOffset(&offset);
      NS_ENSURE_SUCCESS(rv, rv);

      // to avoid flicker, we could track the node and offset to see if we moved
      if (mCaret)
        mCaret->EraseCaret();
      
      //mCaret->SetCaretVisible(true);   // make sure it's visible
      mCaret->DrawAtPosition(parent, offset);
    }
  }
  else
  {
    if (mCaret)
    {
      mCaret->EraseCaret();
    }
  }

  return NS_OK;
}

void
nsEditorEventListener::CleanupDragDropCaret()
{
  if (mCaret)
  {
    mCaret->EraseCaret();
    mCaret->SetCaretVisible(false);    // hide it, so that it turns off its timer

    nsCOMPtr<nsIPresShell> presShell = GetPresShell();
    if (presShell)
    {
      presShell->RestoreCaret();
    }

    mCaret->Terminate();
    mCaret = nsnull;
  }
}

nsresult
nsEditorEventListener::DragExit(nsIDOMDragEvent* aDragEvent)
{
  CleanupDragDropCaret();

  return NS_OK;
}

nsresult
nsEditorEventListener::Drop(nsIDOMDragEvent* aMouseEvent)
{
  CleanupDragDropCaret();

  nsCOMPtr<nsIDOMNSEvent> domNSEvent = do_QueryInterface(aMouseEvent);
  if (domNSEvent) {
    bool defaultPrevented;
    domNSEvent->GetPreventDefault(&defaultPrevented);
    if (defaultPrevented)
      return NS_OK;
  }

  nsCOMPtr<nsIDOMNode> parent;
  aMouseEvent->GetRangeParent(getter_AddRefs(parent));
  nsCOMPtr<nsIContent> dropParent = do_QueryInterface(parent);
  NS_ENSURE_TRUE(dropParent, NS_ERROR_FAILURE);

  if (!dropParent->IsEditable()) {
    return NS_OK;
  }

  if (!CanDrop(aMouseEvent)) {
    // was it because we're read-only?
    if (mEditor->IsReadonly() || mEditor->IsDisabled())
    {
      // it was decided to "eat" the event as this is the "least surprise"
      // since someone else handling it might be unintentional and the 
      // user could probably re-drag to be not over the disabled/readonly 
      // editfields if that is what is desired.
      return aMouseEvent->StopPropagation();
    }
    return NS_OK;
  }

  aMouseEvent->StopPropagation();
  aMouseEvent->PreventDefault();
  // Beware! This may flush notifications via synchronous
  // ScrollSelectionIntoView.
  return mEditor->InsertFromDrop(aMouseEvent);
}

bool
nsEditorEventListener::CanDrop(nsIDOMDragEvent* aEvent)
{
  // if the target doc is read-only, we can't drop
  if (mEditor->IsReadonly() || mEditor->IsDisabled()) {
    return false;
  }

  nsCOMPtr<nsIDOMDataTransfer> dataTransfer;
  aEvent->GetDataTransfer(getter_AddRefs(dataTransfer));
  NS_ENSURE_TRUE(dataTransfer, false);

  nsCOMPtr<nsIDOMDOMStringList> types;
  dataTransfer->GetTypes(getter_AddRefs(types));
  NS_ENSURE_TRUE(types, false);

  // Plaintext editors only support dropping text. Otherwise, HTML and files
  // can be dropped as well.
  bool typeSupported;
  types->Contains(NS_LITERAL_STRING(kTextMime), &typeSupported);
  if (!typeSupported) {
    types->Contains(NS_LITERAL_STRING(kMozTextInternal), &typeSupported);
    if (!typeSupported && !mEditor->IsPlaintextEditor()) {
      types->Contains(NS_LITERAL_STRING(kHTMLMime), &typeSupported);
      if (!typeSupported) {
        types->Contains(NS_LITERAL_STRING(kFileMime), &typeSupported);
      }
    }
  }

  NS_ENSURE_TRUE(typeSupported, false);

  nsCOMPtr<nsIDOMNSDataTransfer> dataTransferNS(do_QueryInterface(dataTransfer));
  NS_ENSURE_TRUE(dataTransferNS, false);

  // If there is no source node, this is probably an external drag and the
  // drop is allowed. The later checks rely on checking if the drag target
  // is the same as the drag source.
  nsCOMPtr<nsIDOMNode> sourceNode;
  dataTransferNS->GetMozSourceNode(getter_AddRefs(sourceNode));
  if (!sourceNode)
    return true;

  // There is a source node, so compare the source documents and this document.
  // Disallow drops on the same document.

  nsCOMPtr<nsIDOMDocument> domdoc;
  nsresult rv = mEditor->GetDocument(getter_AddRefs(domdoc));
  NS_ENSURE_SUCCESS(rv, false);

  nsCOMPtr<nsIDOMDocument> sourceDoc;
  rv = sourceNode->GetOwnerDocument(getter_AddRefs(sourceDoc));
  NS_ENSURE_SUCCESS(rv, false);
  if (domdoc == sourceDoc)      // source and dest are the same document; disallow drops within the selection
  {
    nsCOMPtr<nsISelection> selection;
    rv = mEditor->GetSelection(getter_AddRefs(selection));
    if (NS_FAILED(rv) || !selection)
      return false;
    
    bool isCollapsed;
    rv = selection->GetIsCollapsed(&isCollapsed);
    NS_ENSURE_SUCCESS(rv, false);
  
    // Don't bother if collapsed - can always drop
    if (!isCollapsed)
    {
      nsCOMPtr<nsIDOMNode> parent;
      rv = aEvent->GetRangeParent(getter_AddRefs(parent));
      if (NS_FAILED(rv) || !parent) return false;

      PRInt32 offset = 0;
      rv = aEvent->GetRangeOffset(&offset);
      NS_ENSURE_SUCCESS(rv, false);

      PRInt32 rangeCount;
      rv = selection->GetRangeCount(&rangeCount);
      NS_ENSURE_SUCCESS(rv, false);

      for (PRInt32 i = 0; i < rangeCount; i++)
      {
        nsCOMPtr<nsIDOMRange> range;
        rv = selection->GetRangeAt(i, getter_AddRefs(range));
        nsCOMPtr<nsIDOMNSRange> nsrange(do_QueryInterface(range));
        if (NS_FAILED(rv) || !nsrange) 
          continue; //don't bail yet, iterate through them all

        bool inRange = true;
        (void)nsrange->IsPointInRange(parent, offset, &inRange);
        if (inRange)
          return false;  //okay, now you can bail, we are over the orginal selection
      }
    }
  }
  
  return true;
}

NS_IMETHODIMP
nsEditorEventListener::HandleStartComposition(nsIDOMEvent* aCompositionEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  if (!mEditor->IsAcceptableInputEvent(aCompositionEvent)) {
    return NS_OK;
  }
  return mEditor->BeginIMEComposition();
}

NS_IMETHODIMP
nsEditorEventListener::HandleEndComposition(nsIDOMEvent* aCompositionEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  if (!mEditor->IsAcceptableInputEvent(aCompositionEvent)) {
    return NS_OK;
  }

  // Transfer the event's trusted-ness to our editor
  nsCOMPtr<nsIDOMNSEvent> NSEvent = do_QueryInterface(aCompositionEvent);
  nsAutoEditorKeypressOperation operation(mEditor, NSEvent);

  return mEditor->EndIMEComposition();
}

NS_IMETHODIMP
nsEditorEventListener::Focus(nsIDOMEvent* aEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  NS_ENSURE_ARG(aEvent);

  // Don't turn on selection and caret when the editor is disabled.
  if (mEditor->IsDisabled()) {
    return NS_OK;
  }
  
  // If the spell check skip flag is still enabled from creation time,
  // disable it because focused editors are allowed to spell check.
  PRUint32 currentFlags = 0;
  mEditor->GetFlags(&currentFlags);
  if(currentFlags & nsIPlaintextEditor::eEditorSkipSpellCheck)
  {
    currentFlags ^= nsIPlaintextEditor::eEditorSkipSpellCheck;
    mEditor->SetFlags(currentFlags);
  }
  

  nsCOMPtr<nsIDOMEventTarget> target;
  aEvent->GetTarget(getter_AddRefs(target));
  nsCOMPtr<nsINode> node = do_QueryInterface(target);
  NS_ENSURE_TRUE(node, NS_ERROR_UNEXPECTED);

  // If the traget is a document node but it's not editable, we should ignore
  // it because actual focused element's event is going to come.
  if (node->IsNodeOfType(nsINode::eDOCUMENT) &&
      !node->HasFlag(NODE_IS_EDITABLE)) {
    return NS_OK;
  }

  if (node->IsNodeOfType(nsINode::eCONTENT)) {
    // XXX If the focus event target is a form control in contenteditable
    // element, perhaps, the parent HTML editor should do nothing by this
    // handler.  However, FindSelectionRoot() returns the root element of the
    // contenteditable editor.  So, the editableRoot value is invalid for
    // the plain text editor, and it will be set to the wrong limiter of
    // the selection.  However, fortunately, actual bugs are not found yet.
    nsCOMPtr<nsIContent> editableRoot = mEditor->FindSelectionRoot(node);

    // make sure that the element is really focused in case an earlier
    // listener in the chain changed the focus.
    if (editableRoot) {
      nsIFocusManager* fm = nsFocusManager::GetFocusManager();
      NS_ENSURE_TRUE(fm, NS_OK);

      nsCOMPtr<nsIDOMElement> element;
      fm->GetFocusedElement(getter_AddRefs(element));
      if (!SameCOMIdentity(element, target))
        return NS_OK;
    }
  }

  mEditor->OnFocus(target);
  return NS_OK;
}

NS_IMETHODIMP
nsEditorEventListener::Blur(nsIDOMEvent* aEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  NS_ENSURE_ARG(aEvent);

  // check if something else is focused. If another element is focused, then
  // we should not change the selection.
  nsIFocusManager* fm = nsFocusManager::GetFocusManager();
  NS_ENSURE_TRUE(fm, NS_OK);

  nsCOMPtr<nsIDOMElement> element;
  fm->GetFocusedElement(getter_AddRefs(element));
  if (element)
    return NS_OK;

  // turn off selection and caret
  nsCOMPtr<nsISelectionController>selCon;
  mEditor->GetSelectionController(getter_AddRefs(selCon));
  if (selCon)
  {
    nsCOMPtr<nsISelection> selection;
    selCon->GetSelection(nsISelectionController::SELECTION_NORMAL,
                         getter_AddRefs(selection));

    nsCOMPtr<nsISelectionPrivate> selectionPrivate =
      do_QueryInterface(selection);
    if (selectionPrivate) {
      selectionPrivate->SetAncestorLimiter(nsnull);
    }

    nsCOMPtr<nsIPresShell> presShell = GetPresShell();
    if (presShell) {
      nsRefPtr<nsCaret> caret = presShell->GetCaret();
      if (caret) {
        caret->SetIgnoreUserModify(true);
      }
    }

    selCon->SetCaretEnabled(false);

    if(mEditor->IsFormWidget() || mEditor->IsPasswordEditor() ||
       mEditor->IsReadonly() || mEditor->IsDisabled() ||
       mEditor->IsInputFiltered())
    {
      selCon->SetDisplaySelection(nsISelectionController::SELECTION_HIDDEN);//hide but do NOT turn off
    }
    else
    {
      selCon->SetDisplaySelection(nsISelectionController::SELECTION_DISABLED);
    }

    selCon->RepaintSelection(nsISelectionController::SELECTION_NORMAL);
  }

  return NS_OK;
}

