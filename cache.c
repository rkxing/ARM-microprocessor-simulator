#include "cache.h"
#include "shell.h" // Mostly for mem_read_32
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>

cache_t *i_cache, *d_cache;

uint64_t timestamp_counter = 0;

query_state_list_heads_list_t *global_query_state_list_heads_list_head = NULL;

void cache_init_all() {
    i_cache = cache_new(64, 4, 32);
    d_cache = cache_new(256, 8, 32);
    global_query_state_list_heads_list_head = (query_state_list_heads_list_t*)malloc(sizeof(query_state_list_heads_list_t));
    if (global_query_state_list_heads_list_head == NULL) {
        printf("malloc failed to init global_query_state_list_heads_list_head\n");
        exit(1);
    }
    global_query_state_list_heads_list_head->cache = i_cache;
    global_query_state_list_heads_list_head->head = NULL;
    global_query_state_list_heads_list_head->next = (query_state_list_heads_list_t*)malloc(sizeof(query_state_list_heads_list_t));
    global_query_state_list_heads_list_head->next->cache = d_cache;
    global_query_state_list_heads_list_head->next->head = NULL;
    global_query_state_list_heads_list_head->next->next = NULL;
}

cache_t *cache_new(int sets, int ways, int block)
{
    cache_t *new_cache = (cache_t*)malloc(sizeof(cache_t));
    new_cache->num_sets = sets;
    new_cache->num_lines = block;

    new_cache->lines = (cache_line_t**)malloc(sizeof(cache_line_t*) * sets);
    for (int i = 0; i < sets; i++) {
        // we use calloc here to init cache_line vals to 0
        new_cache->lines[i] = (cache_line_t*)calloc(block, sizeof(cache_line_t));
    }

    return new_cache;
}

void cache_destroy_all() {
    // We need to destroy both the cache (*proper*) structs
    // and the query state structs.

    // Cache structs
    cache_destroy(i_cache);
    cache_destroy(d_cache);

    // Query structs
    query_state_list_heads_list_t *l_of_l_ptr = global_query_state_list_heads_list_head;
    while(l_of_l_ptr != NULL) {
        // Logically all reads should be finished,
        // so we must have a bug if this is not empty.
        assert(l_of_l_ptr->head == NULL);

        query_state_list_heads_list_t *next = l_of_l_ptr->next;
        free(l_of_l_ptr);
        l_of_l_ptr = next;
    }
}

void cache_destroy(cache_t *c)
{
    for (int i = 0; i < c->num_sets; i++)
        free(c->lines[i]);
    free(c->lines);
    free(c);
}


/*
* this function searches for the requested data in the cache. on a hit, this data
* is immediately returned to the calling pipeline stage. on a miss, this function
* returns NULL and the pipeline should begin a 10-cycle stall
*/
cache_line_t* search_cache(cache_t *c, uint64_t addr) {
    int set_idx = truncator64(addr, 5, 11);
    uint64_t tag = truncator64(addr, 11, 64);
    cache_line_t* curr;

    for (int i = 0; i < c->num_lines; i++) {
        curr = &c->lines[set_idx][i];
        if ((curr->tag == tag) && curr->valid_bit)
            return curr;
    }

    return NULL;
}


/*
 * called in two cases:
 * 1) on cache hit
 * 2) on cache miss inside the cache_allocate function
 *    since a newly-introduced line becomes the MRU line
 */
void cache_update_timestamp(cache_line_t* line)
{
    line->used_timestamp = ++timestamp_counter;
}

/*
 * called on cache miss after 10th stalled cycle to update cache with a new line
 */
cache_line_t* cache_allocate(cache_t* c, uint64_t addr) {
    int set_idx = truncator64(addr, 5, 11);
    uint64_t tag = truncator64(addr, 11, 64);
    cache_line_t *lru_line = &c->lines[set_idx][0];

    uint64_t mask = (uint64_t)-1 << LOG_BLOCK_SIZE;
    addr &= mask; // The starting addr should be the requested addr "rounded down"

    for (int i = 1; i < c->num_lines; i++) {
        if (lru_line->used_timestamp > c->lines[set_idx][i].used_timestamp)
            lru_line = &c->lines[set_idx][i];
    }

    lru_line->valid_bit = 1;
    lru_line->tag = tag;
    cache_update_timestamp(lru_line);
    for (int i = 0; i < BLOCK_SIZE; i += 4) {
        write_to_byte_array(lru_line->data, 4, i, mem_read_32(addr + i));
    }

    return lru_line;
}


void cache_refresh_query_states() {
    query_state_list_heads_list_t *l_of_l_ptr = global_query_state_list_heads_list_head;
    while(l_of_l_ptr != NULL) {
        query_state_list_t *l_ptr = l_of_l_ptr->head;

        // To make the timings correct, we set wait_d_cache
        // in this func, rather than pipe_stage_mem
        // Warning: this manner of checking assumes that
        // the main pipe stall any time there's at least one
        // query entry. For a different arch this may be false.
        if(l_of_l_ptr->cache == d_cache) {
            if(l_ptr == NULL) {
                printf("wait_d_cache being turned off\n");
                wait_d_cache = false;
            } else {
                wait_d_cache = true;
            }
        }

        while(l_ptr != NULL) {
            l_ptr->state->remaining_cycles--;

            // Changed from previous implementation. The current implementation is such that
            // cache_refresh_query_states is responsible for both decrementing the timers
            // and allocating (and populating) cache lines when timer is 0. This is only cancelled
            // if cache_cancel is explicitly called by the core pipeline.
            assert(l_ptr->state->remaining_cycles >= 0);
            if(l_ptr->state->remaining_cycles == 0) {

                uint64_t addr = l_ptr->state->addr;
                cache_t *c = l_of_l_ptr->cache;
                cache_line_t *c_line = cache_allocate(c, addr); // c_line won't be actually used as of now

                // Purge the query entry
                query_state_list_t *l_next = l_ptr->next;
                if(l_ptr->prev == NULL) {
                    l_of_l_ptr->head = l_next;
                }
                else {
                    l_ptr->prev->next = l_next;
                }
                if (l_next != NULL) {
                    l_next->prev = l_ptr->prev;
                }
                free(l_ptr->state);
                l_ptr->state = NULL;
                free(l_ptr);
                l_ptr = l_next;
            }
            else {
                l_ptr = l_ptr->next;
            }
        }

        l_of_l_ptr = l_of_l_ptr->next;
    }
}

void cache_cancel(cache_t *c, uint64_t addr)
{
    query_state_list_heads_list_t *l_of_l_ptr = global_query_state_list_heads_list_head;
    while(l_of_l_ptr != NULL && l_of_l_ptr->cache != c) {
        l_of_l_ptr = l_of_l_ptr->next;
    }

    assert(l_of_l_ptr != NULL);
    // Throw if c is not a recognized cache.

    query_state_list_t *l_ptr = l_of_l_ptr->head;
    uint64_t mask = (uint64_t)-1 << LOG_BLOCK_SIZE;
    while(l_ptr != NULL && ((l_ptr->state->addr & mask) != (addr & mask))) {
        l_ptr = l_ptr->next;
    }

    if(l_ptr == NULL) {
        // The request to cancel does not exist--nothing to do
        printf("Did not find entry to purge.\n");
        return;
    }
        printf("Found entry to purge.\n");

    // Simply purge the query entry
    if(l_ptr->prev == NULL) {
        l_of_l_ptr->head = l_ptr->next;
    }
    else {
        l_ptr->prev->next = l_ptr->next;
    }
    free(l_ptr->state);
    l_ptr->state = NULL;
    free(l_ptr);
    l_ptr = NULL;
}


query_state_t cache_read_handler(cache_t *c, uint64_t addr, size_t size)
{
    query_state_list_heads_list_t *l_of_l_ptr = global_query_state_list_heads_list_head;
    while(l_of_l_ptr != NULL && l_of_l_ptr->cache != c) {
        l_of_l_ptr = l_of_l_ptr->next;
    }

    assert(l_of_l_ptr != NULL);
    // Throw if c is not a recognized cache.

    query_state_list_t *l_ptr = l_of_l_ptr->head;
    uint64_t mask = (uint64_t)-1 << LOG_BLOCK_SIZE;
    while(l_ptr != NULL && (l_ptr->state->addr & mask) != (addr & mask)) {
        l_ptr = l_ptr->next;
    }

    query_state_t result;
    if(l_ptr != NULL) { // This is a miss that is already in the "load queue".
        result = *l_ptr->state;
        if(l_of_l_ptr->cache == i_cache)
            printf("icache bubble (%d) at cycle %d\n", result.remaining_cycles, stat_cycles+1);
        else if(l_of_l_ptr->cache == d_cache)
            printf("dcache stall (%d) at cycle %d\n", result.remaining_cycles, stat_cycles+1);
        else
            assert(0);
        if(result.remaining_cycles == 0) {
            // Read bytes out of the array of bytes in the cache line
            // based on the offset derived from the addr.
            // We make the big assumption here that the address is aligned,
            // i.e. the bytes of the data won't exceed the cache line boundary.
            //cache_line_t *c_line = cache_allocate(c, addr);
            // Change: moved the cache_allocate invocation to cache_refresh_query_states
            //         Now we can assume it's already allocated.
            cache_line_t *c_line = search_cache(c, addr);
            assert(c_line != NULL);
            uint64_t offset = truncator64(addr, 0, 5);
            result.data = read_from_byte_array(
                c_line->data,
                size,
                offset
            );
            result.c_line =c_line;

            // Purge the query entry
            if(l_ptr->prev == NULL) {
                l_of_l_ptr->head = l_ptr->next;
            }
            else {
                l_ptr->prev->next = l_ptr->next;
            }
            free(l_ptr->state);
            l_ptr->state = NULL;
        }
    } else {
        // This is new read request. We first try to find the data in the cache.
        // In the case of a miss, we need to create a new query entry, and let
        // the calling pipeline stage know it needs to stall.
        cache_line_t *c_line = search_cache(c, addr);
        if(c_line != NULL) { // Cache hit
            if(l_of_l_ptr->cache == i_cache)
                printf("icache hit (0x%lx) at cycle %d\n", addr, stat_cycles+1);
            else if(l_of_l_ptr->cache == d_cache)
                printf("dcache hit (0x%lx) at cycle %d\n", addr, stat_cycles+1);
            else
                assert(0);
            result.addr = addr;
            result.remaining_cycles = 0;
            size_t shift = 0;
            uint64_t offset = truncator64(addr, 0, 5);
            result.data = read_from_byte_array(c_line->data, size, offset);
            result.c_line = c_line;
        }
        else { // Cache miss
            if(l_of_l_ptr->cache == i_cache)
                printf("icache miss (0x%lx) at cycle %d\n", addr, stat_cycles+1);
            else if(l_of_l_ptr->cache == d_cache)
                printf("dcache miss (0x%lx) at cycle %d\n", addr, stat_cycles+1);
            else
                assert(0);
            l_ptr = (query_state_list_t*)malloc(sizeof(query_state_list_t));
            l_ptr->state = (query_state_t*)malloc(sizeof(query_state_t));
            l_ptr->state->addr = addr;
            if(c == i_cache) {
                l_ptr->state->remaining_cycles = INST_MISS_DELAY;
            }
            else if(c == d_cache) {
                l_ptr->state->remaining_cycles = DATA_MISS_DELAY;
            }
            else {
                assert(0);
            }
            // l_ptr->state->data just remains garbage
            l_ptr->state->c_line = NULL; // This could also remain garbage,
                                         // but we explicitlyset it to NULL
                                         // so bugs result in crashes and are
                                         // easier to catch.
            l_ptr->prev = NULL;
            l_ptr->next = l_of_l_ptr->head;
            l_of_l_ptr->head = l_ptr;

            result = *l_ptr->state;
        }
    }

    return result;
}

// The "concrete data" of the return value does not matter,
// but the "metadata" does: the caller will inspect
// the return value's remaining_cycles
query_state_t cache_write_handler(cache_t *c, uint64_t addr, size_t size, uint64_t data) {
    query_state_t q = cache_read_handler(c, addr, size);
    size_t offset = truncator64(addr, 0, 5);

    if(q.remaining_cycles == 0) {
        assert(q.c_line!= NULL);
        write_to_byte_array(q.c_line->data, size, offset, data);
        cache_sync_to_mem(c, q.c_line, addr);
    }

    return q;
}

void cache_sync_to_mem(cache_t *c, cache_line_t *c_line, uint64_t addr) {
    assert(c == d_cache);
    // If not, we'll need to take different tag bits etc.

    assert(c_line->tag == truncator64(addr, 11, 64));
    // Ideally we also check the set is correct, but it's
    // pretty trick to infer the set number from a cache_line_t
    // pointer. We consider changing our implementation to
    // use full address in the tag in the future.

    assert(BLOCK_SIZE % 4 == 0);
    // If not, some more code will be necessary.

    addr &= 0xFFFFFFFFFFFFF800;
    size_t shift = 0;
    for(int i=0; i < BLOCK_SIZE/4; i++) {
        uint32_t num = 0;
        for(int j=0; j < 4; j++) {
            num += c_line->data[i*4 + j] << shift;
            shift += 8;
        }
        mem_write_32(addr, num);
        addr += 4;
    }
}