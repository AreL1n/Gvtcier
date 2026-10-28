#ifndef lctype_h
#define lctype_h

#include "gvts.h"

#if !defined(GVTS_USE_CTYPE)

#if 'A' == 65 && '0' == 48

#define GVTS_USE_CTYPE	0
#else

#define GVTS_USE_CTYPE	1
#endif

#endif

#if !GVTS_USE_CTYPE	       

#include <limits.h>

#include "Glimits.h"

#define ALPHABIT	0
#define DIGITBIT	1
#define PRINTBIT	2
#define SPACEBIT	3
#define XDIGITBIT	4

#define MASK(B)		(1 << (B))

#define testprop(c,p)	(gvtsi_ctype_[(c)+1] & (p))

#define lislalpha(c)	((c) >= 0x80 || testprop(c, MASK(ALPHABIT)))
#define lislalnum(c)	((c) >= 0x80 || testprop(c, (MASK(ALPHABIT) | MASK(DIGITBIT))))
#define lisdigit(c)	testprop(c, MASK(DIGITBIT))
#define lisspace(c)	testprop(c, MASK(SPACEBIT))
#define lisprint(c)	testprop(c, MASK(PRINTBIT))
#define lisxdigit(c)	testprop(c, MASK(XDIGITBIT))

#define ltolower(c)  \
  check_exp(('A' <= (c) && (c) <= 'Z') || (c) == ((c) | ('A' ^ 'a')),  \
            (c) | ('A' ^ 'a'))

GVTSI_DDEC(const lu_byte gvtsi_ctype_[UCHAR_MAX + 2];)

#else			        

#include <ctype.h>

#define lislalpha(c)	(isalpha(c) || (c) == '_')
#define lislalnum(c)	(isalnum(c) || (c) == '_')
#define lisdigit(c)	(isdigit(c))
#define lisspace(c)	(isspace(c))
#define lisprint(c)	(isprint(c))
#define lisxdigit(c)	(isxdigit(c))

#define ltolower(c)	(tolower(c))

#endif			       

#endif
