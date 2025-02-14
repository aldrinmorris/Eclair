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
 * Portions created by the Initial Developer are Copyright (C) 2002
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *    Boris Zbarsky <bzbarsky@mit.edu>  (original author)
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

#include "nsUnicharStreamLoader.h"
#include "nsIInputStream.h"
#include "nsICharsetConverterManager.h"
#include "nsIServiceManager.h"

#define SNIFFING_BUFFER_SIZE 512 // specified in draft-abarth-mime-sniff-06

NS_IMETHODIMP
nsUnicharStreamLoader::Init(nsIUnicharStreamLoaderObserver *aObserver)
{
  NS_ENSURE_ARG_POINTER(aObserver);

  mObserver = aObserver;

  if (!mRawData.SetCapacity(SNIFFING_BUFFER_SIZE))
    return NS_ERROR_OUT_OF_MEMORY;

  return NS_OK;
}

nsresult
nsUnicharStreamLoader::Create(nsISupports *aOuter,
                              REFNSIID aIID,
                              void **aResult)
{
  if (aOuter) return NS_ERROR_NO_AGGREGATION;

  nsUnicharStreamLoader* it = new nsUnicharStreamLoader();
  NS_ADDREF(it);
  nsresult rv = it->QueryInterface(aIID, aResult);
  NS_RELEASE(it);
  return rv;
}

NS_IMPL_ISUPPORTS3(nsUnicharStreamLoader, nsIUnicharStreamLoader,
                   nsIRequestObserver, nsIStreamListener)

/* readonly attribute nsIChannel channel; */
NS_IMETHODIMP
nsUnicharStreamLoader::GetChannel(nsIChannel **aChannel)
{
  NS_IF_ADDREF(*aChannel = mChannel);
  return NS_OK;
}

/* readonly attribute nsACString charset */
NS_IMETHODIMP
nsUnicharStreamLoader::GetCharset(nsACString& aCharset)
{
  aCharset = mCharset;
  return NS_OK;
}

/* nsIRequestObserver implementation */
NS_IMETHODIMP
nsUnicharStreamLoader::OnStartRequest(nsIRequest*, nsISupports*)
{
  return NS_OK;
}

NS_IMETHODIMP
nsUnicharStreamLoader::OnStopRequest(nsIRequest *aRequest,
                                     nsISupports *aContext,
                                     nsresult aStatus)
{
  if (!mObserver) {
    NS_ERROR("nsUnicharStreamLoader::OnStopRequest called before ::Init");
    return NS_ERROR_UNEXPECTED;
  }

  mContext = aContext;
  mChannel = do_QueryInterface(aRequest);

  nsresult rv = NS_OK;
  if (mRawData.Length() > 0 && NS_SUCCEEDED(aStatus)) {
    NS_ABORT_IF_FALSE(mBuffer.Length() == 0,
                      "should not have both decoded and raw data");
    rv = DetermineCharset();
  }

  if (NS_FAILED(rv)) {
    // Call the observer but pass it no data.
    mObserver->OnStreamComplete(this, mContext, rv, EmptyString());
  } else {
    mObserver->OnStreamComplete(this, mContext, aStatus, mBuffer);
  }

  mObserver = nsnull;
  mDecoder = nsnull;
  mContext = nsnull;
  mChannel = nsnull;
  mCharset.Truncate();
  mBuffer.Truncate();
  return rv;
}

/* nsIStreamListener implementation */
NS_IMETHODIMP
nsUnicharStreamLoader::OnDataAvailable(nsIRequest *aRequest,
                                       nsISupports *aContext,
                                       nsIInputStream *aInputStream,
                                       PRUint32 aSourceOffset,
                                       PRUint32 aCount)
{
  if (!mObserver) {
    NS_ERROR("nsUnicharStreamLoader::OnDataAvailable called before ::Init");
    return NS_ERROR_UNEXPECTED;
  }

  mContext = aContext;
  mChannel = do_QueryInterface(aRequest);

  nsresult rv = NS_OK;
  if (mDecoder) {
    // process everything we've got
    PRUint32 dummy;
    aInputStream->ReadSegments(WriteSegmentFun, this, aCount, &dummy);
  } else {
    // no decoder yet.  Read up to SNIFFING_BUFFER_SIZE octets into
    // mRawData (this is the cutoff specified in
    // draft-abarth-mime-sniff-06).  If we can get that much, then go
    // ahead and fire charset detection and read the rest.  Otherwise
    // wait for more data.

    PRUint32 haveRead = mRawData.Length();
    PRUint32 toRead = NS_MIN(SNIFFING_BUFFER_SIZE - haveRead, aCount);
    PRUint32 n;
    char *here = mRawData.BeginWriting() + haveRead;

    rv = aInputStream->Read(here, toRead, &n);
    if (NS_SUCCEEDED(rv)) {
      mRawData.SetLength(haveRead + n);
      if (mRawData.Length() == SNIFFING_BUFFER_SIZE) {
        rv = DetermineCharset();
        if (NS_SUCCEEDED(rv)) {
          // process what's left
          PRUint32 dummy;
          aInputStream->ReadSegments(WriteSegmentFun, this, aCount - n, &dummy);
        }
      } else {
        NS_ABORT_IF_FALSE(n == aCount, "didn't read as much as was available");
      }
    }
  }

  mContext = nsnull;
  mChannel = nsnull;
  return rv;
}

/* internal */
static NS_DEFINE_CID(kCharsetConverterManagerCID,
                     NS_ICHARSETCONVERTERMANAGER_CID);

nsresult
nsUnicharStreamLoader::DetermineCharset()
{
  nsresult rv = mObserver->OnDetermineCharset(this, mContext,
                                              mRawData, mCharset);
  if (NS_FAILED(rv) || mCharset.IsEmpty()) {
    // The observer told us nothing useful
    mCharset.AssignLiteral("UTF-8");
  }

  // Create the decoder for this character set
  nsCOMPtr<nsICharsetConverterManager> ccm =
    do_GetService(kCharsetConverterManagerCID, &rv);
  if (NS_FAILED(rv)) return rv;

  rv = ccm->GetUnicodeDecoder(mCharset.get(), getter_AddRefs(mDecoder));
  if (NS_FAILED(rv)) return rv;

  // Process the data into mBuffer
  PRUint32 dummy;
  rv = WriteSegmentFun(nsnull, this,
                       mRawData.BeginReading(),
                       0, mRawData.Length(),
                       &dummy);
  mRawData.Truncate();
  return rv;
}

NS_METHOD
nsUnicharStreamLoader::WriteSegmentFun(nsIInputStream *,
                                       void *aClosure,
                                       const char *aSegment,
                                       PRUint32,
                                       PRUint32 aCount,
                                       PRUint32 *aWriteCount)
{
  nsUnicharStreamLoader* self = static_cast<nsUnicharStreamLoader*>(aClosure);

  PRUint32 haveRead = self->mBuffer.Length();
  PRUint32 consumed = 0;
  nsresult rv;
  do {
    PRInt32 srcLen = aCount - consumed;
    PRInt32 dstLen;
    self->mDecoder->GetMaxLength(aSegment + consumed, srcLen, &dstLen);

    PRUint32 capacity = haveRead + dstLen;
    if (!self->mBuffer.SetCapacity(capacity)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    rv = self->mDecoder->Convert(aSegment + consumed,
                                 &srcLen,
                                 self->mBuffer.BeginWriting() + haveRead,
                                 &dstLen);
    haveRead += dstLen;
    // XXX if srcLen is negative, we want to drop the _first_ byte in
    // the erroneous byte sequence and try again.  This is not quite
    // possible right now -- see bug 160784
    consumed += srcLen;
    if (NS_FAILED(rv)) {
      if (haveRead >= capacity) {
        // Make room for writing the 0xFFFD below (bug 785753).
        if (!self->mBuffer.SetCapacity(haveRead + 1)) {
          return NS_ERROR_OUT_OF_MEMORY;
        }
      }
      self->mBuffer.BeginWriting()[haveRead++] = 0xFFFD;
      ++consumed;
      // XXX this is needed to make sure we don't underrun our buffer;
      // bug 160784 again
      consumed = NS_MAX<PRUint32>(consumed, 0);
      self->mDecoder->Reset();
    }
  } while (consumed < aCount);

  self->mBuffer.SetLength(haveRead);
  *aWriteCount = aCount;
  return NS_OK;
}
