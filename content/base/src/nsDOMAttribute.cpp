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
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ms2ger <ms2ger@gmail.com>
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

/*
 * Implementation of DOM Core's nsIDOMAttr node.
 */

#include "nsDOMAttribute.h"
#include "nsGenericElement.h"
#include "mozilla/dom/Element.h"
#include "nsContentCreatorFunctions.h"
#include "nsINameSpaceManager.h"
#include "nsDOMError.h"
#include "nsUnicharUtils.h"
#include "nsDOMString.h"
#include "nsIDocument.h"
#include "nsIDOMDocument.h"
#include "nsIDOMUserDataHandler.h"
#include "nsEventDispatcher.h"
#include "nsGkAtoms.h"
#include "nsCOMArray.h"
#include "nsNodeUtils.h"
#include "nsEventListenerManager.h"
#include "nsTextNode.h"
#include "mozAutoDocUpdate.h"
#include "nsMutationEvent.h"
#include "nsPLDOMEvent.h"
#include "nsWrapperCacheInlines.h"

using namespace mozilla::dom;

//----------------------------------------------------------------------
bool nsDOMAttribute::sInitialized;

nsDOMAttribute::nsDOMAttribute(nsDOMAttributeMap *aAttrMap,
                               already_AddRefed<nsINodeInfo> aNodeInfo,
                               const nsAString   &aValue, bool aNsAware)
  : nsIAttribute(aAttrMap, aNodeInfo, aNsAware), mValue(aValue), mChild(nsnull)
{
  NS_ABORT_IF_FALSE(mNodeInfo, "We must get a nodeinfo here!");
  NS_ABORT_IF_FALSE(mNodeInfo->NodeType() == nsIDOMNode::ATTRIBUTE_NODE,
                    "Wrong nodeType");

  // We don't add a reference to our content. It will tell us
  // to drop our reference when it goes away.

  EnsureChildState();

  nsIContent* content = GetContentInternal();
  if (content) {
    content->AddMutationObserver(this);
  }
}

nsDOMAttribute::~nsDOMAttribute()
{
  if (mChild) {
    static_cast<nsTextNode*>(mChild)->UnbindFromAttribute();
    NS_RELEASE(mChild);
    mFirstChild = nsnull;
  }

  nsIContent* content = GetContentInternal();
  if (content) {
    content->RemoveMutationObserver(this);
  }
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsDOMAttribute)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsDOMAttribute)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS

  if (!nsINode::Traverse(tmp, cb)) {
    return NS_SUCCESS_INTERRUPTED_TRAVERSE;
  }

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_RAWPTR(mChild)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(nsDOMAttribute)
  nsINode::Trace(tmp, aCallback, aClosure);
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsDOMAttribute)
  nsINode::Unlink(tmp);
  if (tmp->mChild) {
    static_cast<nsTextNode*>(tmp->mChild)->UnbindFromAttribute();
    NS_RELEASE(tmp->mChild);
    tmp->mFirstChild = nsnull;
  }
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

DOMCI_NODE_DATA(Attr, nsDOMAttribute)

// QueryInterface implementation for nsDOMAttribute
NS_INTERFACE_TABLE_HEAD(nsDOMAttribute)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_NODE_INTERFACE_TABLE5(nsDOMAttribute, nsIDOMAttr, nsIAttribute, nsIDOMNode,
                           nsIDOMEventTarget, nsIMutationObserver)
  NS_INTERFACE_MAP_ENTRIES_CYCLE_COLLECTION(nsDOMAttribute)
  NS_INTERFACE_MAP_ENTRY_TEAROFF(nsISupportsWeakReference,
                                 new nsNodeSupportsWeakRefTearoff(this))
  NS_INTERFACE_MAP_ENTRY_TEAROFF(nsIDOMXPathNSResolver,
                                 new nsNode3Tearoff(this))
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(Attr)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsDOMAttribute)
NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_DESTROY(nsDOMAttribute,
                                              nsNodeUtils::LastRelease(this))

void
nsDOMAttribute::SetMap(nsDOMAttributeMap *aMap)
{
  if (mAttrMap && !aMap && sInitialized) {
    // We're breaking a relationship with content and not getting a new one,
    // need to locally cache value. GetValue() does that.
    GetValue(mValue);
  }

  nsIContent* content = GetContentInternal();
  if (content) {
    content->RemoveMutationObserver(this);
  }

  mAttrMap = aMap;

  // If we have a new content, we sholud start listening to it.
  content = GetContentInternal();
  if (content) {
    content->AddMutationObserver(this);
  }
}

nsIContent*
nsDOMAttribute::GetContent() const
{
  return GetContentInternal();
}

nsresult
nsDOMAttribute::SetOwnerDocument(nsIDocument* aDocument)
{
  NS_ASSERTION(aDocument, "Missing document");

  nsIDocument *doc = OwnerDoc();
  NS_ASSERTION(doc != aDocument, "bad call to nsDOMAttribute::SetOwnerDocument");
  doc->DeleteAllPropertiesFor(this);

  nsCOMPtr<nsINodeInfo> newNodeInfo;
  newNodeInfo = aDocument->NodeInfoManager()->
    GetNodeInfo(mNodeInfo->NameAtom(), mNodeInfo->GetPrefixAtom(),
                mNodeInfo->NamespaceID(),
                nsIDOMNode::ATTRIBUTE_NODE);
  NS_ENSURE_TRUE(newNodeInfo, NS_ERROR_OUT_OF_MEMORY);
  NS_ASSERTION(newNodeInfo, "GetNodeInfo lies");
  mNodeInfo.swap(newNodeInfo);

  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::GetName(nsAString& aName)
{
  aName = NodeName();
  return NS_OK;
}

already_AddRefed<nsIAtom>
nsDOMAttribute::GetNameAtom(nsIContent* aContent)
{
  nsIAtom* result = nsnull;
  if (!mNsAware &&
      mNodeInfo->NamespaceID() == kNameSpaceID_None &&
      aContent->IsInHTMLDocument() &&
      aContent->IsHTML()) {
    nsAutoString name;
    mNodeInfo->NameAtom()->ToString(name);
    nsAutoString lower;
    ToLowerCase(name, lower);
    nsCOMPtr<nsIAtom> nameAtom = do_GetAtom(lower);
    nameAtom.swap(result);
  } else {
    nsCOMPtr<nsIAtom> nameAtom = mNodeInfo->NameAtom();
    nameAtom.swap(result);
  }
  return result;
}

NS_IMETHODIMP
nsDOMAttribute::GetValue(nsAString& aValue)
{
  nsIContent* content = GetContentInternal();
  if (content) {
    nsCOMPtr<nsIAtom> nameAtom = GetNameAtom(content);
    content->GetAttr(mNodeInfo->NamespaceID(), nameAtom, aValue);
  }
  else {
    aValue = mValue;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::SetValue(const nsAString& aValue)
{
  nsresult rv = NS_OK;
  nsIContent* content = GetContentInternal();
  if (content) {
    nsCOMPtr<nsIAtom> nameAtom = GetNameAtom(content);
    rv = content->SetAttr(mNodeInfo->NamespaceID(),
                          nameAtom,
                          mNodeInfo->GetPrefixAtom(),
                          aValue,
                          true);
  }
  else {
    mValue = aValue;

    if (mChild) {
      if (mValue.IsEmpty()) {
        doRemoveChild(true);
      } else {
        mChild->SetText(mValue, false);
      }
    } else {
      EnsureChildState();
    }
  }

  return rv;
}


NS_IMETHODIMP
nsDOMAttribute::GetSpecified(bool* aSpecified)
{
  NS_ENSURE_ARG_POINTER(aSpecified);
  OwnerDoc()->WarnOnceAbout(nsIDocument::eSpecified);

  *aSpecified = true;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::GetOwnerElement(nsIDOMElement** aOwnerElement)
{
  NS_ENSURE_ARG_POINTER(aOwnerElement);
  OwnerDoc()->WarnOnceAbout(nsIDocument::eOwnerElement);

  nsIContent* content = GetContentInternal();
  if (content) {
    return CallQueryInterface(content, aOwnerElement);
  }

  *aOwnerElement = nsnull;

  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::GetNodeName(nsAString& aNodeName)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eNodeName);

  return GetName(aNodeName);
}

NS_IMETHODIMP
nsDOMAttribute::GetNodeValue(nsAString& aNodeValue)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eNodeValue);

  return GetValue(aNodeValue);
}

NS_IMETHODIMP
nsDOMAttribute::SetNodeValue(const nsAString& aNodeValue)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eNodeValue);

  return SetValue(aNodeValue);
}

NS_IMETHODIMP
nsDOMAttribute::GetNodeType(PRUint16* aNodeType)
{
  NS_ENSURE_ARG_POINTER(aNodeType);
  OwnerDoc()->WarnOnceAbout(nsIDocument::eNodeType);

  *aNodeType = (PRUint16)nsIDOMNode::ATTRIBUTE_NODE;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::GetParentNode(nsIDOMNode** aParentNode)
{
  NS_ENSURE_ARG_POINTER(aParentNode);
  OwnerDoc()->WarnOnceAbout(nsIDocument::eParentNode);

  *aParentNode = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::GetParentElement(nsIDOMElement** aParentElement)
{
  *aParentElement = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::GetChildNodes(nsIDOMNodeList** aChildNodes)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eChildNodes);

  return nsINode::GetChildNodes(aChildNodes);
}

NS_IMETHODIMP
nsDOMAttribute::HasChildNodes(bool* aHasChildNodes)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eHasChildNodes);

  *aHasChildNodes = mFirstChild != nsnull;

  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::HasAttributes(bool* aHasAttributes)
{
  NS_ENSURE_ARG_POINTER(aHasAttributes);
  OwnerDoc()->WarnOnceAbout(nsIDocument::eHasAttributes);

  *aHasAttributes = false;

  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::GetFirstChild(nsIDOMNode** aFirstChild)
{
  *aFirstChild = nsnull;

  OwnerDoc()->WarnOnceAbout(nsIDocument::eFirstChild);

  if (mFirstChild) {
    CallQueryInterface(mFirstChild, aFirstChild);
  }
  
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::GetLastChild(nsIDOMNode** aLastChild)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eLastChild);

  return GetFirstChild(aLastChild);
}

NS_IMETHODIMP
nsDOMAttribute::GetPreviousSibling(nsIDOMNode** aPreviousSibling)
{
  NS_ENSURE_ARG_POINTER(aPreviousSibling);

  OwnerDoc()->WarnOnceAbout(nsIDocument::ePreviousSibling);

  *aPreviousSibling = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::GetNextSibling(nsIDOMNode** aNextSibling)
{
  NS_ENSURE_ARG_POINTER(aNextSibling);

  OwnerDoc()->WarnOnceAbout(nsIDocument::eNextSibling);

  *aNextSibling = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::GetAttributes(nsIDOMNamedNodeMap** aAttributes)
{
  NS_ENSURE_ARG_POINTER(aAttributes);

  OwnerDoc()->WarnOnceAbout(nsIDocument::eAttributes);

  *aAttributes = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::InsertBefore(nsIDOMNode* aNewChild, nsIDOMNode* aRefChild, nsIDOMNode** aReturn)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eInsertBefore);

  return ReplaceOrInsertBefore(false, aNewChild, aRefChild, aReturn);
}

NS_IMETHODIMP
nsDOMAttribute::ReplaceChild(nsIDOMNode* aNewChild, nsIDOMNode* aOldChild, nsIDOMNode** aReturn)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eReplaceChild);

  return ReplaceOrInsertBefore(true, aNewChild, aOldChild, aReturn);
}

NS_IMETHODIMP
nsDOMAttribute::RemoveChild(nsIDOMNode* aOldChild, nsIDOMNode** aReturn)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eRemoveChild);

  return nsINode::RemoveChild(aOldChild, aReturn);
}

NS_IMETHODIMP
nsDOMAttribute::AppendChild(nsIDOMNode* aNewChild, nsIDOMNode** aReturn)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eAppendChild);

  return InsertBefore(aNewChild, nsnull, aReturn);
}

nsresult
nsDOMAttribute::Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const
{
  nsAutoString value;
  const_cast<nsDOMAttribute*>(this)->GetValue(value);

  nsCOMPtr<nsINodeInfo> ni = aNodeInfo;
  *aResult = new nsDOMAttribute(nsnull, ni.forget(), value, mNsAware);
  if (!*aResult) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  NS_ADDREF(*aResult);

  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::CloneNode(bool aDeep, nsIDOMNode** aResult)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eCloneNode);

  return nsNodeUtils::CloneNodeImpl(this, aDeep, true, aResult);
}

NS_IMETHODIMP
nsDOMAttribute::GetOwnerDocument(nsIDOMDocument** aOwnerDocument)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eOwnerDocument);

  return nsINode::GetOwnerDocument(aOwnerDocument);
}

NS_IMETHODIMP
nsDOMAttribute::GetNamespaceURI(nsAString& aNamespaceURI)
{
  return mNodeInfo->GetNamespaceURI(aNamespaceURI);
}

NS_IMETHODIMP
nsDOMAttribute::GetPrefix(nsAString& aPrefix)
{
  mNodeInfo->GetPrefix(aPrefix);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::GetLocalName(nsAString& aLocalName)
{
  mNodeInfo->GetName(aLocalName);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::Normalize()
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eNormalize);

  // Nothing to do here
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::IsSupported(const nsAString& aFeature,
                            const nsAString& aVersion,
                            bool* aReturn)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eIsSupported);

  return nsGenericElement::InternalIsSupported(static_cast<nsIDOMAttr*>(this), 
                                               aFeature, aVersion, aReturn);
}

already_AddRefed<nsIURI>
nsDOMAttribute::GetBaseURI() const
{
  nsINode *parent = GetContentInternal();

  return parent ? parent->GetBaseURI() : nsnull;
}

NS_IMETHODIMP
nsDOMAttribute::GetDOMBaseURI(nsAString &aURI)
{
  return nsINode::GetDOMBaseURI(aURI);
}

NS_IMETHODIMP
nsDOMAttribute::CompareDocumentPosition(nsIDOMNode *other,
                                        PRUint16 *aResult)
{
  return nsINode::CompareDocumentPosition(other, aResult);
}

NS_IMETHODIMP
nsDOMAttribute::IsEqualNode(nsIDOMNode* aOther, bool* aResult)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eIsEqualNode);

  return nsINode::IsEqualNode(aOther, aResult);
}

NS_IMETHODIMP
nsDOMAttribute::GetTextContent(nsAString &aTextContent)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eTextContent);

  return GetNodeValue(aTextContent);
}

NS_IMETHODIMP
nsDOMAttribute::SetTextContent(const nsAString& aTextContent)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eTextContent);

  return SetNodeValue(aTextContent);
}

NS_IMETHODIMP
nsDOMAttribute::Contains(nsIDOMNode* aOther, bool* aReturn)
{
  return nsINode::Contains(aOther, aReturn);
}

NS_IMETHODIMP
nsDOMAttribute::LookupPrefix(const nsAString & namespaceURI,
                             nsAString & aResult)
{
  SetDOMStringToNull(aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::IsDefaultNamespace(const nsAString & namespaceURI,
                                   bool *aResult)
{
  *aResult = namespaceURI.IsEmpty();
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::LookupNamespaceURI(const nsAString & prefix,
                              nsAString & aResult)
{
  SetDOMStringToNull(aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMAttribute::SetUserData(const nsAString & key,
                            nsIVariant *data, nsIDOMUserDataHandler *handler,
                            nsIVariant **aResult)
{
  return nsINode::SetUserData(key, data, handler, aResult);
}

NS_IMETHODIMP
nsDOMAttribute::GetUserData(const nsAString & key, nsIVariant **aResult)
{
  return nsINode::GetUserData(key, aResult);
}

NS_IMETHODIMP
nsDOMAttribute::GetIsId(bool* aReturn)
{
  nsIContent* content = GetContentInternal();
  if (!content)
  {
    *aReturn = false;
    return NS_OK;
  }

  nsIAtom* idAtom = content->GetIDAttributeName();
  if (!idAtom)
  {
    *aReturn = false;
    return NS_OK;
  }

  *aReturn = mNodeInfo->Equals(idAtom, kNameSpaceID_None);
  return NS_OK;
}

bool
nsDOMAttribute::IsNodeOfType(PRUint32 aFlags) const
{
    return !(aFlags & ~eATTRIBUTE);
}

PRUint32
nsDOMAttribute::GetChildCount() const
{
  return mFirstChild ? 1 : 0;
}

nsIContent *
nsDOMAttribute::GetChildAt(PRUint32 aIndex) const
{
  return aIndex == 0 ? mFirstChild : nsnull;
}

nsIContent * const *
nsDOMAttribute::GetChildArray(PRUint32* aChildCount) const
{
  *aChildCount = GetChildCount();
  return &mFirstChild;
}

PRInt32
nsDOMAttribute::IndexOf(nsINode* aPossibleChild) const
{
  if (!aPossibleChild || aPossibleChild != mFirstChild) {
    return -1;
  }

  return 0;
}

nsresult
nsDOMAttribute::InsertChildAt(nsIContent* aKid, PRUint32 aIndex,
                              bool aNotify)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
nsDOMAttribute::AppendChildTo(nsIContent* aKid, bool aNotify)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
nsDOMAttribute::RemoveChildAt(PRUint32 aIndex, bool aNotify)
{
  if (aIndex != 0 || !mChild) {
    return NS_OK;
  }

  doRemoveChild(aNotify);

  nsString nullString;
  SetDOMStringToNull(nullString);
  SetValue(nullString);
  return NS_OK;
}

nsresult
nsDOMAttribute::PreHandleEvent(nsEventChainPreVisitor& aVisitor)
{
  aVisitor.mCanHandle = true;
  return NS_OK;
}

void
nsDOMAttribute::EnsureChildState()
{
  NS_PRECONDITION(!mChild, "Someone screwed up");

  nsAutoString value;
  GetValue(value);

  if (!value.IsEmpty()) {
    NS_NewTextNode(&mChild, mNodeInfo->NodeInfoManager());

    static_cast<nsTextNode*>(mChild)->BindToAttribute(this);
    mFirstChild = mChild;

    mChild->SetText(value, false);
  }
}

void
nsDOMAttribute::AttributeChanged(nsIDocument* aDocument,
                                 Element* aElement,
                                 PRInt32 aNameSpaceID,
                                 nsIAtom* aAttribute,
                                 PRInt32 aModType)
{
  nsIContent* content = GetContentInternal();
  if (aElement != content) {
    return;
  }

  if (aNameSpaceID != mNodeInfo->NamespaceID()) {
    return;
  }

  nsCOMPtr<nsIAtom> nameAtom = GetNameAtom(content);
  if (nameAtom != aAttribute) {
    return;
  }

  nsCOMPtr<nsIMutationObserver> kungFuDeathGrip(this);
  
  // Just blow away our mChild and recreate it if needed
  if (mChild) {
    doRemoveChild(true);
  }
  EnsureChildState();
}

void
nsDOMAttribute::Initialize()
{
  sInitialized = true;
}

void
nsDOMAttribute::Shutdown()
{
  sInitialized = false;
}

void
nsDOMAttribute::doRemoveChild(bool aNotify)
{
  NS_ASSERTION(mChild && mFirstChild, "Why are we here?");
  NS_ASSERTION(mChild == mFirstChild, "Something got out of sync!");

  nsRefPtr<nsTextNode> child = static_cast<nsTextNode*>(mChild);
  nsMutationGuard::DidMutate();
  mozAutoDocUpdate updateBatch(OwnerDoc(), UPDATE_CONTENT_MODEL, aNotify);

  NS_RELEASE(mChild);
  mFirstChild = nsnull;

  if (aNotify) {
    nsNodeUtils::AttributeChildRemoved(this, child);
  }

  child->UnbindFromAttribute();
}

