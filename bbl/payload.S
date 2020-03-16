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

  .globl _payload2_start, _payload2_end
_payload2_start:
  .incbin BBL_PAYLOAD2
_payload2_end:

