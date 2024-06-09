#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "cache.h"

uint32_t truncator32(uint32_t data, size_t begin, size_t end) {
    assert(begin >= 0);
    assert(end >= begin);
    if(end >= 32) {
        return data >> begin;
    }
    return ((data & ((uint32_t)1 << end) - 1) >> begin);
}

uint64_t truncator64(uint64_t data, size_t begin, size_t end) {
    assert(begin >= 0);
    assert(end >= begin);
    if(end >= 64) {
        return data >> begin;
    }
    return ((data & ((uint64_t)1 << end) - 1) >> begin);
}

uint64_t read_from_byte_array(uint8_t* const arr, size_t size, size_t offset) {
    uint64_t res = 0;
    size_t shift = 0;
    assert(size <= 8); // We don't support reading >64 bits yet

    while(size > 0) {
        assert(offset < BLOCK_SIZE);
        res += ((uint64_t)(arr[offset])) << shift;
        shift += 8;
        offset++;
        size--;
    }

    return res;
}

void write_to_byte_array(uint8_t* arr, size_t size, size_t offset, uint64_t data) {
    assert(size <= 8); // We don't support reading >64 bits yet

    while(size > 0) {
        assert(offset < BLOCK_SIZE);
        arr[offset] = (uint8_t)data;
        data >>= 8;
        offset++;
        size--;
    }
}