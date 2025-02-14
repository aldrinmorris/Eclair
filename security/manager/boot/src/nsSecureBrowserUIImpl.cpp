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
 * Portions created by the Initial Developer are Copyright (C) 1998-2001
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Hubbie Shaw
 *   Doug Turner <dougt@netscape.com>
 *   Stuart Parmenter <pavlov@netscape.com>
 *   Brian Ryner <bryner@brianryner.com>
 *   Terry Hayes <thayes@netscape.com>
 *   Kai Engert <kaie@netscape.com>
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

#ifdef MOZ_LOGGING
#define FORCE_PR_LOG
#endif

#include "nspr.h"
#include "prlog.h"
#include "prmem.h"

#include "nsISecureBrowserUI.h"
#include "nsSecureBrowserUIImpl.h"
#include "nsCOMPtr.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIServiceManager.h"
#include "nsIObserverService.h"
#include "nsCURILoader.h"
#include "nsIDocShell.h"
#include "nsIDocument.h"
#include "nsIPrincipal.h"
#include "nsIDOMElement.h"
#include "nsPIDOMWindow.h"
#include "nsIContent.h"
#include "nsIWebProgress.h"
#include "nsIWebProgressListener.h"
#include "nsIChannel.h"
#include "nsIHttpChannel.h"
#include "nsIFileChannel.h"
#include "nsIWyciwygChannel.h"
#include "nsIFTPChannel.h"
#include "nsITransportSecurityInfo.h"
#include "nsISSLStatus.h"
#include "nsIURI.h"
#include "nsISecurityEventSink.h"
#include "nsIPrompt.h"
#include "nsIFormSubmitObserver.h"
#include "nsISecurityWarningDialogs.h"
#include "nsISecurityInfoProvider.h"
#include "imgIRequest.h"
#include "nsThreadUtils.h"
#include "nsNetUtil.h"
#include "nsNetCID.h"
#include "nsCRT.h"

using namespace mozilla;

#define SECURITY_STRING_BUNDLE_URL "chrome://pipnss/locale/security.properties"

#define IS_SECURE(state) ((state & 0xFFFF) == STATE_IS_SECURE)

#if defined(PR_LOGGING)
//
// Log module for nsSecureBrowserUI logging...
//
// To enable logging (see prlog.h for full details):
//
//    set NSPR_LOG_MODULES=nsSecureBrowserUI:5
//    set NSPR_LOG_FILE=nspr.log
//
// this enables PR_LOG_DEBUG level information and places all output in
// the file nspr.log
//
PRLogModuleInfo* gSecureDocLog = nsnull;
#endif /* PR_LOGGING */

struct RequestHashEntry : PLDHashEntryHdr {
    void *r;
};

PR_STATIC_CALLBACK(bool)
RequestMapMatchEntry(PLDHashTable *table, const PLDHashEntryHdr *hdr,
                         const void *key)
{
  const RequestHashEntry *entry = static_cast<const RequestHashEntry*>(hdr);
  return entry->r == key;
}

PR_STATIC_CALLBACK(bool)
RequestMapInitEntry(PLDHashTable *table, PLDHashEntryHdr *hdr,
                     const void *key)
{
  RequestHashEntry *entry = static_cast<RequestHashEntry*>(hdr);
  entry->r = (void*)key;
  return true;
}

static PLDHashTableOps gMapOps = {
  PL_DHashAllocTable,
  PL_DHashFreeTable,
  PL_DHashVoidPtrKeyStub,
  RequestMapMatchEntry,
  PL_DHashMoveEntryStub,
  PL_DHashClearEntryStub,
  PL_DHashFinalizeStub,
  RequestMapInitEntry
};

#ifdef DEBUG
class nsAutoAtomic {
  public:
    nsAutoAtomic(PRInt32 &i)
    :mI(i) {
      PR_ATOMIC_INCREMENT(&mI);
    }

    ~nsAutoAtomic() {
      PR_ATOMIC_DECREMENT(&mI);
    }

  protected:
    PRInt32 &mI;

  private:
    nsAutoAtomic(); // not accessible
};
#endif

nsSecureBrowserUIImpl::nsSecureBrowserUIImpl()
  : mReentrantMonitor("nsSecureBrowserUIImpl.mReentrantMonitor")
  , mNotifiedSecurityState(lis_no_security)
  , mNotifiedToplevelIsEV(false)
  , mNewToplevelSecurityState(STATE_IS_INSECURE)
  , mNewToplevelIsEV(false)
  , mNewToplevelSecurityStateKnown(true)
  , mIsViewSource(false)
  , mSubRequestsHighSecurity(0)
  , mSubRequestsLowSecurity(0)
  , mSubRequestsBrokenSecurity(0)
  , mSubRequestsNoSecurity(0)
  , mRestoreSubrequests(false)
  , mOnLocationChangeSeen(false)
#ifdef DEBUG
  , mOnStateLocationChangeReentranceDetection(0)
#endif
{
  mTransferringRequests.ops = nsnull;
  ResetStateTracking();
  
#if defined(PR_LOGGING)
  if (!gSecureDocLog)
    gSecureDocLog = PR_NewLogModule("nsSecureBrowserUI");
#endif /* PR_LOGGING */
}

nsSecureBrowserUIImpl::~nsSecureBrowserUIImpl()
{
  if (mTransferringRequests.ops) {
    PL_DHashTableFinish(&mTransferringRequests);
    mTransferringRequests.ops = nsnull;
  }
}

NS_IMPL_THREADSAFE_ISUPPORTS6(nsSecureBrowserUIImpl,
                              nsISecureBrowserUI,
                              nsIWebProgressListener,
                              nsIFormSubmitObserver,
                              nsIObserver,
                              nsISupportsWeakReference,
                              nsISSLStatusProvider)

NS_IMETHODIMP
nsSecureBrowserUIImpl::Init(nsIDOMWindow *aWindow)
{

#ifdef PR_LOGGING
  nsCOMPtr<nsIDOMWindow> window(do_QueryReferent(mWindow));

  PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
         ("SecureUI:%p: Init: mWindow: %p, aWindow: %p\n", this,
          window.get(), aWindow));
#endif

  if (!aWindow) {
    NS_WARNING("Null window passed to nsSecureBrowserUIImpl::Init()");
    return NS_ERROR_INVALID_ARG;
  }

  if (mWindow) {
    NS_WARNING("Trying to init an nsSecureBrowserUIImpl twice");
    return NS_ERROR_ALREADY_INITIALIZED;
  }

  nsCOMPtr<nsPIDOMWindow> pwin(do_QueryInterface(aWindow));
  if (pwin->IsInnerWindow()) {
    pwin = pwin->GetOuterWindow();
  }

  nsresult rv;
  mWindow = do_GetWeakReference(pwin, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIStringBundleService> service(do_GetService(NS_STRINGBUNDLE_CONTRACTID, &rv));
  if (NS_FAILED(rv)) return rv;
  
  // We do not need to test for mStringBundle here...
  // Anywhere we use it, we will test before using.  Some
  // embedded users of PSM may want to reuse our
  // nsSecureBrowserUIImpl implementation without the
  // bundle.
  service->CreateBundle(SECURITY_STRING_BUNDLE_URL, getter_AddRefs(mStringBundle));
  
  
  // hook up to the form post notifications:
  nsCOMPtr<nsIObserverService> svc(do_GetService("@mozilla.org/observer-service;1", &rv));
  if (NS_SUCCEEDED(rv)) {
    rv = svc->AddObserver(this, NS_FORMSUBMIT_SUBJECT, true);
  }
  
  nsCOMPtr<nsPIDOMWindow> piwindow(do_QueryInterface(aWindow));
  if (!piwindow) return NS_ERROR_FAILURE;

  nsIDocShell *docShell = piwindow->GetDocShell();

  // The Docshell will own the SecureBrowserUI object
  if (!docShell)
    return NS_ERROR_FAILURE;

  docShell->SetSecurityUI(this);

  /* GetWebProgress(mWindow) */
  // hook up to the webprogress notifications.
  nsCOMPtr<nsIWebProgress> wp(do_GetInterface(docShell));
  if (!wp) return NS_ERROR_FAILURE;
  /* end GetWebProgress */
  
  wp->AddProgressListener(static_cast<nsIWebProgressListener*>(this),
                          nsIWebProgress::NOTIFY_STATE_ALL | 
                          nsIWebProgress::NOTIFY_LOCATION  |
                          nsIWebProgress::NOTIFY_SECURITY);


  return NS_OK;
}

NS_IMETHODIMP
nsSecureBrowserUIImpl::GetState(PRUint32* aState)
{
  ReentrantMonitorAutoEnter lock(mReentrantMonitor);
  return MapInternalToExternalState(aState, mNotifiedSecurityState, mNotifiedToplevelIsEV);
}

// static
already_AddRefed<nsISupports> 
nsSecureBrowserUIImpl::ExtractSecurityInfo(nsIRequest* aRequest)
{
  nsISupports *retval = nsnull; 
  nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
  if (channel)
    channel->GetSecurityInfo(&retval);
  
  if (!retval) {
    nsCOMPtr<nsISecurityInfoProvider> provider(do_QueryInterface(aRequest));
    if (provider)
      provider->GetSecurityInfo(&retval);
  }

  return retval;
}

nsresult
nsSecureBrowserUIImpl::MapInternalToExternalState(PRUint32* aState, lockIconState lock, bool ev)
{
  NS_ENSURE_ARG(aState);

  switch (lock)
  {
    case lis_broken_security:
      *aState = STATE_IS_BROKEN;
      break;

    case lis_mixed_security:
      *aState = STATE_IS_BROKEN;
      break;

    case lis_low_security:
      *aState = STATE_IS_SECURE | STATE_SECURE_LOW;
      break;

    case lis_high_security:
      *aState = STATE_IS_SECURE | STATE_SECURE_HIGH;
      break;

    default:
    case lis_no_security:
      *aState = STATE_IS_INSECURE;
      break;
  }

  if (ev && (*aState & STATE_IS_SECURE))
    *aState |= nsIWebProgressListener::STATE_IDENTITY_EV_TOPLEVEL;
  
  return NS_OK;
}

NS_IMETHODIMP
nsSecureBrowserUIImpl::GetTooltipText(nsAString& aText)
{
  lockIconState state;
  nsXPIDLString tooltip;

  {
    ReentrantMonitorAutoEnter lock(mReentrantMonitor);
    state = mNotifiedSecurityState;
    tooltip = mInfoTooltip;
  }

  if (state == lis_mixed_security)
  {
    GetBundleString(NS_LITERAL_STRING("SecurityButtonMixedContentTooltipText").get(),
                    aText);
  }
  else if (!tooltip.IsEmpty())
  {
    aText = tooltip;
  }
  else
  {
    GetBundleString(NS_LITERAL_STRING("SecurityButtonTooltipText").get(),
                    aText);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsSecureBrowserUIImpl::Observe(nsISupports*, const char*,
                               const PRUnichar*)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}


static nsresult IsChildOfDomWindow(nsIDOMWindow *parent, nsIDOMWindow *child,
                                   bool* value)
{
  *value = false;
  
  if (parent == child) {
    *value = true;
    return NS_OK;
  }
  
  nsCOMPtr<nsIDOMWindow> childsParent;
  child->GetParent(getter_AddRefs(childsParent));
  
  if (childsParent && childsParent.get() != child)
    IsChildOfDomWindow(parent, childsParent, value);
  
  return NS_OK;
}

static PRUint32 GetSecurityStateFromSecurityInfo(nsISupports *info)
{
  nsresult res;
  PRUint32 securityState;

  nsCOMPtr<nsITransportSecurityInfo> psmInfo(do_QueryInterface(info));
  if (!psmInfo) {
    PR_LOG(gSecureDocLog, PR_LOG_DEBUG, ("SecureUI: GetSecurityState: - no nsITransportSecurityInfo for %p\n",
                                         (nsISupports *)info));
    return nsIWebProgressListener::STATE_IS_INSECURE;
  }
  PR_LOG(gSecureDocLog, PR_LOG_DEBUG, ("SecureUI: GetSecurityState: - info is %p\n", 
                                       (nsISupports *)info));
  
  res = psmInfo->GetSecurityState(&securityState);
  if (NS_FAILED(res)) {
    PR_LOG(gSecureDocLog, PR_LOG_DEBUG, ("SecureUI: GetSecurityState: - GetSecurityState failed: %d\n",
                                         res));
    securityState = nsIWebProgressListener::STATE_IS_BROKEN;
  }
  
  PR_LOG(gSecureDocLog, PR_LOG_DEBUG, ("SecureUI: GetSecurityState: - Returning %d\n", 
                                       securityState));
  return securityState;
}


NS_IMETHODIMP
nsSecureBrowserUIImpl::Notify(nsIDOMHTMLFormElement* aDOMForm,
                              nsIDOMWindow* aWindow, nsIURI* actionURL,
                              bool* cancelSubmit)
{
  // Return NS_OK unless we want to prevent this form from submitting.
  *cancelSubmit = false;
  if (!aWindow || !actionURL || !aDOMForm)
    return NS_OK;
  
  nsCOMPtr<nsIContent> formNode = do_QueryInterface(aDOMForm);

  nsCOMPtr<nsIDocument> document = formNode->GetDocument();
  if (!document) return NS_OK;

  nsIPrincipal *principal = formNode->NodePrincipal();
  
  if (!principal)
  {
    *cancelSubmit = true;
    return NS_OK;
  }

  nsCOMPtr<nsIURI> formURL;
  if (NS_FAILED(principal->GetURI(getter_AddRefs(formURL))) ||
      !formURL)
  {
    formURL = document->GetDocumentURI();
  }

  nsCOMPtr<nsIDOMWindow> postingWindow =
    do_QueryInterface(document->GetWindow());
  // We can't find this document's window, cancel it.
  if (!postingWindow)
  {
    NS_WARNING("If you see this and can explain why it should be allowed, note in Bug 332324");
    *cancelSubmit = true;
    return NS_OK;
  }

  nsCOMPtr<nsIDOMWindow> window;
  {
    ReentrantMonitorAutoEnter lock(mReentrantMonitor);
    window = do_QueryReferent(mWindow);
    NS_ASSERTION(window, "Window has gone away?!");
  }

  bool isChild;
  IsChildOfDomWindow(window, postingWindow, &isChild);
  
  // This notify call is not for our window, ignore it.
  if (!isChild)
    return NS_OK;
  
  bool okayToPost;
  nsresult res = CheckPost(formURL, actionURL, &okayToPost);
  
  if (NS_SUCCEEDED(res) && !okayToPost)
    *cancelSubmit = true;
  
  return res;
}

//  nsIWebProgressListener
NS_IMETHODIMP 
nsSecureBrowserUIImpl::OnProgressChange(nsIWebProgress* aWebProgress,
                                        nsIRequest* aRequest,
                                        PRInt32 aCurSelfProgress,
                                        PRInt32 aMaxSelfProgress,
                                        PRInt32 aCurTotalProgress,
                                        PRInt32 aMaxTotalProgress)
{
  NS_NOTREACHED("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

void nsSecureBrowserUIImpl::ResetStateTracking()
{
  ReentrantMonitorAutoEnter lock(mReentrantMonitor);

  mInfoTooltip.Truncate();
  mDocumentRequestsInProgress = 0;
  if (mTransferringRequests.ops) {
    PL_DHashTableFinish(&mTransferringRequests);
    mTransferringRequests.ops = nsnull;
  }
  PL_DHashTableInit(&mTransferringRequests, &gMapOps, nsnull,
                    sizeof(RequestHashEntry), 16);
}

nsresult
nsSecureBrowserUIImpl::EvaluateAndUpdateSecurityState(nsIRequest* aRequest, nsISupports *info,
                                                      bool withNewLocation)
{
  /* I explicitly ignore the camelCase variable naming style here,
     I want to make it clear these are temp variables that relate to the 
     member variables with the same suffix.*/

  PRUint32 temp_NewToplevelSecurityState = nsIWebProgressListener::STATE_IS_INSECURE;
  bool temp_NewToplevelIsEV = false;

  bool updateStatus = false;
  nsCOMPtr<nsISSLStatus> temp_SSLStatus;

  bool updateTooltip = false;
  nsXPIDLString temp_InfoTooltip;

    temp_NewToplevelSecurityState = GetSecurityStateFromSecurityInfo(info);

    PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
           ("SecureUI:%p: OnStateChange: remember mNewToplevelSecurityState => %x\n", this,
            temp_NewToplevelSecurityState));

    nsCOMPtr<nsISSLStatusProvider> sp = do_QueryInterface(info);
    if (sp) {
      // Ignore result
      updateStatus = true;
      (void) sp->GetSSLStatus(getter_AddRefs(temp_SSLStatus));
      if (temp_SSLStatus) {
        bool aTemp;
        if (NS_SUCCEEDED(temp_SSLStatus->GetIsExtendedValidation(&aTemp))) {
          temp_NewToplevelIsEV = aTemp;
        }
      }
    }

    if (info) {
      nsCOMPtr<nsITransportSecurityInfo> secInfo(do_QueryInterface(info));
      if (secInfo) {
        updateTooltip = true;
        secInfo->GetShortSecurityDescription(getter_Copies(temp_InfoTooltip));
      }
    }

  // assume temp_NewToplevelSecurityState was set in this scope!
  // see code that is directly above

  {
    ReentrantMonitorAutoEnter lock(mReentrantMonitor);
    mNewToplevelSecurityStateKnown = true;
    mNewToplevelSecurityState = temp_NewToplevelSecurityState;
    mNewToplevelIsEV = temp_NewToplevelIsEV;
    if (updateStatus) {
      mSSLStatus = temp_SSLStatus;
    }
    if (updateTooltip) {
      mInfoTooltip = temp_InfoTooltip;
    }
    PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
           ("SecureUI:%p: remember securityInfo %p\n", this,
            info));
    nsCOMPtr<nsIAssociatedContentSecurity> associatedContentSecurityFromRequest =
        do_QueryInterface(aRequest);
    if (associatedContentSecurityFromRequest)
        mCurrentToplevelSecurityInfo = aRequest;
    else
        mCurrentToplevelSecurityInfo = info;

    // The subrequest counters are now in sync with 
    // mCurrentToplevelSecurityInfo, don't restore after top level
    // document load finishes.
    mRestoreSubrequests = false;
  }

  return UpdateSecurityState(aRequest, withNewLocation, 
                             updateStatus, updateTooltip);
}

void
nsSecureBrowserUIImpl::UpdateSubrequestMembers(nsISupports *securityInfo)
{
  // For wyciwyg channels in subdocuments we only update our
  // subrequest state members.
  PRUint32 reqState = GetSecurityStateFromSecurityInfo(securityInfo);

  // the code above this line should run without a lock
  ReentrantMonitorAutoEnter lock(mReentrantMonitor);

  if (reqState & STATE_IS_SECURE) {
    if (reqState & STATE_SECURE_LOW || reqState & STATE_SECURE_MED) {
      PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
             ("SecureUI:%p: OnStateChange: subreq LOW\n", this));
      ++mSubRequestsLowSecurity;
    } else {
      PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
             ("SecureUI:%p: OnStateChange: subreq HIGH\n", this));
      ++mSubRequestsHighSecurity;
    }
  } else if (reqState & STATE_IS_BROKEN) {
    PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
           ("SecureUI:%p: OnStateChange: subreq BROKEN\n", this));
    ++mSubRequestsBrokenSecurity;
  } else {
    PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
           ("SecureUI:%p: OnStateChange: subreq INSECURE\n", this));
    ++mSubRequestsNoSecurity;
  }
}

NS_IMETHODIMP
nsSecureBrowserUIImpl::OnStateChange(nsIWebProgress* aWebProgress,
                                     nsIRequest* aRequest,
                                     PRUint32 aProgressStateFlags,
                                     nsresult aStatus)
{
#ifdef DEBUG
  nsAutoAtomic atomic(mOnStateLocationChangeReentranceDetection);
  NS_ASSERTION(mOnStateLocationChangeReentranceDetection == 1,
               "unexpected parallel nsIWebProgress OnStateChange and/or OnLocationChange notification");
#endif
  /*
    All discussion, unless otherwise mentioned, only refers to
    http, https, file or wyciwig requests.


    Redirects are evil, well, some of them.
    There are multiple forms of redirects.

    Redirects caused by http refresh content are ok, because experiments show,
    with those redirects, the old page contents and their requests will come to STOP
    completely, before any progress from new refreshed page content is reported.
    So we can safely treat them as separate page loading transactions.

    Evil are redirects at the http protocol level, like code 302.

    If the toplevel documents gets replaced, i.e. redirected with 302, we do not care for the 
    security state of the initial transaction, which has now been redirected, 
    we only care for the new page load.
    
    For the implementation of the security UI, we make an assumption, that is hopefully true.
    
    Imagine, the received page that was delivered with the 302 redirection answer,
    also delivered html content.

    What happens if the parser starts to analyze the content and tries to load contained sub objects?
    
    In that case we would see start and stop requests for subdocuments, some for the previous document,
    some for the new target document. And only those for the new toplevel document may be
    taken into consideration, when deciding about the security state of the next toplevel document.
    
    Because security state is being looked at, when loading stops for (sub)documents, this 
    could cause real confusion, because we have to decide, whether an incoming progress 
    belongs to the new toplevel page, or the previous, already redirected page.
    
    Can we simplify here?
    
    If a redirect at the http protocol level is seen, can we safely assume, its html content
    will not be parsed, anylzed, and no embedded objects will get loaded (css, js, images),
    because the redirect is already happening?
    
    If we can assume that, this really simplify things. Because we will never see notification
    for sub requests that need to get ignored.
    
    I would like to make this assumption for now, but please let me (kaie) know if I'm wrong.
    
    Excurse:
      If my assumption is wrong, then we would require more tracking information.
      We need to keep lists of all pointers to request object that had been seen since the
      last toplevel start event.
      If the start for a redirected page is seen, the list of releveant object must be cleared,
      and only progress for requests which start after it must be analyzed.
      All other events must be ignored, as they belong to now irrelevant previous top level documents.


    Frames are also evil.

    First we need a decision.
    kaie thinks: 
      Only if the toplevel frame is secure, we should try to display secure lock icons.
      If some of the inner contents are insecure, we display mixed mode.
      
      But if the top level frame is not secure, why indicate a mixed lock icon at all?
      I think we should always display an open lock icon, if the top level frameset is insecure.
      
      That's the way Netscape Communicator behaves, and I think we should do the same.
      
      The user will not know which parts are secure and which are not,
      and any certificate information, displayed in the tooltip or in the "page info"
      will only be relevant for some subframe(s), and the user will not know which ones,
      so we shouldn't display it as a general attribute of the displayed page.

    Why are frames evil?
    
    Because the progress for the toplevel frame document is not easily distinguishable
    from subframes. The same STATE bits are reported.

    While at first sight, when a new page load happens,
    the toplevel frameset document has also the STATE_IS_NETWORK bit in it.
    But this can't really be used. Because in case that document causes a http 302 redirect, 
    the real top level frameset will no longer have that bit.
    
    But we need some way to distinguish top level frames from inner frames.
    
    I saw that the web progress we get delivered has a reference to the toplevel DOM window.
    
    I suggest, we look at all incoming requests.
    If a request is NOT for the toplevel DOM window, we will always treat it as a subdocument request,
    regardless of whether the load flags indicate a top level document.
  */

  nsCOMPtr<nsIDOMWindow> windowForProgress;
  aWebProgress->GetDOMWindow(getter_AddRefs(windowForProgress));

  nsCOMPtr<nsIDOMWindow> window;
  bool isViewSource;

  nsCOMPtr<nsINetUtil> ioService;

  {
    ReentrantMonitorAutoEnter lock(mReentrantMonitor);
    window = do_QueryReferent(mWindow);
    NS_ASSERTION(window, "Window has gone away?!");
    isViewSource = mIsViewSource;
    ioService = mIOService;
  }

  if (!ioService)
  {
    ioService = do_GetService(NS_IOSERVICE_CONTRACTID);
    if (ioService)
    {
      ReentrantMonitorAutoEnter lock(mReentrantMonitor);
      mIOService = ioService;
    }
  }

  bool isNoContentResponse = false;
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aRequest);
  if (httpChannel) 
  {
    PRUint32 response;
    isNoContentResponse = NS_SUCCEEDED(httpChannel->GetResponseStatus(&response)) &&
        (response == 204 || response == 205);
  }
  const bool isToplevelProgress = (windowForProgress.get() == window.get()) && !isNoContentResponse;
  
#ifdef PR_LOGGING
  if (windowForProgress)
  {
    if (isToplevelProgress)
    {
      PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
             ("SecureUI:%p: OnStateChange: progress: for toplevel\n", this));
    }
    else
    {
      PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
             ("SecureUI:%p: OnStateChange: progress: for something else\n", this));
    }
  }
  else
  {
      PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
             ("SecureUI:%p: OnStateChange: progress: no window known\n", this));
  }
#endif

  PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
         ("SecureUI:%p: OnStateChange\n", this));

  if (isViewSource)
    return NS_OK;

  if (!aRequest)
  {
    PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
           ("SecureUI:%p: OnStateChange with null request\n", this));
    return NS_ERROR_NULL_POINTER;
  }

#ifdef PR_LOGGING
  if (PR_LOG_TEST(gSecureDocLog, PR_LOG_DEBUG)) {
    nsXPIDLCString reqname;
    aRequest->GetName(reqname);
    PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
           ("SecureUI:%p: %p %p OnStateChange %x %s\n", this, aWebProgress,
            aRequest, aProgressStateFlags, reqname.get()));
  }
#endif

  nsCOMPtr<nsISupports> securityInfo(ExtractSecurityInfo(aRequest));

  nsCOMPtr<nsIURI> uri;
  nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
  if (channel) {
    channel->GetURI(getter_AddRefs(uri));
  }

  nsCOMPtr<imgIRequest> imgRequest(do_QueryInterface(aRequest));
  if (imgRequest) {
    NS_ASSERTION(!channel, "How did that happen, exactly?");
    // for image requests, we get the URI from here
    imgRequest->GetURI(getter_AddRefs(uri));
  }
  
  if (uri) {
    bool vs;
    if (NS_SUCCEEDED(uri->SchemeIs("javascript", &vs)) && vs) {
      // We ignore the progress events for javascript URLs.
      // If a document loading gets triggered, we will see more events.
      return NS_OK;
    }
  }

  PRUint32 loadFlags = 0;
  aRequest->GetLoadFlags(&loadFlags);

#ifdef PR_LOGGING
  if (aProgressStateFlags & STATE_START
      &&
      aProgressStateFlags & STATE_IS_REQUEST
      &&
      isToplevelProgress
      &&
      loadFlags & nsIChannel::LOAD_DOCUMENT_URI)
  {
    PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
           ("SecureUI:%p: OnStateChange: SOMETHING STARTS FOR TOPMOST DOCUMENT\n", this));
  }

  if (aProgressStateFlags & STATE_STOP
      &&
      aProgressStateFlags & STATE_IS_REQUEST
      &&
      isToplevelProgress
      &&
      loadFlags & nsIChannel::LOAD_DOCUMENT_URI)
  {
    PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
           ("SecureUI:%p: OnStateChange: SOMETHING STOPS FOR TOPMOST DOCUMENT\n", this));
  }
#endif

  bool isSubDocumentRelevant = true;

  // We are only interested in requests that load in the browser window...
  if (!imgRequest) { // is not imgRequest
    nsCOMPtr<nsIHttpChannel> httpRequest(do_QueryInterface(aRequest));
    if (!httpRequest) {
      nsCOMPtr<nsIFileChannel> fileRequest(do_QueryInterface(aRequest));
      if (!fileRequest) {
        nsCOMPtr<nsIWyciwygChannel> wyciwygRequest(do_QueryInterface(aRequest));
        if (!wyciwygRequest) {
          nsCOMPtr<nsIFTPChannel> ftpRequest(do_QueryInterface(aRequest));
          if (!ftpRequest) {
            PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
                   ("SecureUI:%p: OnStateChange: not relevant for sub content\n", this));
            isSubDocumentRelevant = false;
          }
        }
      }
    }
  }

  // This will ignore all resource, chrome, data, file, moz-icon, and anno
  // protocols. Local resources are treated as trusted.
  if (uri && ioService) {
    bool hasFlag;
    nsresult rv = 
      ioService->URIChainHasFlags(uri, 
                                  nsIProtocolHandler::URI_IS_LOCAL_RESOURCE,
                                  &hasFlag);
    if (NS_SUCCEEDED(rv) && hasFlag) {
      isSubDocumentRelevant = false;
    }
  }

#if defined(DEBUG)
  nsCString info2;
  PRUint32 testFlags = loadFlags;

  if (testFlags & nsIChannel::LOAD_DOCUMENT_URI)
  {
    testFlags -= nsIChannel::LOAD_DOCUMENT_URI;
    info2.Append("LOAD_DOCUMENT_URI ");
  }
  if (testFlags & nsIChannel::LOAD_RETARGETED_DOCUMENT_URI)
  {
    testFlags -= nsIChannel::LOAD_RETARGETED_DOCUMENT_URI;
    info2.Append("LOAD_RETARGETED_DOCUMENT_URI ");
  }
  if (testFlags & nsIChannel::LOAD_REPLACE)
  {
    testFlags -= nsIChannel::LOAD_REPLACE;
    info2.Append("LOAD_REPLACE ");
  }

  const char *_status = NS_SUCCEEDED(aStatus) ? "1" : "0";

  nsCString info;
  PRUint32 f = aProgressStateFlags;
  if (f & nsIWebProgressListener::STATE_START)
  {
    f -= nsIWebProgressListener::STATE_START;
    info.Append("START ");
  }
  if (f & nsIWebProgressListener::STATE_REDIRECTING)
  {
    f -= nsIWebProgressListener::STATE_REDIRECTING;
    info.Append("REDIRECTING ");
  }
  if (f & nsIWebProgressListener::STATE_TRANSFERRING)
  {
    f -= nsIWebProgressListener::STATE_TRANSFERRING;
    info.Append("TRANSFERRING ");
  }
  if (f & nsIWebProgressListener::STATE_NEGOTIATING)
  {
    f -= nsIWebProgressListener::STATE_NEGOTIATING;
    info.Append("NEGOTIATING ");
  }
  if (f & nsIWebProgressListener::STATE_STOP)
  {
    f -= nsIWebProgressListener::STATE_STOP;
    info.Append("STOP ");
  }
  if (f & nsIWebProgressListener::STATE_IS_REQUEST)
  {
    f -= nsIWebProgressListener::STATE_IS_REQUEST;
    info.Append("IS_REQUEST ");
  }
  if (f & nsIWebProgressListener::STATE_IS_DOCUMENT)
  {
    f -= nsIWebProgressListener::STATE_IS_DOCUMENT;
    info.Append("IS_DOCUMENT ");
  }
  if (f & nsIWebProgressListener::STATE_IS_NETWORK)
  {
    f -= nsIWebProgressListener::STATE_IS_NETWORK;
    info.Append("IS_NETWORK ");
  }
  if (f & nsIWebProgressListener::STATE_IS_WINDOW)
  {
    f -= nsIWebProgressListener::STATE_IS_WINDOW;
    info.Append("IS_WINDOW ");
  }
  if (f & nsIWebProgressListener::STATE_IS_INSECURE)
  {
    f -= nsIWebProgressListener::STATE_IS_INSECURE;
    info.Append("IS_INSECURE ");
  }
  if (f & nsIWebProgressListener::STATE_IS_BROKEN)
  {
    f -= nsIWebProgressListener::STATE_IS_BROKEN;
    info.Append("IS_BROKEN ");
  }
  if (f & nsIWebProgressListener::STATE_IS_SECURE)
  {
    f -= nsIWebProgressListener::STATE_IS_SECURE;
    info.Append("IS_SECURE ");
  }
  if (f & nsIWebProgressListener::STATE_SECURE_HIGH)
  {
    f -= nsIWebProgressListener::STATE_SECURE_HIGH;
    info.Append("SECURE_HIGH ");
  }
  if (f & nsIWebProgressListener::STATE_SECURE_MED)
  {
    f -= nsIWebProgressListener::STATE_SECURE_MED;
    info.Append("SECURE_MED ");
  }
  if (f & nsIWebProgressListener::STATE_SECURE_LOW)
  {
    f -= nsIWebProgressListener::STATE_SECURE_LOW;
    info.Append("SECURE_LOW ");
  }
  if (f & nsIWebProgressListener::STATE_RESTORING)
  {
    f -= nsIWebProgressListener::STATE_RESTORING;
    info.Append("STATE_RESTORING ");
  }

  if (f > 0)
  {
    info.Append("f contains unknown flag!");
  }

  PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
         ("SecureUI:%p: OnStateChange: %s %s -- %s\n", this, _status, 
          info.get(), info2.get()));

  if (aProgressStateFlags & STATE_STOP
      &&
      channel)
  {
    PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
           ("SecureUI:%p: OnStateChange: seeing STOP with security state: %d\n", this,
            GetSecurityStateFromSecurityInfo(securityInfo)
            ));
  }
#endif

  if (aProgressStateFlags & STATE_TRANSFERRING
      &&
      aProgressStateFlags & STATE_IS_REQUEST)
  {
    // The listing of a request in mTransferringRequests
    // means, there has already been data transfered.

    ReentrantMonitorAutoEnter lock(mReentrantMonitor);
    PL_DHashTableOperate(&mTransferringRequests, aRequest, PL_DHASH_ADD);
    
    return NS_OK;
  }

  bool requestHasTransferedData = false;

  if (aProgressStateFlags & STATE_STOP
      &&
      aProgressStateFlags & STATE_IS_REQUEST)
  {
    { /* scope for the ReentrantMonitorAutoEnter */
      ReentrantMonitorAutoEnter lock(mReentrantMonitor);
      PLDHashEntryHdr *entry = PL_DHashTableOperate(&mTransferringRequests, aRequest, PL_DHASH_LOOKUP);
      if (PL_DHASH_ENTRY_IS_BUSY(entry))
      {
        PL_DHashTableOperate(&mTransferringRequests, aRequest, PL_DHASH_REMOVE);

        requestHasTransferedData = true;
      }
    }

    if (!requestHasTransferedData) {
      // Because image loads doesn't support any TRANSFERRING notifications but
      // only START and STOP we must ask them directly whether content was
      // transferred.  See bug 432685 for details.
      nsCOMPtr<nsISecurityInfoProvider> securityInfoProvider =
        do_QueryInterface(aRequest);
      // Guess true in all failure cases to be safe.  But if we're not
      // an nsISecurityInfoProvider, then we just haven't transferred
      // any data.
      bool hasTransferred;
      requestHasTransferedData =
        securityInfoProvider &&
        (NS_FAILED(securityInfoProvider->GetHasTransferredData(&hasTransferred)) ||
         hasTransferred);
    }
  }

  bool allowSecurityStateChange = true;
  if (loadFlags & nsIChannel::LOAD_RETARGETED_DOCUMENT_URI)
  {
    // The original consumer (this) is no longer the target of the load.
    // Ignore any events with this flag, do not allow them to update
    // our secure UI state.
    allowSecurityStateChange = false;
  }

  if (aProgressStateFlags & STATE_START
      &&
      aProgressStateFlags & STATE_IS_REQUEST
      &&
      isToplevelProgress
      &&
      loadFlags & nsIChannel::LOAD_DOCUMENT_URI)
  {
    bool inProgress;

    PRInt32 saveSubHigh;
    PRInt32 saveSubLow;
    PRInt32 saveSubBroken;
    PRInt32 saveSubNo;
    nsCOMPtr<nsIAssociatedContentSecurity> prevContentSecurity;

    PRInt32 newSubHigh = 0;
    PRInt32 newSubLow = 0;
    PRInt32 newSubBroken = 0;
    PRInt32 newSubNo = 0;

    {
      ReentrantMonitorAutoEnter lock(mReentrantMonitor);
      inProgress = (mDocumentRequestsInProgress!=0);

      if (allowSecurityStateChange && !inProgress)
      {
        saveSubHigh = mSubRequestsHighSecurity;
        saveSubLow = mSubRequestsLowSecurity;
        saveSubBroken = mSubRequestsBrokenSecurity;
        saveSubNo = mSubRequestsNoSecurity;
        prevContentSecurity = do_QueryInterface(mCurrentToplevelSecurityInfo);
      }
    }

    if (allowSecurityStateChange && !inProgress)
    {
      PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
             ("SecureUI:%p: OnStateChange: start for toplevel document\n", this
              ));

      if (prevContentSecurity)
      {
        PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
               ("SecureUI:%p: OnStateChange: start, saving current sub state\n", this
                ));
  
        // before resetting our state, let's save information about
        // sub element loads, so we can restore it later
        prevContentSecurity->SetCountSubRequestsHighSecurity(saveSubHigh);
        prevContentSecurity->SetCountSubRequestsLowSecurity(saveSubLow);
        prevContentSecurity->SetCountSubRequestsBrokenSecurity(saveSubBroken);
        prevContentSecurity->SetCountSubRequestsNoSecurity(saveSubNo);
        prevContentSecurity->Flush();
        PR_LOG(gSecureDocLog, PR_LOG_DEBUG, ("SecureUI:%p: Saving subs in START to %p as %d,%d,%d,%d\n", 
          this, prevContentSecurity.get(), saveSubHigh, saveSubLow, saveSubBroken, saveSubNo));      
      }

      bool retrieveAssociatedState = false;

      if (securityInfo &&
          (aProgressStateFlags & nsIWebProgressListener::STATE_RESTORING) != 0) {
        retrieveAssociatedState = true;
      } else {
        nsCOMPtr<nsIWyciwygChannel> wyciwygRequest(do_QueryInterface(aRequest));
        if (wyciwygRequest) {
          retrieveAssociatedState = true;
        }
      }

      if (retrieveAssociatedState)
      {
        // When restoring from bfcache, we will not get events for the 
        // page's sub elements, so let's load the state of sub elements
        // from the cache.
    
        nsCOMPtr<nsIAssociatedContentSecurity> 
          newContentSecurity(do_QueryInterface(securityInfo));
    
        if (newContentSecurity)
        {
          PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
                 ("SecureUI:%p: OnStateChange: start, loading old sub state\n", this
                  ));
    
          newContentSecurity->GetCountSubRequestsHighSecurity(&newSubHigh);
          newContentSecurity->GetCountSubRequestsLowSecurity(&newSubLow);
          newContentSecurity->GetCountSubRequestsBrokenSecurity(&newSubBroken);
          newContentSecurity->GetCountSubRequestsNoSecurity(&newSubNo);
          PR_LOG(gSecureDocLog, PR_LOG_DEBUG, ("SecureUI:%p: Restoring subs in START from %p to %d,%d,%d,%d\n", 
            this, newContentSecurity.get(), newSubHigh, newSubLow, newSubBroken, newSubNo));      
        }
      }
      else
      {
        // If we don't get OnLocationChange for this top level load later,
        // it didn't get rendered.  But we reset the state to unknown and
        // mSubRequests* to zeros.  If we would have left these values after 
        // this top level load stoped, we would override the original top level
        // load with all zeros and break mixed content state on back and forward.
        mRestoreSubrequests = true;
      }
    }

    {
      ReentrantMonitorAutoEnter lock(mReentrantMonitor);

      if (allowSecurityStateChange && !inProgress)
      {
        ResetStateTracking();
        mSubRequestsHighSecurity = newSubHigh;
        mSubRequestsLowSecurity = newSubLow;
        mSubRequestsBrokenSecurity = newSubBroken;
        mSubRequestsNoSecurity = newSubNo;
        mNewToplevelSecurityStateKnown = false;
      }

      // By using a counter, this code also works when the toplevel
      // document get's redirected, but the STOP request for the 
      // previous toplevel document has not yet have been received.
      PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
             ("SecureUI:%p: OnStateChange: ++mDocumentRequestsInProgress\n", this
              ));
      ++mDocumentRequestsInProgress;
    }

    return NS_OK;
  }

  if (aProgressStateFlags & STATE_STOP
      &&
      aProgressStateFlags & STATE_IS_REQUEST
      &&
      isToplevelProgress
      &&
      loadFlags & nsIChannel::LOAD_DOCUMENT_URI)
  {
    PRInt32 temp_DocumentRequestsInProgress;
    nsCOMPtr<nsISecurityEventSink> temp_ToplevelEventSink;

    {
      ReentrantMonitorAutoEnter lock(mReentrantMonitor);
      temp_DocumentRequestsInProgress = mDocumentRequestsInProgress;
      if (allowSecurityStateChange)
      {
        temp_ToplevelEventSink = mToplevelEventSink;
      }
    }

    if (temp_DocumentRequestsInProgress <= 0)
    {
      // Ignore stop requests unless a document load is in progress
      // Unfortunately on application start, see some stops without having seen any starts...
      return NS_OK;
    }

    PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
           ("SecureUI:%p: OnStateChange: --mDocumentRequestsInProgress\n", this
            ));

    if (!temp_ToplevelEventSink && channel)
    {
      if (allowSecurityStateChange)
      {
        ObtainEventSink(channel, temp_ToplevelEventSink);
      }
    }

    bool sinkChanged = false;
    bool inProgress;
    {
      ReentrantMonitorAutoEnter lock(mReentrantMonitor);
      if (allowSecurityStateChange)
      {
        sinkChanged = (mToplevelEventSink != temp_ToplevelEventSink);
        mToplevelEventSink = temp_ToplevelEventSink;
      }
      --mDocumentRequestsInProgress;
      inProgress = mDocumentRequestsInProgress > 0;
    }

    if (allowSecurityStateChange && requestHasTransferedData) {
      // Data has been transferred for the single toplevel
      // request. Evaluate the security state.

      // Do this only when the sink has changed.  We update and notify
      // the state from OnLacationChange, this is actually redundant.
      // But when the target sink changes between OnLocationChange and
      // OnStateChange, we have to fire the notification here (again).

      if (sinkChanged || mOnLocationChangeSeen)
        return EvaluateAndUpdateSecurityState(aRequest, securityInfo, false);
    }
    mOnLocationChangeSeen = false;

    if (mRestoreSubrequests && !inProgress)
    {
      // We get here when there were no OnLocationChange between 
      // OnStateChange(START) and OnStateChange(STOP).  Then the load has not
      // been rendered but has been retargeted in some other way then by external
      // app handler.  Restore mSubRequests* members to what the current security 
      // state info holds (it was reset to all zero in OnStateChange(START) 
      // before).
      nsCOMPtr<nsIAssociatedContentSecurity> currentContentSecurity;
      {
        ReentrantMonitorAutoEnter lock(mReentrantMonitor);
        currentContentSecurity = do_QueryInterface(mCurrentToplevelSecurityInfo);

        // Drop this indication flag, the restore opration is just being
        // done.
        mRestoreSubrequests = false;

        // We can do this since the state didn't actually change.
        mNewToplevelSecurityStateKnown = true;
      }

      PRInt32 subHigh = 0;
      PRInt32 subLow = 0;
      PRInt32 subBroken = 0;
      PRInt32 subNo = 0;

      if (currentContentSecurity)
      {
        currentContentSecurity->GetCountSubRequestsHighSecurity(&subHigh);
        currentContentSecurity->GetCountSubRequestsLowSecurity(&subLow);
        currentContentSecurity->GetCountSubRequestsBrokenSecurity(&subBroken);
        currentContentSecurity->GetCountSubRequestsNoSecurity(&subNo);
        PR_LOG(gSecureDocLog, PR_LOG_DEBUG, ("SecureUI:%p: Restoring subs in STOP from %p to %d,%d,%d,%d\n", 
          this, currentContentSecurity.get(), subHigh, subLow, subBroken, subNo));      
      }

      {
        ReentrantMonitorAutoEnter lock(mReentrantMonitor);
        mSubRequestsHighSecurity = subHigh;
        mSubRequestsLowSecurity = subLow;
        mSubRequestsBrokenSecurity = subBroken;
        mSubRequestsNoSecurity = subNo;
      }
    }
    
    return NS_OK;
  }
  
  if (aProgressStateFlags & STATE_STOP
      &&
      aProgressStateFlags & STATE_IS_REQUEST)
  {
    if (!isSubDocumentRelevant)
      return NS_OK;
    
    // if we arrive here, LOAD_DOCUMENT_URI is not set
    
    // We only care for the security state of sub requests which have actually transfered data.

    if (allowSecurityStateChange && requestHasTransferedData)
    {  
      UpdateSubrequestMembers(securityInfo);
      
      // Care for the following scenario:
      // A new top level document load might have already started,
      // but the security state of the new top level document might not yet been known.
      // 
      // At this point, we are learning about the security state of a sub-document.
      // We must not update the security state based on the sub content,
      // if the new top level state is not yet known.
      //
      // We skip updating the security state in this case.

      bool temp_NewToplevelSecurityStateKnown;
      {
        ReentrantMonitorAutoEnter lock(mReentrantMonitor);
        temp_NewToplevelSecurityStateKnown = mNewToplevelSecurityStateKnown;
      }

      if (temp_NewToplevelSecurityStateKnown)
        return UpdateSecurityState(aRequest, false, false, false);
    }

    return NS_OK;
  }

  return NS_OK;
}

// I'm keeping this as a separate function, in order to simplify the review
// for bug 412456. We should inline this in a follow up patch.
void nsSecureBrowserUIImpl::ObtainEventSink(nsIChannel *channel, 
                                            nsCOMPtr<nsISecurityEventSink> &sink)
{
  if (!sink)
    NS_QueryNotificationCallbacks(channel, sink);
}

nsresult nsSecureBrowserUIImpl::UpdateSecurityState(nsIRequest* aRequest, 
                                                    bool withNewLocation, 
                                                    bool withUpdateStatus, 
                                                    bool withUpdateTooltip)
{
  lockIconState warnSecurityState = lis_no_security;
  bool showWarning = false;
  nsresult rv = NS_OK;

  // both parameters are both input and outout
  bool flagsChanged = UpdateMyFlags(showWarning, warnSecurityState);

  if (flagsChanged || withNewLocation || withUpdateStatus || withUpdateTooltip)
    rv = TellTheWorld(showWarning, warnSecurityState, aRequest);

  return rv;
}

// must not fail, by definition, only trivial assignments
// or string operations are allowed
// returns true if our overall state has changed and we must send out notifications
bool nsSecureBrowserUIImpl::UpdateMyFlags(bool &showWarning, lockIconState &warnSecurityState)
{
  ReentrantMonitorAutoEnter lock(mReentrantMonitor);
  bool mustTellTheWorld = false;

  lockIconState newSecurityState;

  if (mNewToplevelSecurityState & STATE_IS_SECURE)
  {
    if (mNewToplevelSecurityState & STATE_SECURE_LOW
        ||
        mNewToplevelSecurityState & STATE_SECURE_MED)
    {
      if (mSubRequestsBrokenSecurity
          ||
          mSubRequestsNoSecurity)
      {
        newSecurityState = lis_mixed_security;
      }
      else
      {
        newSecurityState = lis_low_security;
      }
    }
    else
    {
      // toplevel is high security

      if (mSubRequestsBrokenSecurity
          ||
          mSubRequestsNoSecurity)
      {
        newSecurityState = lis_mixed_security;
      }
      else if (mSubRequestsLowSecurity)
      {
        newSecurityState = lis_low_security;
      }
      else
      {
        newSecurityState = lis_high_security;
      }
    }
  }
  else
  if (mNewToplevelSecurityState & STATE_IS_BROKEN)
  {
    // indicating BROKEN is more important than MIXED.
  
    newSecurityState = lis_broken_security;
  }
  else
  {
    newSecurityState = lis_no_security;
  }

  PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
         ("SecureUI:%p: UpdateSecurityState:  old-new  %d - %d\n", this,
         mNotifiedSecurityState, newSecurityState
          ));

  if (mNotifiedSecurityState != newSecurityState)
  {
    mustTellTheWorld = true;

    // we'll treat "broken" exactly like "insecure",
    // i.e. we do not show alerts when switching between broken and insecure

    /*
      from                 to           shows alert
    ------------------------------     ---------------

    no or broken -> no or broken    => <NOTHING SHOWN>

    no or broken -> mixed           => mixed alert
    no or broken -> low             => low alert
    no or broken -> high            => high alert
    
    mixed, high, low -> no, broken  => leaving secure

    mixed        -> low             => low alert
    mixed        -> high            => high alert

    high         -> low             => low alert
    high         -> mixed           => mixed
    
    low          -> high            => high
    low          -> mixed           => mixed


      security    icon
      ----------------
    
      no          open
      mixed       broken
      broken      broken
      low         low
      high        high
    */

    showWarning = true;
    
    switch (mNotifiedSecurityState)
    {
      case lis_no_security:
      case lis_broken_security:
        switch (newSecurityState)
        {
          case lis_no_security:
          case lis_broken_security:
            showWarning = false;
            break;
          
          default:
            break;
        }
      
      default:
        break;
    }

    if (showWarning)
    {
      warnSecurityState = newSecurityState;
    }
    
    mNotifiedSecurityState = newSecurityState;

    if (lis_no_security == newSecurityState)
    {
      mSSLStatus = nsnull;
      mInfoTooltip.Truncate();
    }
  }

  if (mNotifiedToplevelIsEV != mNewToplevelIsEV) {
    mustTellTheWorld = true;
    mNotifiedToplevelIsEV = mNewToplevelIsEV;
  }

  return mustTellTheWorld;
}

nsresult nsSecureBrowserUIImpl::TellTheWorld(bool showWarning, 
                                             lockIconState warnSecurityState, 
                                             nsIRequest* aRequest)
{
  nsCOMPtr<nsISecurityEventSink> temp_ToplevelEventSink;
  lockIconState temp_NotifiedSecurityState;
  bool temp_NotifiedToplevelIsEV;

  {
    ReentrantMonitorAutoEnter lock(mReentrantMonitor);
    temp_ToplevelEventSink = mToplevelEventSink;
    temp_NotifiedSecurityState = mNotifiedSecurityState;
    temp_NotifiedToplevelIsEV = mNotifiedToplevelIsEV;
  }

  if (temp_ToplevelEventSink)
  {
    PRUint32 newState = STATE_IS_INSECURE;
    MapInternalToExternalState(&newState, 
                               temp_NotifiedSecurityState, 
                               temp_NotifiedToplevelIsEV);

    PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
           ("SecureUI:%p: UpdateSecurityState: calling OnSecurityChange\n", this
            ));

    temp_ToplevelEventSink->OnSecurityChange(aRequest, newState);
  }
  else
  {
    PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
           ("SecureUI:%p: UpdateSecurityState: NO mToplevelEventSink!\n", this
            ));

  }

  if (showWarning)
  {
    switch (warnSecurityState)
    {
      case lis_no_security:
      case lis_broken_security:
        ConfirmLeavingSecure();
        break;

      case lis_mixed_security:
        ConfirmMixedMode();
        break;

      case lis_low_security:
        ConfirmEnteringWeak();
        break;

      case lis_high_security:
        ConfirmEnteringSecure();
        break;
    }
  }

  return NS_OK; 
}

NS_IMETHODIMP
nsSecureBrowserUIImpl::OnLocationChange(nsIWebProgress* aWebProgress,
                                        nsIRequest* aRequest,
                                        nsIURI* aLocation)
{
#ifdef DEBUG
  nsAutoAtomic atomic(mOnStateLocationChangeReentranceDetection);
  NS_ASSERTION(mOnStateLocationChangeReentranceDetection == 1,
               "unexpected parallel nsIWebProgress OnStateChange and/or OnLocationChange notification");
#endif
  PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
         ("SecureUI:%p: OnLocationChange\n", this));

  bool updateIsViewSource = false;
  bool temp_IsViewSource = false;
  nsCOMPtr<nsIDOMWindow> window;

  if (aLocation)
  {
    bool vs;

    nsresult rv = aLocation->SchemeIs("view-source", &vs);
    NS_ENSURE_SUCCESS(rv, rv);

    if (vs) {
      PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
             ("SecureUI:%p: OnLocationChange: view-source\n", this));
    }

    updateIsViewSource = true;
    temp_IsViewSource = vs;
  }

  {
    ReentrantMonitorAutoEnter lock(mReentrantMonitor);
    if (updateIsViewSource) {
      mIsViewSource = temp_IsViewSource;
    }
    mCurrentURI = aLocation;
    window = do_QueryReferent(mWindow);
    NS_ASSERTION(window, "Window has gone away?!");
  }

  // If the location change does not have a corresponding request, then we
  // assume that it does not impact the security state.
  if (!aRequest)
    return NS_OK;

  // The location bar has changed, so we must update the security state.  The
  // only concern with doing this here is that a page may transition from being
  // reported as completely secure to being reported as partially secure
  // (mixed).  This may be confusing for users, and it may bother users who
  // like seeing security dialogs.  However, it seems prudent given that page
  // loading may never end in some edge cases (perhaps by a site with malicious
  // intent).

  nsCOMPtr<nsIDOMWindow> windowForProgress;
  aWebProgress->GetDOMWindow(getter_AddRefs(windowForProgress));

  nsCOMPtr<nsISupports> securityInfo(ExtractSecurityInfo(aRequest));

  if (windowForProgress.get() == window.get()) {
    // For toplevel channels, update the security state right away.
    mOnLocationChangeSeen = true;
    return EvaluateAndUpdateSecurityState(aRequest, securityInfo, true);
  }

  // For channels in subdocuments we only update our subrequest state members.
  UpdateSubrequestMembers(securityInfo);

  // Care for the following scenario:

  // A new toplevel document load might have already started, but the security
  // state of the new toplevel document might not yet be known.
  // 
  // At this point, we are learning about the security state of a sub-document.
  // We must not update the security state based on the sub content, if the new
  // top level state is not yet known.
  //
  // We skip updating the security state in this case.

  bool temp_NewToplevelSecurityStateKnown;
  {
    ReentrantMonitorAutoEnter lock(mReentrantMonitor);
    temp_NewToplevelSecurityStateKnown = mNewToplevelSecurityStateKnown;
  }

  if (temp_NewToplevelSecurityStateKnown)
    return UpdateSecurityState(aRequest, true, false, false);

  return NS_OK;
}

NS_IMETHODIMP
nsSecureBrowserUIImpl::OnStatusChange(nsIWebProgress* aWebProgress,
                                      nsIRequest* aRequest,
                                      nsresult aStatus,
                                      const PRUnichar* aMessage)
{
  NS_NOTREACHED("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

nsresult
nsSecureBrowserUIImpl::OnSecurityChange(nsIWebProgress *aWebProgress,
                                        nsIRequest *aRequest,
                                        PRUint32 state)
{
#if defined(DEBUG)
  nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
  if (!channel)
    return NS_OK;

  nsCOMPtr<nsIURI> aURI;
  channel->GetURI(getter_AddRefs(aURI));
  
  if (aURI) {
    nsCAutoString temp;
    aURI->GetSpec(temp);
    PR_LOG(gSecureDocLog, PR_LOG_DEBUG,
           ("SecureUI:%p: OnSecurityChange: (%x) %s\n", this,
            state, temp.get()));
  }
#endif

  return NS_OK;
}

// nsISSLStatusProvider methods
NS_IMETHODIMP
nsSecureBrowserUIImpl::GetSSLStatus(nsISSLStatus** _result)
{
  NS_ENSURE_ARG_POINTER(_result);

  ReentrantMonitorAutoEnter lock(mReentrantMonitor);

  switch (mNotifiedSecurityState)
  {
    case lis_mixed_security:
    case lis_low_security:
    case lis_high_security:
      break;

    default:
      NS_NOTREACHED("if this is reached you must add more entries to the switch");
    case lis_no_security:
    case lis_broken_security:
      *_result = nsnull;
      return NS_OK;
  }
 
  *_result = mSSLStatus;
  NS_IF_ADDREF(*_result);

  return NS_OK;
}

nsresult
nsSecureBrowserUIImpl::IsURLHTTPS(nsIURI* aURL, bool* value)
{
  *value = false;

  if (!aURL)
    return NS_OK;

  return aURL->SchemeIs("https", value);
}

nsresult
nsSecureBrowserUIImpl::IsURLJavaScript(nsIURI* aURL, bool* value)
{
  *value = false;

  if (!aURL)
    return NS_OK;

  return aURL->SchemeIs("javascript", value);
}

void
nsSecureBrowserUIImpl::GetBundleString(const PRUnichar* name,
                                       nsAString &outString)
{
  nsCOMPtr<nsIStringBundle> temp_StringBundle;

  {
    ReentrantMonitorAutoEnter lock(mReentrantMonitor);
    temp_StringBundle = mStringBundle;
  }

  if (temp_StringBundle && name) {
    PRUnichar *ptrv = nsnull;
    if (NS_SUCCEEDED(temp_StringBundle->GetStringFromName(name,
                                                          &ptrv)))
      outString = ptrv;
    else
      outString.SetLength(0);

    nsMemory::Free(ptrv);

  } else {
    outString.SetLength(0);
  }
}

nsresult
nsSecureBrowserUIImpl::CheckPost(nsIURI *formURL, nsIURI *actionURL, bool *okayToPost)
{
  bool formSecure, actionSecure, actionJavaScript;
  *okayToPost = true;

  nsresult rv = IsURLHTTPS(formURL, &formSecure);
  if (NS_FAILED(rv))
    return rv;

  rv = IsURLHTTPS(actionURL, &actionSecure);
  if (NS_FAILED(rv))
    return rv;

  rv = IsURLJavaScript(actionURL, &actionJavaScript);
  if (NS_FAILED(rv))
    return rv;

  // If we are posting to a secure link, all is okay.
  // It doesn't matter whether the currently viewed page is secure or not,
  // because the data will be sent to a secure URL.
  if (actionSecure) {
    return NS_OK;
  }

  // Action is a JavaScript call, not an actual post. That's okay too.
  if (actionJavaScript) {
    return NS_OK;
  }

  // posting to insecure webpage from a secure webpage.
  if (formSecure) {
    *okayToPost = ConfirmPostToInsecureFromSecure();
  } else {
    *okayToPost = ConfirmPostToInsecure();
  }

  return NS_OK;
}

//
// Implementation of an nsIInterfaceRequestor for use
// as context for NSS calls
//
class nsUIContext : public nsIInterfaceRequestor
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIINTERFACEREQUESTOR

  nsUIContext(nsIDOMWindow *window);
  virtual ~nsUIContext();

private:
  nsCOMPtr<nsIDOMWindow> mWindow;
};

NS_IMPL_ISUPPORTS1(nsUIContext, nsIInterfaceRequestor)

nsUIContext::nsUIContext(nsIDOMWindow *aWindow)
: mWindow(aWindow)
{
}

nsUIContext::~nsUIContext()
{
}

/* void getInterface (in nsIIDRef uuid, [iid_is (uuid), retval] out nsQIResult result); */
NS_IMETHODIMP nsUIContext::GetInterface(const nsIID & uuid, void * *result)
{
  NS_ENSURE_TRUE(mWindow, NS_ERROR_FAILURE);
  nsresult rv;

  if (uuid.Equals(NS_GET_IID(nsIPrompt))) {
    nsCOMPtr<nsIDOMWindow> window = do_QueryInterface(mWindow, &rv);
    if (NS_FAILED(rv)) return rv;

    nsIPrompt *prompt;

    rv = window->GetPrompter(&prompt);
    *result = prompt;
  } else if (uuid.Equals(NS_GET_IID(nsIDOMWindow))) {
    *result = mWindow;
    NS_ADDREF ((nsISupports*) *result);
    rv = NS_OK;
  } else {
    rv = NS_ERROR_NO_INTERFACE;
  }

  return rv;
}

bool
nsSecureBrowserUIImpl::GetNSSDialogs(nsCOMPtr<nsISecurityWarningDialogs> & dialogs,
                                     nsCOMPtr<nsIInterfaceRequestor> & ctx)
{
  if (!NS_IsMainThread()) {
    NS_ERROR("nsSecureBrowserUIImpl::GetNSSDialogs called off the main thread");
    return false;
  }

  dialogs = do_GetService(NS_SECURITYWARNINGDIALOGS_CONTRACTID);
  if (!dialogs)
    return false;

  nsCOMPtr<nsIDOMWindow> window;
  {
    ReentrantMonitorAutoEnter lock(mReentrantMonitor);
    window = do_QueryReferent(mWindow);
    NS_ASSERTION(window, "Window has gone away?!");
  }
  ctx = new nsUIContext(window);
  
  return true;
}

bool nsSecureBrowserUIImpl::
ConfirmEnteringSecure()
{
  nsCOMPtr<nsISecurityWarningDialogs> dialogs;
  nsCOMPtr<nsIInterfaceRequestor> ctx;

  if (!GetNSSDialogs(dialogs, ctx)) {
    return false; // Should this allow true for unimplemented?
  }

  bool confirms;
  dialogs->ConfirmEnteringSecure(ctx, &confirms);

  return confirms;
}

bool nsSecureBrowserUIImpl::
ConfirmEnteringWeak()
{
  nsCOMPtr<nsISecurityWarningDialogs> dialogs;
  nsCOMPtr<nsIInterfaceRequestor> ctx;

  if (!GetNSSDialogs(dialogs, ctx)) {
    return false; // Should this allow true for unimplemented?
  }

  bool confirms;
  dialogs->ConfirmEnteringWeak(ctx, &confirms);

  return confirms;
}

bool nsSecureBrowserUIImpl::
ConfirmLeavingSecure()
{
  nsCOMPtr<nsISecurityWarningDialogs> dialogs;
  nsCOMPtr<nsIInterfaceRequestor> ctx;

  if (!GetNSSDialogs(dialogs, ctx)) {
    return false; // Should this allow true for unimplemented?
  }

  bool confirms;
  dialogs->ConfirmLeavingSecure(ctx, &confirms);

  return confirms;
}

bool nsSecureBrowserUIImpl::
ConfirmMixedMode()
{
  nsCOMPtr<nsISecurityWarningDialogs> dialogs;
  nsCOMPtr<nsIInterfaceRequestor> ctx;

  if (!GetNSSDialogs(dialogs, ctx)) {
    return false; // Should this allow true for unimplemented?
  }

  bool confirms;
  dialogs->ConfirmMixedMode(ctx, &confirms);

  return confirms;
}

/**
 * ConfirmPostToInsecure - returns true if
 *   the user approves the submit (or doesn't care).
 *   returns false on errors.
 */
bool nsSecureBrowserUIImpl::
ConfirmPostToInsecure()
{
  nsCOMPtr<nsISecurityWarningDialogs> dialogs;
  nsCOMPtr<nsIInterfaceRequestor> ctx;

  if (!GetNSSDialogs(dialogs, ctx)) {
    return false; // Should this allow true for unimplemented?
  }

  bool result;

  nsresult rv = dialogs->ConfirmPostToInsecure(ctx, &result);
  if (NS_FAILED(rv)) return false;

  return result;
}

/**
 * ConfirmPostToInsecureFromSecure - returns true if
 *   the user approves the submit (or doesn't care).
 *   returns false on errors.
 */
bool nsSecureBrowserUIImpl::
ConfirmPostToInsecureFromSecure()
{
  nsCOMPtr<nsISecurityWarningDialogs> dialogs;
  nsCOMPtr<nsIInterfaceRequestor> ctx;

  if (!GetNSSDialogs(dialogs, ctx)) {
    return false; // Should this allow true for unimplemented?
  }

  bool result;

  nsresult rv = dialogs->ConfirmPostToInsecureFromSecure(ctx, &result);
  if (NS_FAILED(rv)) return false;

  return result;
}
