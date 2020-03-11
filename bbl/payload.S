// See LICENSE for license details.

#include "config.h"
#include "encoding.h"

  .section ".payload","a",@progbits

#if RELAXED_ALIGNMENT
  /* align payload minimally */
  .align 3
#else
  /* align payload to megapage */
  .align RISCV_PGSHIFT + RISCV_PGLEVEL_BITS
#endif

  .globl _payload_start, _payload_end
_payload_start:
  .incbin BBL_PAYLOAD
_payload_end:

  /* this gets copied later */
  .align 3

  .globl _sel4_payload_start, _sel4_payload_end
_sel4_payload_start:
  .incbin BBL_PAYLOAD_SEL4
_sel4_payload_end:

