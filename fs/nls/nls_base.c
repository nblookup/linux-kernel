/*
 * linux/fs/nls.c
 *
 * Native language support--charsets and unicode translations.
 * By Gordon Chaffee 1996, 1997
 *
 * Unicode based case conversion 1999 by Wolfram Pienkoss
 *
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/config.h>
#include <linux/nls.h>
#include <linux/malloc.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif
#include <asm/byteorder.h>

static struct nls_table *tables = (struct nls_table *) NULL;

/*
 * Sample implementation from Unicode home page.
 * http://www.stonehand.com/unicode/standard/fss-utf.html
 */
struct utf8_table {
	int     cmask;
	int     cval;
	int     shift;
	long    lmask;
	long    lval;
};

static struct utf8_table utf8_table[] =
{
    {0x80,  0x00,   0*6,    0x7F,           0,         /* 1 byte sequence */},
    {0xE0,  0xC0,   1*6,    0x7FF,          0x80,      /* 2 byte sequence */},
    {0xF0,  0xE0,   2*6,    0xFFFF,         0x800,     /* 3 byte sequence */},
    {0xF8,  0xF0,   3*6,    0x1FFFFF,       0x10000,   /* 4 byte sequence */},
    {0xFC,  0xF8,   4*6,    0x3FFFFFF,      0x200000,  /* 5 byte sequence */},
    {0xFE,  0xFC,   5*6,    0x7FFFFFFF,     0x4000000, /* 6 byte sequence */},
    {0,						       /* end of table    */}
};

int
utf8_mbtowc(__u16 *p, const __u8 *s, int n)
{
	long l;
	int c0, c, nc;
	struct utf8_table *t;
  
	nc = 0;
	c0 = *s;
	l = c0;
	for (t = utf8_table; t->cmask; t++) {
		nc++;
		if ((c0 & t->cmask) == t->cval) {
			l &= t->lmask;
			if (l < t->lval)
				return -1;
			*p = l;
			return nc;
		}
		if (n <= nc)
			return -1;
		s++;
		c = (*s ^ 0x80) & 0xFF;
		if (c & 0xC0)
			return -1;
		l = (l << 6) | c;
	}
	return -1;
}

int
utf8_mbstowcs(__u16 *pwcs, const __u8 *s, int n)
{
	__u16 *op;
	const __u8 *ip;
	int size;

	op = pwcs;
	ip = s;
	while (*ip && n > 0) {
		if (*ip & 0x80) {
			size = utf8_mbtowc(op, ip, n);
			if (size == -1) {
				/* Ignore character and move on */
				ip++;
				n--;
			} else {
				op += size;
				ip += size;
				n -= size;
			}
		} else {
			*op++ = *ip++;
		}
	}
	return (op - pwcs);
}

int
utf8_wctomb(__u8 *s, __u16 wc, int maxlen)
{
	long l;
	int c, nc;
	struct utf8_table *t;
  
	if (s == 0)
		return 0;
  
	l = wc;
	nc = 0;
	for (t = utf8_table; t->cmask && maxlen; t++, maxlen--) {
		nc++;
		if (l <= t->lmask) {
			c = t->shift;
			*s = t->cval | (l >> c);
			while (c > 0) {
				c -= 6;
				s++;
				*s = 0x80 | ((l >> c) & 0x3F);
			}
			return nc;
		}
	}
	return -1;
}

int
utf8_wcstombs(__u8 *s, const __u16 *pwcs, int maxlen)
{
	const __u16 *ip;
	__u8 *op;
	int size;

	op = s;
	ip = pwcs;
	while (*ip && maxlen > 0) {
		if (*ip > 0x7f) {
			size = utf8_wctomb(op, *ip, maxlen);
			if (size == -1) {
				/* Ignore character and move on */
				maxlen--;
			} else {
				op += size;
				maxlen -= size;
			}
		} else {
			*op++ = (__u8) *ip;
		}
		ip++;
	}
	return (op - s);
}

int register_nls(struct nls_table * nls)
{
	struct nls_table ** tmp = &tables;

	if (!nls)
		return -EINVAL;
	if (nls->next)
		return -EBUSY;
	while (*tmp) {
		if (nls == *tmp) {
			return -EBUSY;
		}
		tmp = &(*tmp)->next;
	}
	nls->next = tables;
	tables = nls;
	return 0;	
}

int unregister_nls(struct nls_table * nls)
{
	struct nls_table ** tmp = &tables;

	while (*tmp) {
		if (nls == *tmp) {
			*tmp = nls->next;
			return 0;
		}
		tmp = &(*tmp)->next;
	}
	return -EINVAL;
}

struct nls_table *find_nls(char *charset)
{
	struct nls_table *nls = tables;
	while (nls) {
		if (! strcmp(nls->charset, charset))
			return nls;
		nls = nls->next;
	}
	return NULL;
}

struct nls_table *load_nls(char *charset)
{
	struct nls_table *nls;
#ifdef CONFIG_KMOD
	char buf[40];
	int ret;
#endif

	nls = find_nls(charset);
	if (nls) {
		nls->inc_use_count();
		return nls;
	}

#ifndef CONFIG_KMOD
	return NULL;
#else
	if (strlen(charset) > sizeof(buf) - sizeof("nls_")) {
		printk("Unable to load NLS charset %s: name too long\n", charset);
		return NULL;
	}
		
	sprintf(buf, "nls_%s", charset);
	ret = request_module(buf);
	if (ret != 0) {
		printk("Unable to load NLS charset %s\n", charset);
		return NULL;
	}
	nls = find_nls(charset);
	if (nls) {
		nls->inc_use_count();
	}
	return nls;
#endif
}

void unload_nls(struct nls_table *nls)
{
	nls->dec_use_count();
}

struct nls_unicode charset2uni[256] = {
	/* 0x00*/
	{0x00, 0x00}, {0x01, 0x00}, {0x02, 0x00}, {0x03, 0x00},
	{0x04, 0x00}, {0x05, 0x00}, {0x06, 0x00}, {0x07, 0x00},
	{0x08, 0x00}, {0x09, 0x00}, {0x0a, 0x00}, {0x0b, 0x00},
	{0x0c, 0x00}, {0x0d, 0x00}, {0x0e, 0x00}, {0x0f, 0x00},
	/* 0x10*/
	{0x10, 0x00}, {0x11, 0x00}, {0x12, 0x00}, {0x13, 0x00},
	{0x14, 0x00}, {0x15, 0x00}, {0x16, 0x00}, {0x17, 0x00},
	{0x18, 0x00}, {0x19, 0x00}, {0x1a, 0x00}, {0x1b, 0x00},
	{0x1c, 0x00}, {0x1d, 0x00}, {0x1e, 0x00}, {0x1f, 0x00},
	/* 0x20*/
	{0x20, 0x00}, {0x21, 0x00}, {0x22, 0x00}, {0x23, 0x00},
	{0x24, 0x00}, {0x25, 0x00}, {0x26, 0x00}, {0x27, 0x00},
	{0x28, 0x00}, {0x29, 0x00}, {0x2a, 0x00}, {0x2b, 0x00},
	{0x2c, 0x00}, {0x2d, 0x00}, {0x2e, 0x00}, {0x2f, 0x00},
	/* 0x30*/
	{0x30, 0x00}, {0x31, 0x00}, {0x32, 0x00}, {0x33, 0x00},
	{0x34, 0x00}, {0x35, 0x00}, {0x36, 0x00}, {0x37, 0x00},
	{0x38, 0x00}, {0x39, 0x00}, {0x3a, 0x00}, {0x3b, 0x00},
	{0x3c, 0x00}, {0x3d, 0x00}, {0x3e, 0x00}, {0x3f, 0x00},
	/* 0x40*/
	{0x40, 0x00}, {0x41, 0x00}, {0x42, 0x00}, {0x43, 0x00},
	{0x44, 0x00}, {0x45, 0x00}, {0x46, 0x00}, {0x47, 0x00},
	{0x48, 0x00}, {0x49, 0x00}, {0x4a, 0x00}, {0x4b, 0x00},
	{0x4c, 0x00}, {0x4d, 0x00}, {0x4e, 0x00}, {0x4f, 0x00},
	/* 0x50*/
	{0x50, 0x00}, {0x51, 0x00}, {0x52, 0x00}, {0x53, 0x00},
	{0x54, 0x00}, {0x55, 0x00}, {0x56, 0x00}, {0x57, 0x00},
	{0x58, 0x00}, {0x59, 0x00}, {0x5a, 0x00}, {0x5b, 0x00},
	{0x5c, 0x00}, {0x5d, 0x00}, {0x5e, 0x00}, {0x5f, 0x00},
	/* 0x60*/
	{0x60, 0x00}, {0x61, 0x00}, {0x62, 0x00}, {0x63, 0x00},
	{0x64, 0x00}, {0x65, 0x00}, {0x66, 0x00}, {0x67, 0x00},
	{0x68, 0x00}, {0x69, 0x00}, {0x6a, 0x00}, {0x6b, 0x00},
	{0x6c, 0x00}, {0x6d, 0x00}, {0x6e, 0x00}, {0x6f, 0x00},
	/* 0x70*/
	{0x70, 0x00}, {0x71, 0x00}, {0x72, 0x00}, {0x73, 0x00},
	{0x74, 0x00}, {0x75, 0x00}, {0x76, 0x00}, {0x77, 0x00},
	{0x78, 0x00}, {0x79, 0x00}, {0x7a, 0x00}, {0x7b, 0x00},
	{0x7c, 0x00}, {0x7d, 0x00}, {0x7e, 0x00}, {0x7f, 0x00},
	/* 0x80*/
	{0x80, 0x00}, {0x81, 0x00}, {0x82, 0x00}, {0x83, 0x00},
	{0x84, 0x00}, {0x85, 0x00}, {0x86, 0x00}, {0x87, 0x00},
	{0x88, 0x00}, {0x89, 0x00}, {0x8a, 0x00}, {0x8b, 0x00},
	{0x8c, 0x00}, {0x8d, 0x00}, {0x8e, 0x00}, {0x8f, 0x00},
	/* 0x90*/
	{0x90, 0x00}, {0x91, 0x00}, {0x92, 0x00}, {0x93, 0x00},
	{0x94, 0x00}, {0x95, 0x00}, {0x96, 0x00}, {0x97, 0x00},
	{0x98, 0x00}, {0x99, 0x00}, {0x9a, 0x00}, {0x9b, 0x00},
	{0x9c, 0x00}, {0x9d, 0x00}, {0x9e, 0x00}, {0x9f, 0x00},
	/* 0xa0*/
	{0xa0, 0x00}, {0xa1, 0x00}, {0xa2, 0x00}, {0xa3, 0x00},
	{0xa4, 0x00}, {0xa5, 0x00}, {0xa6, 0x00}, {0xa7, 0x00},
	{0xa8, 0x00}, {0xa9, 0x00}, {0xaa, 0x00}, {0xab, 0x00},
	{0xac, 0x00}, {0xad, 0x00}, {0xae, 0x00}, {0xaf, 0x00},
	/* 0xb0*/
	{0xb0, 0x00}, {0xb1, 0x00}, {0xb2, 0x00}, {0xb3, 0x00},
	{0xb4, 0x00}, {0xb5, 0x00}, {0xb6, 0x00}, {0xb7, 0x00},
	{0xb8, 0x00}, {0xb9, 0x00}, {0xba, 0x00}, {0xbb, 0x00},
	{0xbc, 0x00}, {0xbd, 0x00}, {0xbe, 0x00}, {0xbf, 0x00},
	/* 0xc0*/
	{0xc0, 0x00}, {0xc1, 0x00}, {0xc2, 0x00}, {0xc3, 0x00},
	{0xc4, 0x00}, {0xc5, 0x00}, {0xc6, 0x00}, {0xc7, 0x00},
	{0xc8, 0x00}, {0xc9, 0x00}, {0xca, 0x00}, {0xcb, 0x00},
	{0xcc, 0x00}, {0xcd, 0x00}, {0xce, 0x00}, {0xcf, 0x00},
	/* 0xd0*/
	{0xd0, 0x00}, {0xd1, 0x00}, {0xd2, 0x00}, {0xd3, 0x00},
	{0xd4, 0x00}, {0xd5, 0x00}, {0xd6, 0x00}, {0xd7, 0x00},
	{0xd8, 0x00}, {0xd9, 0x00}, {0xda, 0x00}, {0xdb, 0x00},
	{0xdc, 0x00}, {0xdd, 0x00}, {0xde, 0x00}, {0xdf, 0x00},
	/* 0xe0*/
	{0xe0, 0x00}, {0xe1, 0x00}, {0xe2, 0x00}, {0xe3, 0x00},
	{0xe4, 0x00}, {0xe5, 0x00}, {0xe6, 0x00}, {0xe7, 0x00},
	{0xe8, 0x00}, {0xe9, 0x00}, {0xea, 0x00}, {0xeb, 0x00},
	{0xec, 0x00}, {0xed, 0x00}, {0xee, 0x00}, {0xef, 0x00},
	/* 0xf0*/
	{0xf0, 0x00}, {0xf1, 0x00}, {0xf2, 0x00}, {0xf3, 0x00},
	{0xf4, 0x00}, {0xf5, 0x00}, {0xf6, 0x00}, {0xf7, 0x00},
	{0xf8, 0x00}, {0xf9, 0x00}, {0xfa, 0x00}, {0xfb, 0x00},
	{0xfc, 0x00}, {0xfd, 0x00}, {0xfe, 0x00}, {0xff, 0x00},
};

static unsigned char page00[256] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, /* 0x00-0x07 */
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, /* 0x08-0x0f */
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, /* 0x10-0x17 */
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, /* 0x18-0x1f */
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, /* 0x20-0x27 */
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, /* 0x28-0x2f */
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, /* 0x30-0x37 */
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, /* 0x38-0x3f */
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, /* 0x40-0x47 */
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, /* 0x48-0x4f */
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, /* 0x50-0x57 */
	0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, /* 0x58-0x5f */
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, /* 0x60-0x67 */
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, /* 0x68-0x6f */
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, /* 0x70-0x77 */
	0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, /* 0x78-0x7f */

	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, /* 0x80-0x87 */
	0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, /* 0x88-0x8f */
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, /* 0x90-0x97 */
	0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, /* 0x98-0x9f */
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, /* 0xa0-0xa7 */
	0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, /* 0xa8-0xaf */
	0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, /* 0xb0-0xb7 */
	0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, /* 0xb8-0xbf */
	0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, /* 0xc0-0xc7 */
	0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, /* 0xc8-0xcf */
	0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, /* 0xd0-0xd7 */
	0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, /* 0xd8-0xdf */
	0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, /* 0xe0-0xe7 */
	0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, /* 0xe8-0xef */
	0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, /* 0xf0-0xf7 */
	0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff, /* 0xf8-0xff */
};

static unsigned char *page_uni2charset[256] = {
	page00
};

static unsigned char charset2lower[256] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, /* 0x00-0x07 */
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, /* 0x08-0x0f */
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, /* 0x10-0x17 */
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, /* 0x18-0x1f */
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, /* 0x20-0x27 */
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, /* 0x28-0x2f */
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, /* 0x30-0x37 */
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, /* 0x38-0x3f */
	0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, /* 0x40-0x47 */
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, /* 0x48-0x4f */
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, /* 0x50-0x57 */
	0x78, 0x79, 0x7a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, /* 0x58-0x5f */
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, /* 0x60-0x67 */
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, /* 0x68-0x6f */
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, /* 0x70-0x77 */
	0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, /* 0x78-0x7f */

	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, /* 0x80-0x87 */
	0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, /* 0x88-0x8f */
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, /* 0x90-0x97 */
	0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, /* 0x98-0x9f */
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, /* 0xa0-0xa7 */
	0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, /* 0xa8-0xaf */
	0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, /* 0xb0-0xb7 */
	0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, /* 0xb8-0xbf */
	0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, /* 0xc0-0xc7 */
	0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, /* 0xc8-0xcf */
	0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, /* 0xd0-0xd7 */
	0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, /* 0xd8-0xdf */
	0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, /* 0xe0-0xe7 */
	0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, /* 0xe8-0xef */
	0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, /* 0xf0-0xf7 */
	0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff, /* 0xf8-0xff */
};

static unsigned char charset2upper[256] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, /* 0x00-0x07 */
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, /* 0x08-0x0f */
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, /* 0x10-0x17 */
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, /* 0x18-0x1f */
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, /* 0x20-0x27 */
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, /* 0x28-0x2f */
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, /* 0x30-0x37 */
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, /* 0x38-0x3f */
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, /* 0x40-0x47 */
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, /* 0x48-0x4f */
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, /* 0x50-0x57 */
	0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, /* 0x58-0x5f */
	0x60, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, /* 0x60-0x67 */
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, /* 0x68-0x6f */
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, /* 0x70-0x77 */
	0x58, 0x59, 0x5a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, /* 0x78-0x7f */

	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, /* 0x80-0x87 */
	0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, /* 0x88-0x8f */
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, /* 0x90-0x97 */
	0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, /* 0x98-0x9f */
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, /* 0xa0-0xa7 */
	0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, /* 0xa8-0xaf */
	0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, /* 0xb0-0xb7 */
	0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, /* 0xb8-0xbf */
	0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, /* 0xc0-0xc7 */
	0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, /* 0xc8-0xcf */
	0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, /* 0xd0-0xd7 */
	0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, /* 0xd8-0xdf */
	0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, /* 0xe0-0xe7 */
	0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, /* 0xe8-0xef */
	0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, /* 0xf0-0xf7 */
	0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff, /* 0xf8-0xff */
};


void inc_use_count(void)
{
}

void dec_use_count(void)
{
}

static struct nls_table default_table = {
	"default",
	page_uni2charset,
	charset2uni,
	charset2lower,
	charset2upper,
	inc_use_count,
	dec_use_count,
	NULL
};



/* Returns a simple default translation table */
struct nls_table *load_nls_default(void)
{
	return &default_table;
}

EXPORT_SYMBOL(register_nls);
EXPORT_SYMBOL(unregister_nls);
EXPORT_SYMBOL(unload_nls);
EXPORT_SYMBOL(find_nls);
EXPORT_SYMBOL(load_nls);
EXPORT_SYMBOL(load_nls_default);
EXPORT_SYMBOL(utf8_mbtowc);
EXPORT_SYMBOL(utf8_mbstowcs);
EXPORT_SYMBOL(utf8_wctomb);
EXPORT_SYMBOL(utf8_wcstombs);

int init_nls(void)
{
#ifdef CONFIG_NLS_ISO8859_1
	init_nls_iso8859_1();
#endif
#ifdef CONFIG_NLS_ISO8859_2
	init_nls_iso8859_2();
#endif
#ifdef CONFIG_NLS_ISO8859_3
	init_nls_iso8859_3();
#endif
#ifdef CONFIG_NLS_ISO8859_4
	init_nls_iso8859_4();
#endif
#ifdef CONFIG_NLS_ISO8859_5
	init_nls_iso8859_5();
#endif
#ifdef CONFIG_NLS_ISO8859_6
	init_nls_iso8859_6();
#endif
#ifdef CONFIG_NLS_ISO8859_7
	init_nls_iso8859_7();
#endif
#ifdef CONFIG_NLS_ISO8859_8
	init_nls_iso8859_8();
#endif
#ifdef CONFIG_NLS_ISO8859_9
	init_nls_iso8859_9();
#endif
#ifdef CONFIG_NLS_ISO8859_14
        init_nls_iso8859_14();
#endif
#ifdef CONFIG_NLS_ISO8859_15
	init_nls_iso8859_15();
#endif
#ifdef CONFIG_NLS_CODEPAGE_437
	init_nls_cp437();
#endif
#ifdef CONFIG_NLS_CODEPAGE_737
	init_nls_cp737();
#endif
#ifdef CONFIG_NLS_CODEPAGE_775
	init_nls_cp775();
#endif
#ifdef CONFIG_NLS_CODEPAGE_850
	init_nls_cp850();
#endif
#ifdef CONFIG_NLS_CODEPAGE_852
	init_nls_cp852();
#endif
#ifdef CONFIG_NLS_CODEPAGE_855
	init_nls_cp855();
#endif
#ifdef CONFIG_NLS_CODEPAGE_857
	init_nls_cp857();
#endif
#ifdef CONFIG_NLS_CODEPAGE_860
	init_nls_cp860();
#endif
#ifdef CONFIG_NLS_CODEPAGE_861
	init_nls_cp861();
#endif
#ifdef CONFIG_NLS_CODEPAGE_862
	init_nls_cp862();
#endif
#ifdef CONFIG_NLS_CODEPAGE_863
	init_nls_cp863();
#endif
#ifdef CONFIG_NLS_CODEPAGE_864
	init_nls_cp864();
#endif
#ifdef CONFIG_NLS_CODEPAGE_865
	init_nls_cp865();
#endif
#ifdef CONFIG_NLS_CODEPAGE_866
	init_nls_cp866();
#endif
#ifdef CONFIG_NLS_CODEPAGE_869
	init_nls_cp869();
#endif
#ifdef CONFIG_NLS_CODEPAGE_874
	init_nls_cp874();
#endif
#ifdef CONFIG_NLS_KOI8_R
	init_nls_koi8_r();
#endif
	return 0;
}

#ifdef MODULE
int init_module(void)
{
	return init_nls();
}


void cleanup_module(void)
{
}
#endif /* ifdef MODULE */
