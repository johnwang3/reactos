/*
 * Various useful prototypes
 */

#ifndef __INCLUDE_INTERNAL_NTOSKRNL_H
#define __INCLUDE_INTERNAL_NTOSKRNL_H

#ifndef __ASM__

/*
 * Use these to place a function in a specific section of the executable
 */
#define PLACE_IN_SECTION(s)	__attribute__((section (s)))
#define INIT_FUNCTION		PLACE_IN_SECTION("init")
#define PAGE_LOCKED_FUNCTION	PLACE_IN_SECTION("pagelk")
#define PAGE_UNLOCKED_FUNCTION	PLACE_IN_SECTION("pagepo")

#ifdef _NTOSKRNL_

#include "ke.h"
#include "i386/segment.h"
#include "i386/mm.h"
#include "i386/fpu.h"
#include "module.h"
#include "pool.h"
#include "ob.h"
#include "mm.h"
#include "ps.h"
#include "cc.h"
#include "io.h"
#include "po.h"
#include "se.h"
#include "ldr.h"
#include "kd.h"
#include "ex.h"
#include "xhal.h"
#include "v86m.h"
#include "ifs.h"
#include "port.h"
#include "nls.h"
#ifdef KDBG
#include "kdb.h"
#endif
#include "dbgk.h"
#include "trap.h"
#include "safe.h"
#include "tag.h"
#include "test.h"
#include "inbv.h"


#include <pshpack1.h>
/*
 * Defines a descriptor as it appears in the processor tables
 */
typedef struct _DESCRIPTOR
{
  ULONG a;
  ULONG b;
} IDT_DESCRIPTOR, GDT_DESCRIPTOR;

#include <poppack.h>

extern IDT_DESCRIPTOR KiIdt[256];
//extern GDT_DESCRIPTOR KiGdt[256];


VOID ExpInitializeEventImplementation(VOID);
VOID ExpInitializeEventImplementation(VOID);
VOID ExpInitializeEventPairImplementation(VOID);
VOID ExpInitializeSemaphoreImplementation(VOID);
VOID ExpInitializeMutantImplementation(VOID);
VOID ExpInitializeTimerImplementation(VOID);
VOID ExpInitializeProfileImplementation(VOID);
/*
 * Initalization functions (called once by main())
 */
VOID MmInitSystem(ULONG Phase, PLOADER_PARAMETER_BLOCK LoaderBlock, ULONG LastKernelAddress);
VOID IoInit(VOID);
VOID IoInit2(BOOLEAN BootLog);
VOID STDCALL IoInit3(VOID);
VOID ObInit(VOID);
VOID PsInit(VOID);
VOID CmInitializeRegistry(VOID);
VOID STDCALL CmInitHives(BOOLEAN SetupBoot);
VOID CmInit2(PCHAR CommandLine);
VOID CmShutdownRegistry(VOID);
BOOLEAN CmImportSystemHive(PCHAR ChunkBase, ULONG ChunkSize);
BOOLEAN CmImportHardwareHive(PCHAR ChunkBase, ULONG ChunkSize);
VOID KdInitSystem(ULONG Reserved, PLOADER_PARAMETER_BLOCK LoaderBlock);

BOOLEAN
FASTCALL
RtlpCreateUnicodeString(
   IN OUT PUNICODE_STRING UniDest,
   IN PCWSTR  Source,
   IN POOL_TYPE PoolType);
   
#endif
#endif /* __ASM__ */

/*
 *
 */
#define MM_STACK_SIZE             (3*4096)

#endif /* INCLUDE_INTERNAL_NTOSKRNL_H */
