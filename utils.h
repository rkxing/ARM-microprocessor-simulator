#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdint.h>
#include <stddef.h>

// Note: begin starts at 0, and end is EXCLUSIVE (!)
// mnemonic: end-begin should be the same as the data length
uint32_t truncator32(uint32_t data, size_t begin, size_t end);
uint64_t truncator64(uint64_t data, size_t begin, size_t end);

// size and offset are in bytes relative to arr[0]
// Warning: This does not check if offset + size
// may exceed the cache line boundary.
// For lab 4 it's probably fine to use it without
// extra check, as we're allowed to assume all
// memory accesses are aligned.
// Warning: these functions assume we are little endian
uint64_t read_from_byte_array(uint8_t* const arr, size_t size, size_t offset);
void write_to_byte_array(uint8_t* arr, size_t size, size_t offset, uint64_t data);

#endif //#ifndef _UTILS_H_