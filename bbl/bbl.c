// See LICENSE for license details.

#define ENABLE_SEL4		1
#define ENABLE_LINUX	1

#include "bbl.h"
#include "mtrap.h"
#include "atomic.h"
#include "vm.h"
#include "bits.h"
#include "config.h"
#include "fdt.h"
#include <string.h>

extern char _payload_start, _payload_end; /* internal payload */
static const void* entry_point;
long disabled_hart_mask;

extern char _payload2_start, _payload2_end; /* second internal payload */
static uintptr_t sel4_base;
static uintptr_t sel4_size;
static uintptr_t linux_base;
static uintptr_t linux_size;

static uintptr_t dtb_output()
{
  /*
   * Place DTB after the payload, either the internal payload or a
   * preloaded external payload specified in device-tree, if present.
   *
   * Note: linux kernel calls __va(dtb) to get the device-tree virtual
   * address. The kernel's virtual mapping begins at its load address,
   * thus mandating device-tree is in physical memory after the kernel.
   */
  uintptr_t end = linux_base + linux_size;
  return (end + MEGAPAGE_SIZE - 1) / MEGAPAGE_SIZE * MEGAPAGE_SIZE;
}

static void filter_dtb(uintptr_t source)
{
  uintptr_t dest = dtb_output();
  uint32_t size = fdt_size(source);
  memcpy((void*)dest, (void*)source, size);

  // Remove information from the chained FDT
  filter_harts(dest, &disabled_hart_mask);
  filter_plic(dest);
  filter_compat(dest, "riscv,clint0");
  filter_compat(dest, "riscv,debug-013");
}

static void protect_memory_sel4(void)
{
  // Check to see if up to five PMP registers are implemented.
  // Ignore the illegal-instruction trap if PMPs aren't supported.
  uintptr_t a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, tmp, cfg;
  asm volatile ("la %[tmp], 1f\n\t"
				"csrrw %[tmp], mtvec, %[tmp]\n\t"
				"csrw pmpaddr0, %[m1]\n\t"
				"csrr %[a0], pmpaddr0\n\t"
				"csrw pmpaddr1, %[m1]\n\t"
				"csrr %[a1], pmpaddr1\n\t"
				"csrw pmpaddr2, %[m1]\n\t"
				"csrr %[a2], pmpaddr2\n\t"
				"csrw pmpaddr3, %[m1]\n\t"
				"csrr %[a3], pmpaddr3\n\t"
				"csrw pmpaddr4, %[m1]\n\t"
				"csrr %[a4], pmpaddr4\n\t"
				".align 2\n\t"
				"1: csrw mtvec, %[tmp]"
				: [tmp] "=&r" (tmp),
				  [a0] "+r" (a0), [a1] "+r" (a1), [a2] "+r" (a2), [a3] "+r" (a3), [a4] "+r" (a4)
				: [m1] "r" (-1UL));

  // We need at least five PMP registers to protect us and Linux from S-mode SEL4.
  if (!(a0 & a1 & a2 & a3 & a4))
	die("not enough pmp registers!\r\n");

  // Prevent S-mode access to our part of memory.
  extern char _ftext, _end;
  a0 = (uintptr_t)&_ftext >> PMP_SHIFT;
  a1 = (uintptr_t)&_end >> PMP_SHIFT;
  cfg = PMP_TOR << 8;

  // Prevent S-mode access to Linux memory.
  uintptr_t base = linux_base - MEGAPAGE_SIZE;
  a2 = base >> PMP_SHIFT;
  a3 = (base + mem_size) >> PMP_SHIFT;
  cfg |= PMP_TOR << 24;

  // Give S-mode free rein of everything else.
  a4 = -1;
  cfg |= (uintptr_t)(PMP_NAPOT | PMP_R | PMP_W | PMP_X) << 32;

  // Plug it all in.
  asm volatile ("csrw pmpaddr0, %[a0]\n\t"
				"csrw pmpaddr1, %[a1]\n\t"
				"csrw pmpaddr2, %[a2]\n\t"
				"csrw pmpaddr3, %[a3]\n\t"
				"csrw pmpaddr4, %[a4]\n\t"
				"csrw pmpcfg0, %[cfg]"
				:: [a0] "r" (a0), [a1] "r" (a1), [a2] "r" (a2), [a3] "r" (a3), [a4] "r" (a4),
				   [cfg] "r" (cfg));
}

static void protect_memory_linux(void)
{
  // Check to see if up to five PMP registers are implemented.
  // Ignore the illegal-instruction trap if PMPs aren't supported.
  uintptr_t a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, tmp, cfg;
  asm volatile ("la %[tmp], 1f\n\t"
                "csrrw %[tmp], mtvec, %[tmp]\n\t"
                "csrw pmpaddr0, %[m1]\n\t"
                "csrr %[a0], pmpaddr0\n\t"
                "csrw pmpaddr1, %[m1]\n\t"
                "csrr %[a1], pmpaddr1\n\t"
                "csrw pmpaddr2, %[m1]\n\t"
                "csrr %[a2], pmpaddr2\n\t"
                "csrw pmpaddr3, %[m1]\n\t"
                "csrr %[a3], pmpaddr3\n\t"
                "csrw pmpaddr4, %[m1]\n\t"
                "csrr %[a4], pmpaddr4\n\t"
                ".align 2\n\t"
                "1: csrw mtvec, %[tmp]"
                : [tmp] "=&r" (tmp),
                  [a0] "+r" (a0), [a1] "+r" (a1), [a2] "+r" (a2), [a3] "+r" (a3), [a4] "+r" (a4)
                : [m1] "r" (-1UL));

  // We need at least five PMP registers to protect us and SEL4 from S-mode Linux.
  if (!(a0 & a1 & a2 & a3 & a4))
	die("not enough pmp registers!\r\n");

  // Prevent S-mode access to our part of memory.
  extern char _ftext, _end;
  a0 = (uintptr_t)&_ftext >> PMP_SHIFT;
  a1 = (uintptr_t)&_end >> PMP_SHIFT;
  cfg = PMP_TOR << 8;

  // Prevent S-mode access to SEL4 memory.
  a2 = sel4_base >> PMP_SHIFT;
  a3 = (sel4_base + sel4_size) >> PMP_SHIFT;
  cfg |= PMP_TOR << 24;

  // Give S-mode free rein of everything else.
  a4 = -1;
  cfg |= (uintptr_t)(PMP_NAPOT | PMP_R | PMP_W | PMP_X) << 32;

  // Plug it all in.
  asm volatile ("csrw pmpaddr0, %[a0]\n\t"
                "csrw pmpaddr1, %[a1]\n\t"
                "csrw pmpaddr2, %[a2]\n\t"
                "csrw pmpaddr3, %[a3]\n\t"
		        "csrw pmpaddr4, %[a4]\n\t"
                "csrw pmpcfg0, %[cfg]"
                :: [a0] "r" (a0), [a1] "r" (a1), [a2] "r" (a2), [a3] "r" (a3), [a4] "r" (a4),
                   [cfg] "r" (cfg));
}

static void do_nothing()
{
  for(;;);
}

void boot_other_hart(uintptr_t unused __attribute__((unused)))
{
  const void* entry;

  do {
    entry = entry_point;
    mb();
  } while (!entry);

  long hartid = read_csr(mhartid);
  if ((1 << hartid) & disabled_hart_mask) {
    printm("(disabled hart)\r\n");
    while (1) {
      __asm__ volatile("wfi");
#ifdef __riscv_div
      __asm__ volatile("div x0, x0, x0");
#endif
    }
  }

  //TODO: should be notified after sel4 start. for now just delay the startup.
  for (int i = 0; i < 10000000; i++) printm("");

  printm("starting linux payload at 0x%lx..\r\n", entry);

#ifdef BBL_BOOT_MACHINE
  enter_machine_mode(entry, hartid, dtb_output());
#else /* Run bbl in supervisor mode */
  protect_memory_linux();
#if ENABLE_LINUX
  enter_supervisor_mode(entry, hartid, dtb_output());
#else
  enter_supervisor_mode((void*)do_nothing, hartid, dtb_output());
#endif
#endif
}

void boot_loader(uintptr_t dtb)
{
  long hartid = read_csr(mhartid);
  sel4_base = (uintptr_t)&_payload_start;
  sel4_size = &_payload_end - &_payload_start;
  linux_base = BBL_MEM_START + BBL_SEL4_MEMSIZE + MEGAPAGE_SIZE;
  linux_size = &_payload2_end - &_payload2_start;
  memcpy((void*)linux_base, &_payload2_start, linux_size);
  filter_dtb(dtb);
#ifdef PK_ENABLE_LOGO
  print_logo();
#endif
#ifdef PK_PRINT_DEVICE_TREE
  fdt_print(dtb_output());
#endif
  mb();
  printm("starting sel4 payload at 0x%lx..\r\n", &_payload_start);
  protect_memory_sel4();
  entry_point = (void*)linux_base; // start other harts
  // SEL4 has its own builtin dtb, so pass 0
#if ENABLE_SEL4
  enter_supervisor_mode((void*)sel4_base, hartid, 0);
#else
  enter_supervisor_mode((void*)do_nothing, hartid, 0);
#endif
}
