/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=80: */
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
 *   Travis Bogard <travis@netscape.com>
 *   Pierre Phaneuf <pp@ludusdesign.com>
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

#include "nsLocation.h"
#include "nsIScriptSecurityManager.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsIScriptContext.h"
#include "nsIDocShell.h"
#include "nsIDocShellLoadInfo.h"
#include "nsIWebNavigation.h"
#include "nsCDefaultURIFixup.h"
#include "nsIURIFixup.h"
#include "nsIURL.h"
#include "nsIJARURI.h"
#include "nsIIOService.h"
#include "nsIServiceManager.h"
#include "nsNetUtil.h"
#include "plstr.h"
#include "prprf.h"
#include "prmem.h"
#include "nsCOMPtr.h"
#include "nsEscape.h"
#include "nsIDOMWindow.h"
#include "nsIDOMDocument.h"
#include "nsIDocument.h"
#include "nsIPresShell.h"
#include "nsPresContext.h"
#include "nsIJSContextStack.h"
#include "nsXPIDLString.h"
#include "nsDOMError.h"
#include "nsDOMClassInfoID.h"
#include "nsCRT.h"
#include "nsIProtocolHandler.h"
#include "nsReadableUtils.h"
#include "nsITextToSubURI.h"
#include "nsContentUtils.h"
#include "nsJSUtils.h"
#include "jsfriendapi.h"
#include "nsContentUtils.h"

static nsresult
GetDocumentCharacterSetForURI(const nsAString& aHref, nsACString& aCharset)
{
  aCharset.Truncate();

  nsresult rv;

  nsCOMPtr<nsIJSContextStack> stack(do_GetService("@mozilla.org/js/xpc/ContextStack;1", &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  JSContext *cx = nsContentUtils::GetCurrentJSContext();
  if (cx) {
    nsCOMPtr<nsIDOMWindow> window =
      do_QueryInterface(nsJSUtils::GetDynamicScriptGlobal(cx));
    NS_ENSURE_TRUE(window, NS_ERROR_FAILURE);

    nsCOMPtr<nsIDOMDocument> domDoc;
    rv = window->GetDocument(getter_AddRefs(domDoc));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIDocument> doc(do_QueryInterface(domDoc));

    if (doc) {
      aCharset = doc->GetDocumentCharacterSet();
    }
  }

  return NS_OK;
}

nsLocation::nsLocation(nsIDocShell *aDocShell)
{
  mDocShell = do_GetWeakReference(aDocShell);
  nsCOMPtr<nsIDOMWindow> outer = do_GetInterface(aDocShell);
  mOuter = do_GetWeakReference(outer);
}

nsLocation::~nsLocation()
{
}

DOMCI_DATA(Location, nsLocation)

// QueryInterface implementation for nsLocation
NS_INTERFACE_MAP_BEGIN(nsLocation)
  NS_INTERFACE_MAP_ENTRY(nsIDOMLocation)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMLocation)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(Location)
NS_INTERFACE_MAP_END


NS_IMPL_ADDREF(nsLocation)
NS_IMPL_RELEASE(nsLocation)

void
nsLocation::SetDocShell(nsIDocShell *aDocShell)
{
   mDocShell = do_GetWeakReference(aDocShell);
}

nsIDocShell *
nsLocation::GetDocShell()
{
  nsCOMPtr<nsIDocShell> docshell(do_QueryReferent(mDocShell));
  return docshell;
}

// Try to get the the document corresponding to the given JSStackFrame.
static already_AddRefed<nsIDocument>
GetFrameDocument(JSContext *cx, JSStackFrame *fp)
{
  if (!cx || !fp)
    return nsnull;

  JSObject* scope = JS_GetGlobalForFrame(fp);
  if (!scope)
    return nsnull;

  JSAutoEnterCompartment ac;
  if (!ac.enter(cx, scope))
     return nsnull;

  nsCOMPtr<nsIDOMWindow> window =
    do_QueryInterface(nsJSUtils::GetStaticScriptGlobal(cx, scope));
  if (!window)
    return nsnull;

  // If it's a window, get its document.
  nsCOMPtr<nsIDOMDocument> domDoc;
  window->GetDocument(getter_AddRefs(domDoc));
  nsCOMPtr<nsIDocument> doc = do_QueryInterface(domDoc);
  return doc.forget();
}

nsresult
nsLocation::CheckURL(nsIURI* aURI, nsIDocShellLoadInfo** aLoadInfo)
{
  *aLoadInfo = nsnull;

  nsCOMPtr<nsIDocShell> docShell(do_QueryReferent(mDocShell));
  NS_ENSURE_TRUE(docShell, NS_ERROR_NOT_AVAILABLE);

  nsCOMPtr<nsISupports> owner;
  nsCOMPtr<nsIURI> sourceURI;

  if (JSContext *cx = nsContentUtils::GetCurrentJSContext()) {
    // No cx means that there's no JS running, or at least no JS that
    // was run through code that properly pushed a context onto the
    // context stack (as all code that runs JS off of web pages
    // does). We won't bother with security checks in this case, but
    // we need to create the loadinfo etc.

    // Get security manager.
    nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
    NS_ENSURE_STATE(ssm);

    // Check to see if URI is allowed.
    nsresult rv = ssm->CheckLoadURIFromScript(cx, aURI);
    NS_ENSURE_SUCCESS(rv, rv);

    // Now get the principal to use when loading the URI
    // First, get the principal and frame.
    JSStackFrame *fp;
    nsIPrincipal* principal = ssm->GetCxSubjectPrincipalAndFrame(cx, &fp);
    NS_ENSURE_TRUE(principal, NS_ERROR_FAILURE);

    nsCOMPtr<nsIURI> principalURI;
    principal->GetURI(getter_AddRefs(principalURI));

    // Make the load's referrer reflect changes to the document's URI caused by
    // push/replaceState, if possible.  First, get the document corresponding to
    // fp.  If the document's original URI (i.e. its URI before
    // push/replaceState) matches the principal's URI, use the document's
    // current URI as the referrer.  If they don't match, use the principal's
    // URI.

    nsCOMPtr<nsIDocument> frameDoc = GetFrameDocument(cx, fp);
    nsCOMPtr<nsIURI> docOriginalURI, docCurrentURI;
    if (frameDoc) {
      docOriginalURI = frameDoc->GetOriginalURI();
      docCurrentURI = frameDoc->GetDocumentURI();
    }

    bool urisEqual = false;
    if (docOriginalURI && docCurrentURI && principalURI) {
      principalURI->Equals(docOriginalURI, &urisEqual);
    }

    if (urisEqual) {
      sourceURI = docCurrentURI;
    }
    else {
      sourceURI = principalURI;
    }

    owner = do_QueryInterface(nsContentUtils::GetSubjectPrincipal());
  }

  // Create load info
  nsCOMPtr<nsIDocShellLoadInfo> loadInfo;
  docShell->CreateLoadInfo(getter_AddRefs(loadInfo));
  NS_ENSURE_TRUE(loadInfo, NS_ERROR_FAILURE);

  loadInfo->SetOwner(owner);

  if (sourceURI) {
    loadInfo->SetReferrer(sourceURI);
  }

  loadInfo.swap(*aLoadInfo);

  return NS_OK;
}

nsresult
nsLocation::GetURI(nsIURI** aURI, bool aGetInnermostURI)
{
  *aURI = nsnull;

  nsresult rv;
  nsCOMPtr<nsIDocShell> docShell(do_QueryReferent(mDocShell));
  nsCOMPtr<nsIWebNavigation> webNav(do_QueryInterface(docShell, &rv));
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCOMPtr<nsIURI> uri;
  rv = webNav->GetCurrentURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);

  // It is valid for docshell to return a null URI. Don't try to fixup
  // if this happens.
  if (!uri) {
    return NS_OK;
  }

  if (aGetInnermostURI) {
    nsCOMPtr<nsIJARURI> jarURI(do_QueryInterface(uri));
    while (jarURI) {
      jarURI->GetJARFile(getter_AddRefs(uri));
      jarURI = do_QueryInterface(uri);
    }
  }

  NS_ASSERTION(uri, "nsJARURI screwed up?");

  nsCOMPtr<nsIURIFixup> urifixup(do_GetService(NS_URIFIXUP_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  return urifixup->CreateExposableURI(uri, aURI);
}

nsresult
nsLocation::GetWritableURI(nsIURI** aURI)
{
  *aURI = nsnull;

  nsCOMPtr<nsIURI> uri;

  nsresult rv = GetURI(getter_AddRefs(uri));
  if (NS_FAILED(rv) || !uri) {
    return rv;
  }

  return uri->Clone(aURI);
}

nsresult
nsLocation::SetURI(nsIURI* aURI, bool aReplace)
{
  nsCOMPtr<nsIDocShell> docShell(do_QueryReferent(mDocShell));
  if (docShell) {
    nsCOMPtr<nsIDocShellLoadInfo> loadInfo;
    nsCOMPtr<nsIWebNavigation> webNav(do_QueryInterface(docShell));

    if(NS_FAILED(CheckURL(aURI, getter_AddRefs(loadInfo))))
      return NS_ERROR_FAILURE;

    if (aReplace) {
      loadInfo->SetLoadType(nsIDocShellLoadInfo::loadStopContentAndReplace);
    } else {
      loadInfo->SetLoadType(nsIDocShellLoadInfo::loadStopContent);
    }

    return docShell->LoadURI(aURI, loadInfo,
                             nsIWebNavigation::LOAD_FLAGS_NONE, true);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsLocation::GetHash(nsAString& aHash)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  aHash.SetLength(0);

  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetURI(getter_AddRefs(uri));
  if (NS_FAILED(rv) || !uri) {
    return rv;
  }

  nsCAutoString ref;
  nsAutoString unicodeRef;

  rv = uri->GetRef(ref);
  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<nsITextToSubURI> textToSubURI(
        do_GetService(NS_ITEXTTOSUBURI_CONTRACTID, &rv));

    if (NS_SUCCEEDED(rv)) {
      nsCAutoString charset;
      uri->GetOriginCharset(charset);
        
      rv = textToSubURI->UnEscapeURIForUI(charset, ref, unicodeRef);
    }
      
    if (NS_FAILED(rv)) {
      // Oh, well.  No intl here!
      NS_UnescapeURL(ref);
      CopyASCIItoUTF16(ref, unicodeRef);
      rv = NS_OK;
    }
  }

  if (NS_SUCCEEDED(rv) && !unicodeRef.IsEmpty()) {
    aHash.Assign(PRUnichar('#'));
    aHash.Append(unicodeRef);
  }

  if (aHash == mCachedHash) {
    // Work around ShareThis stupidly polling location.hash every
    // 5ms all the time by handing out the same exact string buffer
    // we handed out last time.
    aHash = mCachedHash;
  } else {
    mCachedHash = aHash;
  }

  return rv;
}

NS_IMETHODIMP
nsLocation::SetHash(const nsAString& aHash)
{
  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetWritableURI(getter_AddRefs(uri));
  if (NS_FAILED(rv) || !uri) {
    return rv;
  }

  NS_ConvertUTF16toUTF8 hash(aHash);
  if (hash.IsEmpty() || hash.First() != PRUnichar('#')) {
    hash.Insert(PRUnichar('#'), 0);
  }
  rv = uri->SetRef(hash);
  if (NS_SUCCEEDED(rv)) {
    SetURI(uri);
  }

  return rv;
}

NS_IMETHODIMP
nsLocation::GetHost(nsAString& aHost)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  aHost.Truncate();

  nsCOMPtr<nsIURI> uri;
  nsresult result;

  result = GetURI(getter_AddRefs(uri), true);

  if (uri) {
    nsCAutoString hostport;

    result = uri->GetHostPort(hostport);

    if (NS_SUCCEEDED(result)) {
      AppendUTF8toUTF16(hostport, aHost);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsLocation::SetHost(const nsAString& aHost)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetWritableURI(getter_AddRefs(uri));

  if (uri) {
    rv = uri->SetHostPort(NS_ConvertUTF16toUTF8(aHost));
    if (NS_SUCCEEDED(rv)) {
      SetURI(uri);
    }
  }

  return rv;
}

NS_IMETHODIMP
nsLocation::GetHostname(nsAString& aHostname)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  aHostname.Truncate();

  nsCOMPtr<nsIURI> uri;
  nsresult result;

  result = GetURI(getter_AddRefs(uri), true);

  if (uri) {
    nsCAutoString host;

    result = uri->GetHost(host);

    if (NS_SUCCEEDED(result)) {
      AppendUTF8toUTF16(host, aHostname);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsLocation::SetHostname(const nsAString& aHostname)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetWritableURI(getter_AddRefs(uri));

  if (uri) {
    rv = uri->SetHost(NS_ConvertUTF16toUTF8(aHostname));
    if (NS_SUCCEEDED(rv)) {
      SetURI(uri);
    }
  }

  return rv;
}

NS_IMETHODIMP
nsLocation::GetHref(nsAString& aHref)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  aHref.Truncate();

  nsCOMPtr<nsIURI> uri;
  nsresult result;

  result = GetURI(getter_AddRefs(uri));

  if (uri) {
    nsCAutoString uriString;

    result = uri->GetSpec(uriString);

    if (NS_SUCCEEDED(result)) {
      AppendUTF8toUTF16(uriString, aHref);
    }
  }

  return result;
}

NS_IMETHODIMP
nsLocation::SetHref(const nsAString& aHref)
{
  nsAutoString oldHref;
  nsresult rv = NS_OK;

  JSContext *cx = nsContentUtils::GetCurrentJSContext();

  if (cx) {
    rv = SetHrefWithContext(cx, aHref, false);
  } else {
    rv = GetHref(oldHref);

    if (NS_SUCCEEDED(rv)) {
      nsCOMPtr<nsIURI> oldUri;

      rv = NS_NewURI(getter_AddRefs(oldUri), oldHref);

      if (oldUri) {
        rv = SetHrefWithBase(aHref, oldUri, false);
      }
    }
  }

  return rv;
}

nsresult
nsLocation::SetHrefWithContext(JSContext* cx, const nsAString& aHref,
                               bool aReplace)
{
  nsCOMPtr<nsIURI> base;

  // Get the source of the caller
  nsresult result = GetSourceBaseURL(cx, getter_AddRefs(base));

  if (NS_FAILED(result)) {
    return result;
  }

  return SetHrefWithBase(aHref, base, aReplace);
}

nsresult
nsLocation::SetHrefWithBase(const nsAString& aHref, nsIURI* aBase,
                            bool aReplace)
{
  nsresult result;
  nsCOMPtr<nsIURI> newUri;

  nsCOMPtr<nsIDocShell> docShell(do_QueryReferent(mDocShell));

  nsCAutoString docCharset;
  if (NS_SUCCEEDED(GetDocumentCharacterSetForURI(aHref, docCharset)))
    result = NS_NewURI(getter_AddRefs(newUri), aHref, docCharset.get(), aBase);
  else
    result = NS_NewURI(getter_AddRefs(newUri), aHref, nsnull, aBase);

  if (newUri) {
    /* Check with the scriptContext if it is currently processing a script tag.
     * If so, this must be a <script> tag with a location.href in it.
     * we want to do a replace load, in such a situation. 
     * In other cases, for example if a event handler or a JS timer
     * had a location.href in it, we want to do a normal load,
     * so that the new url will be appended to Session History.
     * This solution is tricky. Hopefully it isn't going to bite
     * anywhere else. This is part of solution for bug # 39938, 72197
     * 
     */
    bool inScriptTag=false;
    JSContext *cx = nsContentUtils::GetCurrentJSContext();
    if (cx) {
      nsIScriptContext *scriptContext =
        nsJSUtils::GetDynamicScriptContext(cx);

      if (scriptContext) {
        if (scriptContext->GetProcessingScriptTag()) {
          // Now check to make sure that the script is running in our window,
          // since we only want to replace if the location is set by a
          // <script> tag in the same window.  See bug 178729.
          nsCOMPtr<nsIScriptGlobalObject> ourGlobal(do_GetInterface(docShell));
          inScriptTag = (ourGlobal == scriptContext->GetGlobalObject());
        }
      }
    } //cx

    return SetURI(newUri, aReplace || inScriptTag);
  }

  return result;
}

NS_IMETHODIMP
nsLocation::GetPathname(nsAString& aPathname)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  aPathname.Truncate();

  nsCOMPtr<nsIURI> uri;
  nsresult result = NS_OK;

  result = GetURI(getter_AddRefs(uri));

  nsCOMPtr<nsIURL> url(do_QueryInterface(uri));
  if (url) {
    nsCAutoString file;

    result = url->GetFilePath(file);

    if (NS_SUCCEEDED(result)) {
      AppendUTF8toUTF16(file, aPathname);
    }
  }

  return result;
}

NS_IMETHODIMP
nsLocation::SetPathname(const nsAString& aPathname)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetWritableURI(getter_AddRefs(uri));

  if (uri) {
    rv = uri->SetPath(NS_ConvertUTF16toUTF8(aPathname));
    if (NS_SUCCEEDED(rv)) {
      SetURI(uri);
    }
  }

  return rv;
}

NS_IMETHODIMP
nsLocation::GetPort(nsAString& aPort)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  aPort.SetLength(0);

  nsCOMPtr<nsIURI> uri;
  nsresult result = NS_OK;

  result = GetURI(getter_AddRefs(uri), true);

  if (uri) {
    PRInt32 port;
    result = uri->GetPort(&port);

    if (NS_SUCCEEDED(result) && -1 != port) {
      nsAutoString portStr;
      portStr.AppendInt(port);
      aPort.Append(portStr);
    }

    // Don't propagate this exception to caller
    result = NS_OK;
  }

  return result;
}

NS_IMETHODIMP
nsLocation::SetPort(const nsAString& aPort)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetWritableURI(getter_AddRefs(uri));

  if (uri) {
    // perhaps use nsReadingIterators at some point?
    NS_ConvertUTF16toUTF8 portStr(aPort);
    const char *buf = portStr.get();
    PRInt32 port = -1;

    if (buf) {
      if (*buf == ':') {
        port = atol(buf+1);
      }
      else {
        port = atol(buf);
      }
    }

    rv = uri->SetPort(port);
    if (NS_SUCCEEDED(rv)) {
      SetURI(uri);
    }
  }

  return rv;
}

NS_IMETHODIMP
nsLocation::GetProtocol(nsAString& aProtocol)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  aProtocol.SetLength(0);

  nsCOMPtr<nsIURI> uri;
  nsresult result = NS_OK;

  result = GetURI(getter_AddRefs(uri));

  if (uri) {
    nsCAutoString protocol;

    result = uri->GetScheme(protocol);

    if (NS_SUCCEEDED(result)) {
      CopyASCIItoUTF16(protocol, aProtocol);
      aProtocol.Append(PRUnichar(':'));
    }
  }

  return result;
}

NS_IMETHODIMP
nsLocation::SetProtocol(const nsAString& aProtocol)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetWritableURI(getter_AddRefs(uri));

  if (uri) {
    rv = uri->SetScheme(NS_ConvertUTF16toUTF8(aProtocol));
    if (NS_SUCCEEDED(rv)) {
      SetURI(uri);
    }
  }

  return rv;
}

NS_IMETHODIMP
nsLocation::GetSearch(nsAString& aSearch)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  aSearch.SetLength(0);

  nsCOMPtr<nsIURI> uri;
  nsresult result = NS_OK;

  result = GetURI(getter_AddRefs(uri));

  nsCOMPtr<nsIURL> url(do_QueryInterface(uri));

  if (url) {
    nsCAutoString search;

    result = url->GetQuery(search);

    if (NS_SUCCEEDED(result) && !search.IsEmpty()) {
      aSearch.Assign(PRUnichar('?'));
      AppendUTF8toUTF16(search, aSearch);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsLocation::SetSearch(const nsAString& aSearch)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetWritableURI(getter_AddRefs(uri));

  nsCOMPtr<nsIURL> url(do_QueryInterface(uri));
  if (url) {
    rv = url->SetQuery(NS_ConvertUTF16toUTF8(aSearch));
    if (NS_SUCCEEDED(rv)) {
      SetURI(uri);
    }
  }

  return rv;
}

NS_IMETHODIMP
nsLocation::Reload(bool aForceget)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  nsresult rv;
  nsCOMPtr<nsIDocShell> docShell(do_QueryReferent(mDocShell));
  nsCOMPtr<nsIWebNavigation> webNav(do_QueryInterface(docShell));
  nsCOMPtr<nsPIDOMWindow> window(do_GetInterface(docShell));

  if (window && window->IsHandlingResizeEvent()) {
    // location.reload() was called on a window that is handling a
    // resize event. Sites do this since Netscape 4.x needed it, but
    // we don't, and it's a horrible experience for nothing. In stead
    // of reloading the page, just clear style data and reflow the
    // page since some sites may use this trick to work around gecko
    // reflow bugs, and this should have the same effect.

    nsCOMPtr<nsIDocument> doc(do_QueryInterface(window->GetExtantDocument()));

    nsIPresShell *shell;
    nsPresContext *pcx;
    if (doc && (shell = doc->GetShell()) && (pcx = shell->GetPresContext())) {
      pcx->RebuildAllStyleData(NS_STYLE_HINT_REFLOW);
    }

    return NS_OK;
  }

  if (webNav) {
    PRUint32 reloadFlags = nsIWebNavigation::LOAD_FLAGS_NONE;

    if (aForceget) {
      reloadFlags = nsIWebNavigation::LOAD_FLAGS_BYPASS_CACHE | 
                    nsIWebNavigation::LOAD_FLAGS_BYPASS_PROXY;
    }
    rv = webNav->Reload(reloadFlags);
    if (rv == NS_BINDING_ABORTED) {
      // This happens when we attempt to reload a POST result and the user says
      // no at the "do you want to reload?" prompt.  Don't propagate this one
      // back to callers.
      rv = NS_OK;
    }
  } else {
    rv = NS_ERROR_FAILURE;
  }

  return rv;
}

NS_IMETHODIMP
nsLocation::Replace(const nsAString& aUrl)
{
  nsresult rv = NS_OK;
  if (JSContext *cx = nsContentUtils::GetCurrentJSContext()) {
    return SetHrefWithContext(cx, aUrl, true);
  }

  nsAutoString oldHref;

  rv = GetHref(oldHref);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> oldUri;

  rv = NS_NewURI(getter_AddRefs(oldUri), oldHref);
  NS_ENSURE_SUCCESS(rv, rv);

  return SetHrefWithBase(aUrl, oldUri, true);
}

NS_IMETHODIMP
nsLocation::Assign(const nsAString& aUrl)
{
  if (!CallerSubsumes())
    return NS_ERROR_DOM_SECURITY_ERR;

  nsAutoString oldHref;
  nsresult result = NS_OK;

  result = GetHref(oldHref);

  if (NS_SUCCEEDED(result)) {
    nsCOMPtr<nsIURI> oldUri;

    result = NS_NewURI(getter_AddRefs(oldUri), oldHref);

    if (oldUri) {
      result = SetHrefWithBase(aUrl, oldUri, false);
    }
  }

  return result;
}

NS_IMETHODIMP
nsLocation::ToString(nsAString& aReturn)
{
  // NB: GetHref checks CallerSubsumes().
  return GetHref(aReturn);
}

nsresult
nsLocation::GetSourceBaseURL(JSContext* cx, nsIURI** sourceURL)
{
  *sourceURL = nsnull;
  nsCOMPtr<nsIScriptGlobalObject> sgo = nsJSUtils::GetDynamicScriptGlobal(cx);
  // If this JS context doesn't have an associated DOM window, we effectively
  // have no script entry point stack. This doesn't generally happen with the DOM,
  // but can sometimes happen with extension code in certain IPC configurations.
  // If this happens, try falling back on the current document associated with
  // the docshell. If that fails, just return null and hope that the caller passed
  // an absolute URI.
  if (!sgo && GetDocShell()) {
    sgo = do_GetInterface(GetDocShell());
  }
  NS_ENSURE_TRUE(sgo, NS_OK);
  nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(sgo);
  NS_ENSURE_TRUE(window, NS_ERROR_UNEXPECTED);
  nsCOMPtr<nsIDOMDocument> document;
  nsresult rv = window->GetDocument(getter_AddRefs(document));
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIDocument> doc = do_QueryInterface(document);
  NS_ENSURE_TRUE(doc, NS_OK);
  *sourceURL = doc->GetBaseURI().get();
  return NS_OK;
}

bool
nsLocation::CallerSubsumes()
{
  // Get the principal associated with the location object.
  nsCOMPtr<nsIDOMWindow> outer = do_QueryReferent(mOuter);
  if (NS_UNLIKELY(!outer))
    return false;
  nsCOMPtr<nsIScriptObjectPrincipal> sop = do_QueryInterface(outer);
  bool subsumes = false;
  nsresult rv = nsContentUtils::GetSubjectPrincipal()->Subsumes(sop->GetPrincipal(), &subsumes);
  NS_ENSURE_SUCCESS(rv, false);
  return subsumes || nsContentUtils::IsCallerTrustedForCapability("UniversalXPConnect");
}
