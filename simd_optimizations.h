#pragma once

#include <immintrin.h>
#include <cstdint>
#include <cstring>

namespace gots {

// ============================================================================
// SIMD-OPTIMIZED OPERATIONS FOR GOTS RUNTIME
// ============================================================================

class SIMDOptimizations {
public:
    // ============================================================================
    // CARD TABLE OPERATIONS
    // ============================================================================
    
    // Process 32 cards simultaneously using AVX2
    static size_t scan_dirty_cards_avx2(uint8_t* card_table, size_t card_count, 
                                       uint32_t* dirty_indices, size_t max_indices) {
        size_t found_count = 0;
        size_t simd_count = card_count & ~31; // Round down to multiple of 32
        
        const __m256i zero_vec = _mm256_setzero_si256();
        
        for (size_t i = 0; i < simd_count && found_count < max_indices; i += 32) {
            // Load 32 cards
            __m256i cards = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(card_table + i));
            
            // Compare with zero (clean cards)
            __m256i cmp_result = _mm256_cmpeq_epi8(cards, zero_vec);
            
            // Invert to get dirty cards mask
            __m256i dirty_mask = _mm256_xor_si256(cmp_result, _mm256_set1_epi8(-1));
            
            // Extract dirty card positions
            uint32_t mask = _mm256_movemask_epi8(dirty_mask);
            
            // Process each dirty card
            while (mask != 0 && found_count < max_indices) {
                int bit_pos = __builtin_ctz(mask); // Count trailing zeros
                dirty_indices[found_count++] = i + bit_pos;
                mask &= mask - 1; // Clear lowest set bit
            }
        }
        
        // Handle remaining cards with scalar code
        for (size_t i = simd_count; i < card_count && found_count < max_indices; ++i) {
            if (card_table[i] != 0) {
                dirty_indices[found_count++] = i;
            }
        }
        
        return found_count;
    }
    
    // Clear multiple cards efficiently using SIMD
    static void clear_cards_avx2(uint8_t* card_table, size_t card_count) {
        size_t simd_count = card_count & ~31; // Round down to multiple of 32
        const __m256i zero_vec = _mm256_setzero_si256();
        
        // Clear 32 cards at a time
        for (size_t i = 0; i < simd_count; i += 32) {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(card_table + i), zero_vec);
        }
        
        // Clear remaining cards
        std::memset(card_table + simd_count, 0, card_count - simd_count);
    }
    
    // ============================================================================
    // OBJECT HEADER OPERATIONS
    // ============================================================================
    
    // Batch process object headers for GC marking
    static void mark_objects_batch_avx2(void** objects, size_t count, uint32_t mark_bit) {
        const __m256i mark_mask = _mm256_set1_epi32(mark_bit);
        size_t simd_count = count & ~7; // Process 8 pointers at a time
        
        for (size_t i = 0; i < simd_count; i += 8) {
            // Load 8 object pointers
            __m256i obj_ptrs_lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(objects + i));
            __m256i obj_ptrs_hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(objects + i + 4));
            
            // For each object, mark the header (would need custom implementation)
            // This is a conceptual example - actual implementation would vary
            
            // Process remaining objects with scalar code
        }
        
        // Handle remaining objects
        for (size_t i = simd_count; i < count; ++i) {
            if (objects[i]) {
                // Mark object header (scalar implementation)
                uint8_t* header = static_cast<uint8_t*>(objects[i]) - 8;
                *reinterpret_cast<uint32_t*>(header + 4) |= mark_bit;
            }
        }
    }
    
    // ============================================================================
    // STRING OPERATIONS
    // ============================================================================
    
    // Fast string comparison using SIMD
    static bool strings_equal_avx2(const char* str1, const char* str2, size_t length) {
        if (length == 0) return true;
        
        size_t simd_length = length & ~31; // Process 32 chars at a time
        
        for (size_t i = 0; i < simd_length; i += 32) {
            __m256i chunk1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(str1 + i));
            __m256i chunk2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(str2 + i));
            
            __m256i cmp = _mm256_cmpeq_epi8(chunk1, chunk2);
            uint32_t mask = _mm256_movemask_epi8(cmp);
            
            if (mask != 0xFFFFFFFF) {
                return false; // Found difference
            }
        }
        
        // Compare remaining characters
        return std::memcmp(str1 + simd_length, str2 + simd_length, length - simd_length) == 0;
    }
    
    // Fast string hashing using SIMD
    static uint64_t hash_string_avx2(const char* str, size_t length) {
        const uint64_t FNV_PRIME = 0x100000001b3ULL;
        const uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;
        
        uint64_t hash = FNV_OFFSET;
        size_t simd_length = length & ~31;
        
        // Process 32 bytes at a time
        for (size_t i = 0; i < simd_length; i += 32) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(str + i));
            
            // Extract bytes and hash them (simplified)
            for (int j = 0; j < 32; ++j) {
                uint8_t byte = _mm256_extract_epi8(chunk, j);
                hash ^= byte;
                hash *= FNV_PRIME;
            }
        }
        
        // Hash remaining bytes
        for (size_t i = simd_length; i < length; ++i) {
            hash ^= str[i];
            hash *= FNV_PRIME;
        }
        
        return hash;
    }
    
    // ============================================================================
    // MEMORY OPERATIONS
    // ============================================================================
    
    // Ultra-fast memory copy using AVX2
    static void memcpy_avx2(void* dest, const void* src, size_t size) {
        if (size < 32) {
            std::memcpy(dest, src, size);
            return;
        }
        
        uint8_t* d = static_cast<uint8_t*>(dest);
        const uint8_t* s = static_cast<const uint8_t*>(src);
        
        size_t simd_size = size & ~31;
        
        // Copy 32 bytes at a time
        for (size_t i = 0; i < simd_size; i += 32) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + i), chunk);
        }
        
        // Copy remaining bytes
        if (simd_size < size) {
            std::memcpy(d + simd_size, s + simd_size, size - simd_size);
        }
    }
    
    // Fast memory initialization
    static void memset_avx2(void* dest, uint8_t value, size_t size) {
        if (size < 32) {
            std::memset(dest, value, size);
            return;
        }
        
        uint8_t* d = static_cast<uint8_t*>(dest);
        const __m256i fill_value = _mm256_set1_epi8(value);
        
        size_t simd_size = size & ~31;
        
        // Set 32 bytes at a time
        for (size_t i = 0; i < simd_size; i += 32) {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + i), fill_value);
        }
        
        // Set remaining bytes
        if (simd_size < size) {
            std::memset(d + simd_size, value, size - simd_size);
        }
    }
    
    // ============================================================================
    // ARRAY OPERATIONS
    // ============================================================================
    
    // Fast array searching using SIMD
    static int find_pointer_avx2(void** array, size_t count, void* target) {
        if (count == 0) return -1;
        
        const __m256i target_vec = _mm256_set1_epi64x(reinterpret_cast<int64_t>(target));
        size_t simd_count = count & ~3; // Process 4 pointers at a time
        
        for (size_t i = 0; i < simd_count; i += 4) {
            __m256i ptrs = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(array + i));
            __m256i cmp = _mm256_cmpeq_epi64(ptrs, target_vec);
            
            uint32_t mask = _mm256_movemask_pd(_mm256_castsi256_pd(cmp));
            if (mask != 0) {
                // Found match, find exact position
                for (int j = 0; j < 4; ++j) {
                    if (mask & (1 << j)) {
                        return i + j;
                    }
                }
            }
        }
        
        // Search remaining elements
        for (size_t i = simd_count; i < count; ++i) {
            if (array[i] == target) {
                return i;
            }
        }
        
        return -1; // Not found
    }
    
    // ============================================================================
    // MATHEMATICAL OPERATIONS
    // ============================================================================
    
    // Vectorized integer operations for counters/statistics
    static void add_counters_avx2(uint32_t* counters, const uint32_t* increments, size_t count) {
        size_t simd_count = count & ~7; // Process 8 counters at a time
        
        for (size_t i = 0; i < simd_count; i += 8) {
            __m256i current = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(counters + i));
            __m256i increment = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(increments + i));
            __m256i result = _mm256_add_epi32(current, increment);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(counters + i), result);
        }
        
        // Handle remaining counters
        for (size_t i = simd_count; i < count; ++i) {
            counters[i] += increments[i];
        }
    }
    
    // ============================================================================
    // UTILITY FUNCTIONS
    // ============================================================================
    
    // Check if AVX2 is available at runtime
    static bool is_avx2_supported() {
        static bool checked = false;
        static bool supported = false;
        
        if (!checked) {
            int info[4];
            __cpuid(info, 0);
            int max_level = info[0];
            
            if (max_level >= 7) {
                __cpuid_count(7, 0, info[0], info[1], info[2], info[3]);
                supported = (info[1] & (1 << 5)) != 0; // AVX2 bit
            }
            
            checked = true;
        }
        
        return supported;
    }
    
    // Get optimal alignment for SIMD operations
    static constexpr size_t simd_alignment() {
        return 32; // 256-bit alignment for AVX2
    }

private:
    // CPUID helper function
    static void __cpuid(int info[4], int function_id) {
        #ifdef _MSC_VER
        __cpuid(info, function_id);
        #else
        __asm__ __volatile__(
            "cpuid"
            : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3])
            : "a"(function_id)
        );
        #endif
    }
    
    static void __cpuid_count(int function_id, int subfunction_id, 
                             int& eax, int& ebx, int& ecx, int& edx) {
        #ifdef _MSC_VER
        int info[4];
        __cpuidex(info, function_id, subfunction_id);
        eax = info[0]; ebx = info[1]; ecx = info[2]; edx = info[3];
        #else
        __asm__ __volatile__(
            "cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(function_id), "c"(subfunction_id)
        );
        #endif
    }
};

// ============================================================================
// SIMD-OPTIMIZED MEMORY ALLOCATOR
// ============================================================================

class SIMDAlignedAllocator {
public:
    static void* allocate_aligned(size_t size, size_t alignment = 32) {
        void* ptr = nullptr;
        
        #ifdef _WIN32
        ptr = _aligned_malloc(size, alignment);
        #else
        if (posix_memalign(&ptr, alignment, size) != 0) {
            ptr = nullptr;
        }
        #endif
        
        return ptr;
    }
    
    static void deallocate_aligned(void* ptr) {
        if (ptr) {
            #ifdef _WIN32
            _aligned_free(ptr);
            #else
            free(ptr);
            #endif
        }
    }
    
    // RAII wrapper for SIMD-aligned memory
    template<typename T>
    class aligned_ptr {
    private:
        T* ptr_;
        size_t size_;
        
    public:
        aligned_ptr(size_t count, size_t alignment = 32) 
            : size_(count * sizeof(T)) {
            ptr_ = static_cast<T*>(allocate_aligned(size_, alignment));
            if (!ptr_) {
                throw std::bad_alloc();
            }
        }
        
        ~aligned_ptr() {
            deallocate_aligned(ptr_);
        }
        
        T* get() { return ptr_; }
        const T* get() const { return ptr_; }
        
        T& operator[](size_t index) { return ptr_[index]; }
        const T& operator[](size_t index) const { return ptr_[index]; }
        
        // Prevent copying
        aligned_ptr(const aligned_ptr&) = delete;
        aligned_ptr& operator=(const aligned_ptr&) = delete;
        
        // Allow moving
        aligned_ptr(aligned_ptr&& other) noexcept : ptr_(other.ptr_), size_(other.size_) {
            other.ptr_ = nullptr;
            other.size_ = 0;
        }
        
        aligned_ptr& operator=(aligned_ptr&& other) noexcept {
            if (this != &other) {
                deallocate_aligned(ptr_);
                ptr_ = other.ptr_;
                size_ = other.size_;
                other.ptr_ = nullptr;
                other.size_ = 0;
            }
            return *this;
        }
    };
};

} // namespace gots