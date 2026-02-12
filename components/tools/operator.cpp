#include <memory>
#include <esp_heap_caps.h>

void* operator new(std::size_t count) { return heap_caps_malloc(count, MALLOC_CAP_SPIRAM); }

void operator delete(void* ptr) noexcept {
    if(ptr) free(ptr);
}

/*
// C++17 only
void* operator new (std::size_t count, std::align_val_t alignment) { 
	return heap_caps_malloc(count, MALLOC_CAP_SPIRAM); 
} 

// C++17 only
void operator delete(void* ptr, std::align_val_t alignment) noexcept { 
	if (ptr) free(ptr); 
}
*/
