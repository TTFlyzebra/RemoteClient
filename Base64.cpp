//
// Created by FlyZebra on 2021/7/29 0029.
//

#include "Base64.h"

static const char *g_pCodes ="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

CBase64::CBase64()
{
}

CBase64::~CBase64()
{
}

bool CBase64::Encode(const unsigned char *pIn, unsigned long uInLen, unsigned char *pOut, unsigned long *uOutLen)
{
	unsigned long i, len2, leven;
	unsigned char *p;

	if(pOut == nullptr || *uOutLen == 0)
		return false;

	//ASSERT((pIn != NULL) && (uInLen != 0) && (pOut != NULL) && (uOutLen != NULL));

	len2 = ((uInLen + 2) / 3) << 2;
	if((*uOutLen) < (len2 + 1)) return false;

	p = pOut;
	leven = 3 * (uInLen / 3);
	for(i = 0; i < leven; i += 3)
	{
		*p++ = g_pCodes[pIn[0] >> 2];
		*p++ = g_pCodes[((pIn[0] & 3) << 4) + (pIn[1] >> 4)];
		*p++ = g_pCodes[((pIn[1] & 0xf) << 2) + (pIn[2] >> 6)];
		*p++ = g_pCodes[pIn[2] & 0x3f];
		pIn += 3;
	}

	if (i < uInLen)
	{
		unsigned char a = pIn[0];
		unsigned char b = ((i + 1) < uInLen) ? pIn[1] : 0;
		unsigned char c = 0;

		*p++ = g_pCodes[a >> 2];
		*p++ = g_pCodes[((a & 3) << 4) + (b >> 4)];
		*p++ = ((i + 1) < uInLen) ? g_pCodes[((b & 0xf) << 2) + (c >> 6)] : '=';
		*p++ = '=';
	}

	*p = 0; // Append NULL byte
	*uOutLen = p - pOut;
	return true;
}
