#include "block.h"

void block_copy_fw(uint64_t dst, uint64_t src, uint32_t len)
{
    for (uint32_t n = 0; n < len; n++)
        *(volatile uint8_t *)(uintptr_t)(dst + n) = rd8(src + n);
}
