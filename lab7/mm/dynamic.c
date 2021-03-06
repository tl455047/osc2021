#include "dynamic.h"
#include <buddy.h>

void dynamic_init() {
  if(dynamic_system.top_chunk == null) {
    //request page frame from buddy
    dynamic_system.top_chunk = buddy_malloc(PAGE_SIZE);
    dynamic_system.top_chunk->size = PAGE_SIZE - DYNAMIC_CHUNK_HEADER_OFFSET;
    dynamic_system.top_chunk->next = null;
    dynamic_system.top_chunk->prev_size = 0;
  }
}

void* dynamic_malloc(size_t size) {
  void* chunk;
  int bin_idx;
  //get nearest bin size from size
  for(bin_idx = 0; size > (bin_idx + 1) * DYNAMIC_BIN_MIN_SLOT; bin_idx++);
  
  if((bin_idx + 1) * DYNAMIC_BIN_MIN_SLOT < DYNAMIC_BIN_MIN_SLOT) {
    uart_puts("size too small need to be larger than 0x10.\n");
    return null;
  }
  //bin size should be smaller than maximum bin size
  if(bin_idx > DYNAMIC_BIN_MAX) {
    uart_puts("request memory larger than memory pool.\n");
    return null;
  }
  //check unsorted bin
  chunk = dynamic_unsorted_bin_malloc(bin_idx);
  //check bin list
  if(chunk == null) {
    chunk = dynamic_find_free_chunk(bin_idx);
  }
  //check top chunk
  if(chunk == null) {
    if(dynamic_system.top_chunk->size < (bin_idx + 1) * DYNAMIC_BIN_MIN_SLOT) {
      //request new page frame from buddy
      if(dynamic_request_new_page() == -1) {
        return null; 
      }
    }
    //allocate from top chunk
    //uart_puts("allocate from top chunk.\n");
    chunk = dynamic_top_chunk_malloc(bin_idx);
  }
  
  if(chunk != null) {
    //initialize
    memset(chunk + DYNAMIC_CHUNK_HEADER_OFFSET, (bin_idx + 1) * DYNAMIC_BIN_MIN_SLOT, 0);
  }
  
  return chunk + DYNAMIC_CHUNK_HEADER_OFFSET;
  //return chunk;
}

void* dynamic_find_free_chunk(int idx) {
  void* chunk;
  //struct dynamic_chunk* split_chunk;
  //int size, split_size, find_idx, split_idx;
  chunk = null;
  //while(find_idx < DYNAMIC_BIN_MAX) {
  if(dynamic_system.bins[idx] != null) {
    //find free chunk
    chunk = dynamic_system.bins[idx];
    dynamic_system.bins[idx] = dynamic_system.bins[idx]->next;
    //set chunk header
    ((struct dynamic_chunk *)chunk)->size = (idx + 1) * DYNAMIC_BIN_MIN_SLOT + 1; //inuse bit
    ((struct dynamic_chunk *)chunk)->next = null; 
    /*if(find_idx > idx) {
      split_idx = (find_idx - idx) - 1 - 2; //exclude header offset
      split_size = (split_idx + 1) * DYNAMIC_BIN_MIN_SLOT;
      //check rest of chunk is enough to be a new chunk
      if(split_size >= DYNAMIC_BIN_MIN_SLOT * 2) {
        //split chunk 
        size = (idx + 1) * DYNAMIC_BIN_MIN_SLOT;
        ((struct dynamic_chunk *)chunk)->size = size + 1; //inuse bit
        
        split_chunk = (void *)chunk + size + DYNAMIC_CHUNK_HEADER_OFFSET;
        split_chunk->size = split_size;
        split_chunk->prev_size = size;
        //put into bin list
        split_chunk->next = dynamic_system.bins[split_idx];
        dynamic_system.bins[split_idx] = split_chunk;
      }*/
    //}
    //return chunk;
  }
  //find_idx++;
  //}
  return chunk;
}

void* dynamic_top_chunk_malloc(int idx) {
  void* chunk;
  size_t chunk_size, top_chunk_size;
  top_chunk_size = dynamic_system.top_chunk->size;
  chunk = dynamic_system.top_chunk;
  chunk_size = (idx + 1) * DYNAMIC_BIN_MIN_SLOT;
  dynamic_system.top_chunk->size = chunk_size + 1; //inuse bit
  dynamic_system.top_chunk = (void *)chunk + chunk_size + DYNAMIC_CHUNK_HEADER_OFFSET;
  dynamic_system.top_chunk->size = top_chunk_size - chunk_size - DYNAMIC_CHUNK_HEADER_OFFSET;
  dynamic_system.top_chunk->next = null;
  dynamic_system.top_chunk->prev_size = chunk_size;
  return chunk;
}

void dynamic_free(void* address) {
  int merged_idx;
  struct dynamic_chunk *chunk, *prev_chunk, *next_chunk;
  
  address -= DYNAMIC_CHUNK_HEADER_OFFSET;
  if(address < buddy_system.start || address > buddy_system.end) 
    return;
  
  chunk = address;
  chunk->size -= 1; //inuse bit
  merged_idx = chunk->size / DYNAMIC_BIN_MIN_SLOT - 1;
  /*uart_puts("free size ");
  uart_hex(chunk->size);
  uart_puts("\n");
  uart_puts("prev size ");
  uart_hex(chunk->prev_size);
  uart_puts("\n");*/
  if(chunk->size >= DYNAMIC_BIN_MIN_SLOT && chunk->size <= DYNAMIC_BIN_MAX * DYNAMIC_BIN_MIN_SLOT) {
    prev_chunk = (void*)chunk - chunk->prev_size - DYNAMIC_CHUNK_HEADER_OFFSET;
    next_chunk = (void*)chunk + chunk->size + DYNAMIC_CHUNK_HEADER_OFFSET;
    //prev chunk is free chunk
    if(chunk->prev_size >= DYNAMIC_BIN_MIN_SLOT && (prev_chunk->size & 0x1) == 0) {
      //prev chunk is freed
      uart_puts("prev chunk is freed.\n");
      if(dynamic_remove_chunk(prev_chunk, prev_chunk->size) == -1) {
        uart_puts("not found chunk.\n");
        return;
      }
      //merge chunk
      prev_chunk->size = chunk->size + prev_chunk->size + DYNAMIC_CHUNK_HEADER_OFFSET;
      merged_idx = prev_chunk->size / DYNAMIC_BIN_MIN_SLOT - 1;
      chunk = prev_chunk;
    }
    //next chunk is free chunk
    if(next_chunk->prev_size >= DYNAMIC_BIN_MIN_SLOT && (next_chunk->size & 0x1) == 0) {
      //next chunk is free
      if(next_chunk == dynamic_system.top_chunk) {
        //is top chunk
        //uart_puts("next chunk is top chunk.\n");
        //update top chunk
        chunk->size = chunk->size + dynamic_system.top_chunk->size + DYNAMIC_CHUNK_HEADER_OFFSET;
        chunk->next = null;
        dynamic_system.top_chunk = chunk;
        //dynamic_status();  
        return;
      }
      else {
        uart_puts("next chunk is freed.\n");
        if(dynamic_remove_chunk(next_chunk, next_chunk->size) == -1) {
          uart_puts("not found chunk.\n");
          return;
        }
        //merge chunk
        chunk->size = chunk->size + next_chunk->size + DYNAMIC_CHUNK_HEADER_OFFSET;
        merged_idx = chunk->size / DYNAMIC_BIN_MIN_SLOT - 1; 
      }
    }
    //set next chunk prev size
    next_chunk = (void*)chunk + chunk->size + DYNAMIC_CHUNK_HEADER_OFFSET;
    next_chunk->prev_size = chunk->size;       
    //put into free list
    if(chunk->size <= DYNAMIC_BIN_MAX * DYNAMIC_BIN_MIN_SLOT) {
      chunk->next = dynamic_system.bins[merged_idx];
      dynamic_system.bins[merged_idx] = chunk;  
      //uart_puts("put chunk into bin.\n");
    }
    //put into unsorted bin
    else {
      chunk->next = dynamic_system.unsorted_bin;
      dynamic_system.unsorted_bin = chunk;
      //uart_puts("put chunk into unsorted bin.\n");
    }
  }
  //dynamic_status();  
}

void dynamic_status() {
  uart_puts("dynamic_status:\n");
  uart_puts("free list info: \n");
  struct dynamic_chunk* chunk;
  for(int i = 0; i < DYNAMIC_BIN_MAX; i++) {
    chunk = dynamic_system.bins[i];
    if(chunk == null)
      continue;
    uart_puts("size [0x");
    uart_hex((i + 1) * DYNAMIC_BIN_MIN_SLOT);
    uart_puts("]: ");
    while(chunk != null) {
      uart_hex((size_t)chunk);
      uart_puts(" --> ");
      chunk = chunk->next;
    }
    uart_puts("null\n");
  }
  uart_puts("unsorted bin info:\n");
  chunk = dynamic_system.unsorted_bin;
  while(chunk != null) {
    uart_hex((size_t)chunk);
    uart_puts("[");
    uart_hex(chunk->size);
    uart_puts("] --> ");
    chunk = chunk->next;
  }
  uart_puts("null\n");
  uart_puts("top chunk info: \n");
  uart_puts("address: ");
  uart_hex((size_t)dynamic_system.top_chunk);
  uart_puts("\n");
  uart_puts("size: ");
  uart_hex(dynamic_system.top_chunk->size);
  uart_puts("\n");
}

void dynamic_top_chunk_free() {
  size_t size;
  int idx;
  size = dynamic_system.top_chunk->size;
  if(size >= DYNAMIC_BIN_MIN_SLOT) {
    if(size <= DYNAMIC_BIN_MAX * DYNAMIC_BIN_MIN_SLOT) {
      //put rest of top chunk into bin list
      idx = size / DYNAMIC_BIN_MIN_SLOT - 1;
      dynamic_system.top_chunk->next = dynamic_system.bins[idx];
      dynamic_system.bins[idx] = dynamic_system.top_chunk;
    }
    else {
      //put rest of top chunk into unsorted bin
      dynamic_system.top_chunk->next = dynamic_system.unsorted_bin;
      dynamic_system.unsorted_bin = dynamic_system.top_chunk;
    }
  } 
}

int dynamic_remove_chunk(void* address, size_t size) {
  struct dynamic_chunk* chunk, *prev_chunk;
  //in free list
  if(size <= DYNAMIC_BIN_MAX * DYNAMIC_BIN_MIN_SLOT) {
    int idx = size / DYNAMIC_BIN_MIN_SLOT - 1;

    chunk = dynamic_system.bins[idx];
    prev_chunk = null;
    while(chunk != null) {
      if(chunk == (struct dynamic_chunk* ) address) {
        //remove chunk in free list
        if(prev_chunk != null)
          prev_chunk->next = chunk->next;
        else
          dynamic_system.bins[idx] = chunk->next;
       /* uart_puts("remove chunk from free list size ");
        uart_hex(size);
        uart_puts(".\n");*/
        return 0;
      }
      prev_chunk = chunk;
      chunk = chunk->next;
    }
  }
  //in unsorted bin
  else {
    chunk = dynamic_system.unsorted_bin;
    prev_chunk = null;
    while(chunk != null) {
      if(chunk == (struct dynamic_chunk*) address) {
        if(prev_chunk != null)
          prev_chunk->next = chunk->next;
        else
          dynamic_system.unsorted_bin = chunk->next;
        /*uart_puts("remove chunk from unsorted bin size ");
        uart_hex(size);
        uart_puts(".\n");*/
        return 0;
      }
      prev_chunk = chunk;
      chunk = chunk->next;
    }   
  }
  //not found chunk
  return -1;
}

int dynamic_request_new_page() {
  void* tmp = buddy_malloc(PAGE_SIZE);
  if(tmp == null) {
    uart_puts("request new page failed.\n ");
    return -1;
  }
  //put rest of top chunk into free list
  dynamic_top_chunk_free();
  //set to new top chunk
  dynamic_system.top_chunk = tmp;
  dynamic_system.top_chunk->size = PAGE_SIZE - DYNAMIC_CHUNK_HEADER_OFFSET;
  dynamic_system.top_chunk->next = null;
  dynamic_system.top_chunk->prev_size = 0;
  
  
  return 0;
}

void* dynamic_unsorted_bin_malloc(int idx) {
  struct dynamic_chunk *chunk, *prev_chunk, *split_chunk;
  int size, split_size, split_idx;
  chunk = dynamic_system.unsorted_bin;
  prev_chunk = null;
  size = (idx + 1) * DYNAMIC_BIN_MIN_SLOT;
  while(chunk != null) {
    //find chunk in unsorted bin
    //uart_puts("free chunk found in unsorted bin.\n");
    if(size < chunk->size) {
      //remove from unsorted bin
      if(dynamic_remove_chunk(chunk, chunk->size) == -1) {
        uart_puts("not found chunk.\n");
        return null;
      }
      
      split_size = chunk->size - size - DYNAMIC_CHUNK_HEADER_OFFSET;
      if(split_size >= DYNAMIC_BIN_MIN_SLOT) {
        //split chunk
        split_chunk = (void*)chunk + size + DYNAMIC_CHUNK_HEADER_OFFSET;
        split_chunk->size = split_size;
        split_chunk->prev_size = size;

        chunk->size = size;

        if(split_chunk->size <= DYNAMIC_BIN_MAX * DYNAMIC_BIN_MIN_SLOT) {
          //put into free list
          /*uart_puts("put chunk back to bin size");
          uart_hex(split_chunk->size);
          uart_puts(".\n");*/
          
          split_idx = split_chunk->size / DYNAMIC_BIN_MIN_SLOT - 1;
          split_chunk->next = dynamic_system.bins[split_idx];
          dynamic_system.bins[split_idx] = split_chunk; 
        }
        else {
          //put into unsorted bin
          /*uart_puts("put chunk back to unsorted bin size ");
          uart_hex(split_chunk->size);
          uart_puts(".\n");*/
          split_chunk->next = chunk->next;
          if(prev_chunk != null)
            prev_chunk->next = split_chunk;
          else
            dynamic_system.unsorted_bin = split_chunk;
        }
      }
      
      //set header
      chunk->size = chunk->size + 1; //inuse bit
      chunk->next = null;

      return chunk; 
    }
    
    prev_chunk = chunk;
    chunk = chunk->next;
  }
  //uart_puts("no free chunk found in insorted bin.\n");
  return null;
}