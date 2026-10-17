#ifndef BLOCK_H
#define BLOCK_H

#include "z63fw.h"

/* Bytes from the firmware's memory into the program's block. */
void block_copy_fw(uint64_t dst, uint64_t src, uint32_t len);

#endif /* BLOCK_H */
