#ifndef H_COMMON
#define H_COMMON

#define MAXPATHLEN 1024

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "lv2.h"

#define HVSC_SYSCALL			811                  	// which syscall to overwrite with hvsc redirect
#define HVSC_SYSCALL_ADDR_355	0x8000000000195540ULL	// where above syscall is in lv2 3.55
#define HVSC_SYSCALL_ADDR_341	0x80000000001BB414ULL	// where above syscall is in lv2 3.41

#define NEW_POKE_SYSCALL		813                  	// which syscall to overwrite with new poke
#define NEW_POKE_SYSCALL_ADDR_355 0x8000000000195A68ULL	// where above syscall is in lv2 3.55
#define NEW_POKE_SYSCALL_ADDR_341 0x80000000001BB93CULL // where above syscall is in lv2 3.41

#define SYSCALL_TABLE_355		0x8000000000346570ULL	// 3.55
#define SYSCALL_TABLE_341		0x80000000002EB128ULL	// 3.41
#define SYSCALL_PTR(n)			(SYSCALL_TABLE + 8 * (n))

#define HV_BASE					0x8000000014000000ULL	// where in lv2 to map lv1
#define HV_SIZE					0x001000				// 0x1000 (we need 4k from lv1 only)
#define HV_PAGE_SIZE			0x0c					// 4k = 0x1000 (1 << 0x0c)
#define	HV_START_OFFSET			0x363000				// remove lv2 protection
#define HV_OFFSET				0x000a78				// at address 0x363a78

#define	HV_START_OFFSET2		0x16f000				// set lv2 access rights for sys_storage
														// at address 0x16f3b8
extern u64 HVSC_SYSCALL_ADDR;
extern u64 NEW_POKE_SYSCALL_ADDR;
extern u64 SYSCALL_TABLE;

#endif

