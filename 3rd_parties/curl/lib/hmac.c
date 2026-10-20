/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 * SPDX-License-Identifier: curl
 *
 * RFC2104 Keyed-Hashing for Message Authentication
 *
 ***************************************************************************/
#include "curl_setup.h"

#if (defined(USE_CURL_NTLM_CORE) && !defined(USE_WINDOWS_SSPI)) ||    \
  !defined(CURL_DISABLE_AWS) || !defined(CURL_DISABLE_DIGEST_AUTH) || \
  defined(USE_SSL)

#include "curl_hmac.h"

/*
 * Generic HMAC algorithm.
 *
 *   This module computes HMAC digests based on any hash function. Parameters
 * and computing procedures are setup dynamically at HMAC computation context
 * initialization.
 */

static const unsigned char hmac_ipad = 0x36;
static const unsigned char hmac_opad = 0x5C;

CURLcode Curl_HMAC_init(const struct HMAC_params *hashparams,
                         const unsigned char *key, unsigned int keylen,
                         struct HMAC_context **pcontext)
{
  size_t i;
  struct HMAC_context *ctxt;
  unsigned char *hkey;
  unsigned char b;
  CURLcode result = CURLE_OK;
  CURLcode final_result;
  bool initialized1 = FALSE;
  bool initialized2 = FALSE;

  *pcontext = NULL;

  /* Create HMAC context. */
  i = sizeof(*ctxt) + (2 * hashparams->ctxtsize) + hashparams->resultlen;
  ctxt = curlx_malloc(i);

  if(!ctxt)
    return CURLE_OUT_OF_MEMORY;

  ctxt->hash = hashparams;
  ctxt->error = CURLE_OK;
  ctxt->hashctxt1 = (void *)(ctxt + 1);
  ctxt->hashctxt2 = (void *)((char *)ctxt->hashctxt1 + hashparams->ctxtsize);
  hkey = (unsigned char *)ctxt->hashctxt2 + hashparams->ctxtsize;

  /* If the key is too long, replace it by its hash digest. */
  if(keylen > hashparams->maxkeylen) {
    result = hashparams->hinit(ctxt->hashctxt1);
    if(result)
      goto fail;
    result = hashparams->hupdate(ctxt->hashctxt1, key, keylen);
    final_result = hashparams->hfinal(hkey, ctxt->hashctxt1);
    if(!result)
      result = final_result;
    if(result)
      goto fail;
    key = hkey;
    keylen = hashparams->resultlen;
  }

  /* Prime the two hash contexts with the modified key. */
  result = hashparams->hinit(ctxt->hashctxt1);
  if(result)
    goto fail;
  initialized1 = TRUE;
  result = hashparams->hinit(ctxt->hashctxt2);
  if(result)
    goto fail;
  initialized2 = TRUE;

  for(i = 0; i < keylen; i++) {
    b = (unsigned char)(*key ^ hmac_ipad);
    result = hashparams->hupdate(ctxt->hashctxt1, &b, 1);
    if(result)
      goto fail;
    b = (unsigned char)(*key++ ^ hmac_opad);
    result = hashparams->hupdate(ctxt->hashctxt2, &b, 1);
    if(result)
      goto fail;
  }

  for(; i < hashparams->maxkeylen; i++) {
    result = hashparams->hupdate(ctxt->hashctxt1, &hmac_ipad, 1);
    if(result)
      goto fail;
    result = hashparams->hupdate(ctxt->hashctxt2, &hmac_opad, 1);
    if(result)
      goto fail;
  }

  /* Done, return pointer to HMAC context. */
  *pcontext = ctxt;
  return CURLE_OK;

fail:
  if(initialized1) {
    final_result = hashparams->hfinal(hkey, ctxt->hashctxt1);
    if(!result)
      result = final_result;
  }
  if(initialized2) {
    final_result = hashparams->hfinal(hkey, ctxt->hashctxt2);
    if(!result)
      result = final_result;
  }
  curlx_free(ctxt);
  return result;
}

void Curl_HMAC_update(struct HMAC_context *ctxt,
                      const unsigned char *data,
                      unsigned int len)
{
  /* Update first hash calculation. */
  if(!ctxt->error)
    ctxt->error = ctxt->hash->hupdate(ctxt->hashctxt1, data, len);
}

CURLcode Curl_HMAC_final(struct HMAC_context *ctxt, unsigned char *output)
{
  const struct HMAC_params *hashparams = ctxt->hash;
  CURLcode result = ctxt->error;
  CURLcode final_result;

  /* Do not get output if called with a null parameter: only release
     storage. */

  if(!output)
    output = (unsigned char *)ctxt->hashctxt2 + ctxt->hash->ctxtsize;

  final_result = hashparams->hfinal(output, ctxt->hashctxt1);
  if(!result)
    result = final_result;
  if(!result)
    result = hashparams->hupdate(ctxt->hashctxt2, output,
                                 hashparams->resultlen);
  final_result = hashparams->hfinal(output, ctxt->hashctxt2);
  if(!result)
    result = final_result;
  curlx_free(ctxt);
  return result;
}

/*
 * Curl_hmacit()
 *
 * This is used to generate a HMAC hash, for the specified input data, given
 * the specified hash function and key.
 *
 * Parameters:
 *
 * hashparams [in]     - The hash function (Curl_HMAC_MD5).
 * key        [in]     - The key to use.
 * keylen     [in]     - The length of the key.
 * buf        [in]     - The data to encrypt.
 * buflen     [in]     - The length of the data.
 * output     [in/out] - The output buffer.
 *
 * Returns CURLE_OK on success.
 */
CURLcode Curl_hmacit(const struct HMAC_params *hashparams,
                     const unsigned char *key, const size_t keylen,
                     const unsigned char *data, size_t datalen,
                     unsigned char *output)
{
  struct HMAC_context *ctxt;
  CURLcode result;
  if(keylen > UINT_MAX) /* unlikely to ever happen */
    return CURLE_BAD_FUNCTION_ARGUMENT;
  result = Curl_HMAC_init(hashparams, key, curlx_uztoui(keylen), &ctxt);
  if(result)
    return result;

  /* Update the digest with the given challenge */
  do {
    unsigned int ilen = (unsigned int)CURLMIN(datalen, UINT_MAX);
    Curl_HMAC_update(ctxt, data, ilen);
    datalen -= ilen;
    data += ilen;
  } while(datalen);

  /* Finalise the digest */
  return Curl_HMAC_final(ctxt, output);
}

#endif /* Using NTLM (without SSPI) or AWS */
