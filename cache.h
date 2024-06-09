#ifndef _CACHE_H_
#define _CACHE_H_

#include "pipe.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>

#define INST_MISS_DELAY 10
#define DATA_MISS_DELAY 10
#define BLOCK_SIZE 32
#define LOG_BLOCK_SIZE 5

extern uint64_t timestamp_counter;
typedef struct
{
    int valid_bit;
    uint64_t tag;
    uint64_t used_timestamp;
    // We use a 64-bit integer for each cache line, together with a global 64-bit
    // timestamp_counter, to keep track of which line is recently used.
    // The global counter is initialized to 0, and every time *any* line is used,
    // the counter is bumped up by 1, representing a uni-direction flow of time.
    // The used_timestamp is then set to the new value of the global counter.
    // Now whenever we need to evict a line, we search and find the one with
    // the oldest used_timestamp. This new line is now the most-recently used,
    // so its used_timestamp is also updated to the global timestamp++
    // 64 bits is large enough regarding concerns of overflow--on a machine that
    // reads into the cache 10^12 times a second, it takes a few hundred years
    // to hit an overflow, and it's only a problem if some other cache line sits
    // there neither used nor evicted over the hundreds of years.
    uint8_t data[BLOCK_SIZE]; // Each block is specified to be 32 bytes
                              // (able to hold 8 inst or 8 data words)
} cache_line_t;

typedef struct
{
    int num_sets;
    int num_lines; // lines/blocks per set
    cache_line_t** lines; // the row index of this array is the set index
                          // aka this is a num_sets x num_lines 2D array
} cache_t;

extern cache_t *i_cache, *d_cache;

// The following structs keep track of data read requests in cases of misses
typedef struct
{
    uint64_t addr;        // For identifying the right entry for the query
    int remaining_cycles; // When this reaches 0, the data is ready
                          // This should be SIGNED, as negative values are
                          // actively used to represent orphaned queries.
    uint64_t data;        // Can be garbage if !ready
    cache_line_t *c_line; // For the convenience of cache_write_handler which
                          // calls cache_read_handler; can be garbage if !ready
} query_state_t;

typedef struct query_state_list_t
{
    query_state_t *state;
    struct query_state_list_t *prev;
    struct query_state_list_t *next;
} query_state_list_t;

typedef struct query_state_list_heads_list_t
{
    cache_t *cache; // For identifying the right entry for the cache
    query_state_list_t *head;
    //query_state_list_heads_list_t *prev; // We don't really need this
    struct query_state_list_heads_list_t *next;
} query_state_list_heads_list_t;

extern query_state_list_heads_list_t *global_query_state_list_heads_list_head;

// Visualization of these linked lists:
/*
  q  q  q  q
  ^  ^  ^  ^
  |  |  |  |
  q  q  q  q
  ^  ^  ^  ^
  |  |  |  |
->c->c->c->c
*/
// where each c is a query_list_heads_list_t,
// and each q is a query_state_list_t


void cache_init_all();

// Responsible for mallocating the cache struct
// as well as register it in the global_query_list_heads_list
cache_t* cache_new(int sets, int ways, int block);

// Call this before the program terminates
void cache_destroy_all();

// Deallocates one cache struct
// and removes it from global_query_list_heads_list
void cache_destroy(cache_t *c);


void cache_update_timestamp(cache_line_t* line);
// Get data from memory, update the corresponding query_state_t entry,
// and return the data.
cache_line_t* cache_allocate(cache_t* c, uint64_t addr);

// Should be called once each cycle to decrement the
// remaining_cycles in each query_state_list_t entry.
void cache_refresh_query_states();

void cache_cancel(cache_t *c, uint64_t addr);

// This function wraps mem_read_32. From now on, pipe.c should always call this
// function in place of the "raw" mem_read_32. `size` is in terms of bytes.
//
// When a miss happens, it allocates and initializes a new linked list
// entry in the query_state_list, and returns this entry. With each
// subsequent call, it returns this same entry identified by addr, but the
// remaining_cycles will have been decremented--all until remaining_cycle hits
// 0, when the data field will be populated and the entry will be purged.
// (Needing to purge the entry before returning is the reason we return a
// struct rather than a ptr to it.)
query_state_t cache_read_handler(cache_t *c, uint64_t addr, size_t size);
query_state_t cache_write_handler(cache_t *c, uint64_t addr, size_t size, uint64_t data);

// Write the cache contents to the memory
// As we have a write-through cache, this is called
// in cache_write_handler and nowhere else.
// Note: as of the writeup, this function assumes addr is correct
void cache_sync_to_mem(cache_t *c, cache_line_t *c_line, uint64_t addr);

#endif