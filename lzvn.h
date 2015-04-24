/*
 *
 * AnV Software - rebuild version of the LZVN compression library
 *
 */

#ifndef _ANV_LZVN_H_
#define _ANV_LZVN_H_ 1

// Uncompress LZVN ;) (Thanks to MinusZwei for the .c version from my 64-Bit assembly :D)
extern size_t lzvn_decode(void * _dest, size_t _dest_size, void * _src, size_t _src_size);

// < XXX = TODO : Compress LZVN = XXX >
extern unsigned char *lzvn_encode(unsigned char *dst, size_t dstlen, unsigned char *src, size_t srclen);

#endif /* _ANV_LZVN_H_ */
