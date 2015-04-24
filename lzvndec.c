//
//  lzvn.c
//
//  Created by MinusZwei on 9/14/14.
//  Copyright (c) 2014 MinusZwei. All rights reserved.
//
//  Based on disassembly work of AnV Software (Andy Vandijck)
//  And extended and renamed for other OS's by AnV Software.
//

#include <stdio.h>
#include <string.h>

#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#endif /* __APPLE__ */

#ifndef OSSwapInt64
#ifdef _MSC_VER
#define OSSwapInt64(x) \
((((x) & 0xff) << 56) |	\
 (((x) & 0xff00) << 40) |	\
 (((x) & 0xff0000) << 24) |	\
 (((x) & 0xff000000) <<  8) |	\
 (((x) & 0xff00000000) >>  8) |	\
 (((x) & 0xff0000000000) >> 24) |	\
 (((x) & 0xff000000000000) >> 40) |	\
 (((x) & 0xff00000000000000) >> 56))
#else /* __GNUC__ || __clang__ */
inline uint64_t OSSwapInt64(uint64_t data)
{
	__asm__("bswap   %0" : "+r" (data));
    
	return data;
}
#endif /* __COMPILER_SEL__ */
#endif /* OSSwapInt64 */

static short Llzvn_tableref[256] =
{
     1,  1,  1,  1,    1,  1,  2,  3,    1,  1,  1,  1,    1,  1,  4,  3,
     1,  1,  1,  1,    1,  1,  4,  3,    1,  1,  1,  1,    1,  1,  5,  3,
     1,  1,  1,  1,    1,  1,  5,  3,    1,  1,  1,  1,    1,  1,  5,  3,
     1,  1,  1,  1,    1,  1,  5,  3,    1,  1,  1,  1,    1,  1,  5,  3,
     1,  1,  1,  1,    1,  1,  0,  3,    1,  1,  1,  1,    1,  1,  0,  3,
     1,  1,  1,  1,    1,  1,  0,  3,    1,  1,  1,  1,    1,  1,  0,  3,
     1,  1,  1,  1,    1,  1,  0,  3,    1,  1,  1,  1,    1,  1,  0,  3,
     5,  5,  5,  5,    5,  5,  5,  5,    5,  5,  5,  5,    5,  5,  5,  5,
     1,  1,  1,  1,    1,  1,  0,  3,    1,  1,  1,  1,    1,  1,  0,  3,
     1,  1,  1,  1,    1,  1,  0,  3,    1,  1,  1,  1,    1,  1,  0,  3,
     6,  6,  6,  6,    6,  6,  6,  6,    6,  6,  6,  6,    6,  6,  6,  6,
     6,  6,  6,  6,    6,  6,  6,  6,    6,  6,  6,  6,    6,  6,  6,  6,
     1,  1,  1,  1,    1,  1,  0,  3,    1,  1,  1,  1,    1,  1,  0,  3,
     5,  5,  5,  5,    5,  5,  5,  5,    5,  5,  5,  5,    5,  5,  5,  5,
     7,  8,  8,  8,    8,  8,  8,  8,    8,  8,  8,  8,    8,  8,  8,  8,
     9, 10, 10, 10,   10, 10, 10, 10,   10, 10, 10, 10,   10, 10, 10, 10
};

#define GOTO_LABEL(reg)                     \
    do {                                    \
        switch (Llzvn_tableref[(reg)]) {    \
            case 0:  goto Llzvn_table0;     \
            case 1:  goto Llzvn_table1;     \
            case 2:  goto Llzvn_table2;     \
            case 3:  goto Llzvn_table3;     \
            case 4:  goto Llzvn_table4;     \
            case 5:  goto Llzvn_table5;     \
            case 6:  goto Llzvn_table6;     \
            case 7:  goto Llzvn_table7;     \
            case 8:  goto Llzvn_table8;     \
            case 9:  goto Llzvn_table9;     \
            case 10: goto Llzvn_table10;    \
        }                                   \
    } while (0)

size_t lzvn_decode(void * _dest, size_t _dest_size, void * _src, size_t _src_size)
{
    size_t   retval = 0;
    const uint64_t _dstptr = (const uint64_t)_dest;
    uint64_t _destsizehandle       = _dest_size;
    uint64_t _srcsizehandle       = _src_size;
    uint64_t _srcptr       = (uint64_t)_src;
    uint64_t vala  = 0;
    uint64_t valb  = 0;
    uint64_t valc = 0;
    uint64_t vald = 0;
    uint64_t vale = 0;
    uint64_t addr           = 0;
    unsigned char byte_data = 0;
    uint64_t uiVal = 0;
    short jmp = 0;
    
    // lea    Llzvn_tableref(%rip),%rbx
    //
    //    this will load the address of the tableref label into the %rbx
    //    register. in our code, this is the 'Llzvn_tableref' array
    //
    //  for clearness, it will be used directly.
    
    retval = 0;    // xor    %retval,%retval
    vale = 0;    // xor    %vale,%vale
    
    // sub    $0x8,%_destsizehandle
    // jb     Llzvn_exit
    jmp = _destsizehandle < 0x8 ? 1 : 0;
    _destsizehandle -= 0x8;

    if (jmp)
    {
        goto Llzvn_exit;
    }
    
    // lea    -0x8(%_srcptr,%_srcsizehandle,1),%_srcsizehandle
    // cmp    %_srcsizehandle,%_srcptr
	// ja     Llzvn_exit
    _srcsizehandle = _srcptr + _srcsizehandle - 0x8;

    if (_srcptr > _srcsizehandle)
    {
        goto Llzvn_exit;
    }
    
    vala = *(uint64_t *)_srcptr;    // mov    (%_srcptr),%vala
    valb = vala & 0xFF;           // movzbq (%_srcptr),%valb

    GOTO_LABEL(valb);           // jmpq   *(%rbx,%valb,8)
    
Llzvn_table4:
    // add    $0x1,%_srcptr
	// cmp    %_srcsizehandle,%_srcptr
	// ja     Llzvn_exit
    _srcptr += 1;

    if (_srcptr > _srcsizehandle)
    {
        goto Llzvn_exit;
    }

    vala = *(uint64_t *)_srcptr;    // mov    (%_srcptr),%vala
    valb = vala & 0xFF;           // movzbq (%_srcptr),%valb

    GOTO_LABEL(valb);           // jmpq   *(%rbx,%valb,8)

Llzvn_table1:
    valb >>= 0x6;            // shr    $0x6,%valb
    _srcptr = _srcptr + valb + 0x2;  // lea    0x2(%_srcptr,%valb,1),%_srcptr
    
    // cmp    %_srcsizehandle,%_srcptr
	// ja     Llzvn_exit
    if (_srcptr > _srcsizehandle)
    {
        goto Llzvn_exit;
    }
    
    vale = vala;                // mov    %vala,%vale
    vale = OSSwapInt64(vale);  // bswap  %vale
    valc = vale;               // mov    %vale,%valc
    vale <<= 0x5;             // shl    $0x5,%vale
    valc <<= 0x2;             // shl    $0x2,%valc
    vale >>= 0x35;            // shr    $0x35,%vale
    valc >>= 0x3d;            // shr    $0x3d,%valc
    vala  >>= 0x10;            // shr    $0x10,%vala
    valc += 0x3;              // add    $0x3,%valc
    
Llzvn_l10:
    vald = retval + valb;   // lea    (%retval,%valb,1),%vald
    vald += valc;       // add    %valc,%vald
    
    // cmp    %_destsizehandle,%vald
	// jae    Llzvn_l8
    if (vald >= _destsizehandle)
    {
        goto Llzvn_l8;
    }
    
    // mov    %vala,(%_dstptr,%retval,1)
    addr = _dstptr + retval;
    memcpy((void *)addr, &vala, sizeof(vala));
    
    retval += valb;  // add    %valb,%retval
    vala = retval;   // mov    %retval,%vala
    
    // sub    %vale,%vala
	// jb     Llzvn_exit
    jmp = vala < vale ? 1 : 0;
    vala -= vale;

    if (jmp)
    {
        goto Llzvn_exit;
    }
    
    // cmp    $0x8,%vale
	// jb     Llzvn_l4
    if (vale < 0x8)
    {
        goto Llzvn_l4;
    }
    
Llzvn_l5:
    // mov    (%_dstptr,%vala,1),%valb
    addr = _dstptr + vala;
    valb = *((uint64_t *)addr);
    
    vala += 0x8;      // add    $0x8,%vala
    
    // mov    %valb,(%_dstptr,%retval,1)
    addr = _dstptr + retval;

    memcpy((void *)addr, &valb, sizeof(valb));

    retval += 0x8;     // add    $0x8,%retval
    
    // sub    $0x8,%valc
    // ja     Llzvn_l5
    jmp = valc > 0x8 ? 1 : 0;
    valc -= 0x8;

    if (jmp)
    {
        goto Llzvn_l5;
    }
    
    retval += valc;     // add    %valc,%retval
    
    vala = *(uint64_t *)_srcptr;    // mov    (%_srcptr),%vala
    valb = vala & 0xFF;           // movzbq (%_srcptr),%valb

    GOTO_LABEL(valb);           // jmpq   *(%rbx,%valb,8)

Llzvn_l8:
    // test   %valb,%valb
	// je     Llzvn_l7
    if (valb == 0) {
        goto Llzvn_l7;
    }
    
	vald = _destsizehandle + 0x8; // lea    0x8(%_destsizehandle),%vald
    
    
Llzvn_l6:
    // mov    %valab,(%_dstptr,%retval,1)
    addr = _dstptr + retval;
    byte_data = (unsigned char)(vala & 0xFF);
    memcpy((void *)addr, &byte_data, sizeof(byte_data));
    
	retval += 0x1; // add    $0x1,%retval
	
    // cmp    %retval,%vald
	// je     Llzvn_exit2
    if (retval == vald) {
        goto Llzvn_exit2;
    }
    
	vala >>= 0x8; // shr    $0x8,%vala
	
    // sub    $0x1,%valb
	// jne    Llzvn_l6
    jmp = valb != 0x1 ? 1 : 0;
    valb -= 1;
    if (jmp) {
        goto Llzvn_l6;
    }
    
    
Llzvn_l7:
    // mov    %retval,%vala
	vala = retval;
    
    // sub    %vale,%vala
	// jb     Llzvn_exit
    jmp = vala < vale ? 1 : 0;
    vala -= vale;
    if (jmp) {
        goto Llzvn_exit;
    }
    
Llzvn_l4:
    vald = _destsizehandle + 0x8; // lea    0x8(%_destsizehandle),%vald
    
    
Llzvn_l9:
    //  movzbq (%_dstptr,%vala,1),%valb
    addr = _dstptr + vala;
    byte_data = *((unsigned char *)addr);
    valb = byte_data;
    valb &= 0xFF;
    
	vala += 0x1; // add    $0x1,%vala
    
    //  mov    %valbb,(%_dstptr,%retval,1)
    addr = _dstptr + retval;
    byte_data = (unsigned char)valb;
    memcpy((void *)addr, &byte_data, sizeof(byte_data));
    
	retval += 0x1; // add    $0x1,%retval
	
    // cmp    %retval,%vald
	// je     Llzvn_exit2
    if (retval == vald) {
        goto Llzvn_exit2;
    }
    
	// sub    $0x1,%valc
    // jne    Llzvn_l9
    jmp = valc != 0x1 ? 1 : 0;
    valc -= 0x1;
    if (jmp) {
        goto Llzvn_l9;
    }
    
	vala = *(uint64_t *)_srcptr;    // mov    (%_srcptr),%vala
    valb = vala & 0xFF;           // movzbq (%_srcptr),%valb
    GOTO_LABEL(valb);           // jmpq   *(%rbx,%valb,8)
    
    
Llzvn_table0:
    valb >>= 0x6;           // shr    $0x6,%valb
	_srcptr = _srcptr + valb + 0x1; // lea    0x1(%_srcptr,%valb,1),%_srcptr
	
    // cmp    %_srcsizehandle,%_srcptr
	// ja     Llzvn_exit
    if (_srcptr > _srcsizehandle) {
        goto Llzvn_exit;
    }
    
	valc = 0x38;     // mov    $0x38,%valc
	valc &= vala;      // and    %vala,%valc
	vala >>= 0x8;     // shr    $0x8,%vala
	valc >>= 0x3;    // shr    $0x3,%valc
	valc += 0x3;     // add    $0x3,%valc
	goto Llzvn_l10; // jmp    Llzvn_l10
    
    
Llzvn_table3:
    valb >>= 0x6;           // shr    $0x6,%valb
	_srcptr = _srcptr + valb + 0x3; // lea    0x3(%_srcptr,%valb,1),%_srcptr
	
    // cmp    %_srcsizehandle,%_srcptr
	// ja     Llzvn_exit
    if (_srcptr > _srcsizehandle) {
        goto Llzvn_exit;
    }
    
	valc = 0x38;     // mov    $0x38,%valc
	vale = 0xFFFF;   // mov    $0xffff,%vale
	valc &= vala;      // and    %vala,%valc
	vala >>= 0x8;     // shr    $0x8,%vala
	valc >>= 0x3;    // shr    $0x3,%valc
	vale &= vala;      // and    %vala,%vale
	vala >>= 0x10;    // shr    $0x10,%vala
	valc += 0x3;     // add    $0x3,%valc
	goto Llzvn_l10; // jmp    Llzvn_l10
    
    
Llzvn_table6:
    valb >>= 0x3;           // shr    $0x3,%valb
	valb &= 0x3;            // and    $0x3,%valb
	_srcptr = _srcptr + valb + 0x3; // lea    0x3(%_srcptr,%valb,1),%_srcptr
	
    // cmp    %_srcsizehandle,%_srcptr
	// ja     Llzvn_exit
    if (_srcptr > _srcsizehandle)
    {
        goto Llzvn_exit;
    }
    
	valc = vala;       // mov    %vala,%valc
	valc &= 0x307;   // and    $0x307,%valc
	vala >>= 0xa;     // shr    $0xa,%vala
	
    // movzbq %valcb,%vale
    vale = valc & 0xFF;
    
	valc >>= 0x8;    // shr    $0x8,%valc
	vale <<= 0x2;    // shl    $0x2,%vale
	valc |= vale;     // or     %vale,%valc
	vale = 0x3FFF;   // mov    $0x3fff,%vale
	valc += 0x3;     // add    $0x3,%valc
	vale &= vala;      // and    %vala,%vale
	vala >>= 0xE;     // shr    $0xe,%vala

	goto Llzvn_l10; // jmp    Llzvn_l10
    
    
Llzvn_table10:
    _srcptr += 1; // add    $0x1,%_srcptr
	
    //cmp    %_srcsizehandle,%_srcptr
   	//ja     Llzvn_exit
	if (_srcptr > _srcsizehandle)
    {
        goto Llzvn_exit;
    }
    
    valc = vala;       // mov    %vala,%valc
	valc &= 0xF;     // and    $0xf,%valc

	goto Llzvn_l11; // jmp    Llzvn_l11
    
    
Llzvn_table9:
    _srcptr += 0x2; // add    $0x2,%_srcptr
	
    // cmp    %_srcsizehandle,%_srcptr
	// ja     Llzvn_exit
    if (_srcptr > _srcsizehandle)
    {
        goto Llzvn_exit;
    }
    
	valc = vala;    // mov    %vala,%valc
	valc >>= 0x8; // shr    $0x8,%valc
	valc &= 0xFF; // and    $0xff,%valc
	valc += 0x10; // add    $0x10,%valc
    
    
Llzvn_l11:
    vala = retval;        // mov    %retval,%vala
	vala -= vale;       // sub    %vale,%vala
	vald = retval + valc; // lea    (%retval,%valc,1),%vald
	
    // cmp    %_destsizehandle,%vald
	// jae    Llzvn_l4
    if (vald >= _destsizehandle) {
        goto Llzvn_l4;
    }
    
	// cmp    $0x8,%vale
	// jae    Llzvn_l5
    if (vale >= 0x8) {
        goto Llzvn_l5;
    }
    
	goto Llzvn_l4; // jmp    Llzvn_l4
    
    
Llzvn_table8:
    vala &= 0xF;            // and    $0xf,%vala
    _srcptr = _srcptr + vala + 0x1; // lea    0x1(%_srcptr,%vala,1),%_srcptr
    goto Llzvn_l0;        // jmp    Llzvn_l0
    
    
Llzvn_table7:
    vala >>= 0x8;           // shr    $0x8,%vala
	vala &= 0xFF;           // and    $0xff,%vala
	vala += 0x10;           // add    $0x10,%vala
	_srcptr = _srcptr + vala + 0x2; // lea    0x2(%_srcptr,%vala,1),%_srcptr
    
    
Llzvn_l0:
    // cmp    %_srcsizehandle,%_srcptr
	// ja     Llzvn_exit
    if (_srcptr > _srcsizehandle)
    {
        goto Llzvn_exit;
    }
    
    vald = retval + vala; // lea    (%retval,%vala,1),%vald
    vala = -vala;       // neg    %vala
    
    // cmp    %_destsizehandle,%vald
	// ja     Llzvn_l2
    if (vald > _destsizehandle)
    {
        goto Llzvn_l2;
    }
    
    vald = _dstptr + vald;  // lea    (%_dstptr,%vald,1),%vald
    
    
Llzvn_l1:
    // mov    (%_srcptr,%vala,1),%valb
    addr = _srcptr + vala;
    valb = *(uint64_t *)addr;
    
    // mov    %valb,(%vald,%vala,1)
    addr = vald + vala;
    *(uint64_t *)addr = valb;
    
    
    //
    //   TODO:
    //     Check if the jump condition is correctly set here.
    //
    
    // add    $0x8,%vala
    // jae    Llzvn_l1
    
    uiVal  = UINT64_MAX;
    uiVal -= (uint64_t)vala;
    
    vala += 0x8;
    
    jmp = (uiVal >= 0x8) ? 1 : 0;

    if (jmp)
    {
        goto Llzvn_l1;
    }
	
	retval = (size_t)vald;  // mov    %vald,%retval
	retval -= _dstptr; // sub    %_dstptr,%retval
	
    vala = *(uint64_t *)_srcptr;    // mov    (%_srcptr),%vala
    valb = vala & 0xFF;           // movzbq (%_srcptr),%valb
    GOTO_LABEL(valb);           // jmpq   *(%rbx,%valb,8)
    
Llzvn_l2:
    vald = _destsizehandle + 0x8; // lea    0x8(%_destsizehandle),%vald
    
Llzvn_l3:
    //  movzbq (%_srcptr,%vala,1),%valb
    addr = _srcptr + vala;
    valb = *((uint64_t *)addr);
    valb &= 0xFF;
    
    //  mov    %valbb,(%_dstptr,%retval,1)
    addr = _dstptr + retval;
    byte_data = (unsigned char)valb;
    memcpy((void *)addr, &byte_data, sizeof(byte_data));
    
    retval += 0x1; // add    $0x1,%retval
	
    // cmp    %retval,%vald
	// je     Llzvn_exit2
	if (vald == retval)
    {
        goto Llzvn_exit2;
    }
    
    // add    $0x1,%vala
	// jne    Llzvn_l3
    jmp = ((int64_t)vala + 0x1 == 0) ? 0 : 1;
    vala += 0x1;

    if (jmp)
    {
        goto Llzvn_l3;
    }
    
	vala = *(uint64_t *)_srcptr;    // mov    (%_srcptr),%vala
    valb = vala & 0xFF;          // movzbq (%_srcptr),%valb

    GOTO_LABEL(valb);          // jmpq   *(%rbx,%valb,8)
    
Llzvn_table5:
Llzvn_exit:
    retval = 0;
    
Llzvn_table2:
Llzvn_exit2:
    return retval;
}
