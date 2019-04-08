#ifndef ALLOCONT_H_
#define ALLOCONT_H_

#include <list>
#include <forward_list>
#include <deque>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <scoped_allocator>
#include <type_traits>

#include <bsl_memory.h>
#include <bslma_mallocfreeallocator.h>

#include <bdlma_sequentialallocator.h>
#include <bdlma_bufferedsequentialallocator.h>
#include <bdlma_multipoolallocator.h>

using namespace BloombergLP;

template <typename T, typename Pool>
struct pool_adaptor {
    typedef T value_type;
    typedef T& reference;
    typedef T const& const_reference;
    Pool* pool;
    pool_adaptor() : pool(nullptr) {}
    pool_adaptor(Pool* poo) : pool(poo) {}
    template <typename T2>
        pool_adaptor(pool_adaptor<T2,Pool> other) : pool(other.pool) { }
    T* allocate(size_t sz) {
        char volatile* p = (char*) pool->allocate(sz * sizeof(T));
        *p = '\0';
        return (T*) p;
    }
    void deallocate(void* p, size_t) { pool->deallocate(p); }
};

template <typename T1, typename T2, typename Pool>
    bool operator==(
        pool_adaptor<T1,Pool> const& one,
        pool_adaptor<T2,Pool> const& two)
            { return one.pool == two.pool; }

struct stdalloc {
    using string = std::string;
    template <typename T> using vector = std::vector<T>;
    template <typename T,
              typename H=std::hash<T>, typename Eq=std::equal_to<T>>
        using unordered_set = std::unordered_set<T,H,Eq>;
    template <typename T>
        using allocator = std::allocator<T>;
};

// Varriant of a bslma::BufferedSequentalPool that supports overaligned types
// up to a pre-determined limit (usually a cache line).
class OveralignedBufferedSequentialPool {
    BloombergLP::bdlma::BufferedSequentialPool d_imp;
    int                                        d_maxAlign;
    int                                        d_alignOffset;

public:
    OveralignedBufferedSequentialPool(char             *buffer,
                                      int               size,
                                      int               maxAlign,
                                      bslma::Allocator *basicAllocator = 0)
        : d_imp(buffer, size, basicAllocator)
        , d_maxAlign(maxAlign)
        , d_alignOffset((intptr_t) buffer & (maxAlign - 1)) { }
    OveralignedBufferedSequentialPool(char             *buffer,
                                      int               size,
                                      int               maxAlign,
                                      bsls::BlockGrowth::Strategy
                                                         growthStrategy,
                                      bslma::Allocator  *basicAllocator = 0)
        : d_imp(buffer, size, growthStrategy, basicAllocator)
        , d_maxAlign(maxAlign)
        , d_alignOffset((intptr_t) buffer & (maxAlign - 1)) { }

    void *allocate(bsls::Types::size_type size) {
        using std::intptr_t;
        void *ret;

        int alignment = static_cast<int>(size | d_maxAlign);
        alignment &= -alignment; // clear all but lowest order set bit
        int alignMask = alignment - 1;

        // If next allocation will naturally achieve sufficient alignment for
        // this allocation, just allocate from imp. Afterwords, test for
        // proper alignment in case a buffer realloc happened.
        if (! (d_alignOffset & alignMask)) {
            ret = d_imp.allocate(size);
            d_alignOffset = ((intptr_t) ret + size) & (d_maxAlign - 1);
        };

        // If next allocation will not achieve sufficient alignment for this
        // allocation, adjust alignment by allocating sufficient extra bytes,
        // then try allocating repeatedly until success. Given the allocation
        // strategy, it should take no more than two tries to get it right.
        while (d_alignOffset & alignMask) {
            int diff = alignment - d_alignOffset;
            (void) d_imp.allocate(diff);
            ret = d_imp.allocate(size);
            d_alignOffset = ((intptr_t) ret + size) & (d_maxAlign - 1);
        }

        return ret;
    }

    void deallocate(void*) {}
};

struct mallocfree {

template <typename T>
    using allocator =
        std::scoped_allocator_adaptor<
            pool_adaptor<T,BloombergLP::bslma::MallocFreeAllocator>>;

template <class T>
    using list = std::list<T,allocator<T>>;
template <class T>
    using forward_list = std::forward_list<T,allocator<T>>;
template <class T>
    using deque = std::deque<T,allocator<T>>;
template <class T>
    using vector = std::vector<T,allocator<T>>;

template <class T>
    using basic_string =
        std::basic_string<T,std::char_traits<T>,allocator<T>>;

using string = basic_string<char>;

template <class Key, class T, class Compare = std::less<Key>>
    using map = std::map<
        Key,T,Compare,allocator<std::pair<const Key,T>>>;
template <class Key, class T, class Compare = std::less<Key>>
    using multimap = std::multimap<
        Key,T,Compare,allocator<std::pair<const Key,T>>>;

template <class Key, class Compare = std::less<Key>>
    using set =      std::set<Key,Compare,allocator<Key>>;
template <class Key, class Compare = std::less<Key>>
   using multiset = std::multiset<Key,Compare,allocator<Key>>;

template <class Key, class T,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_map = std::unordered_map<
        Key,T,Hash,Pred,allocator<std::pair<const Key,T>>>;
template <class Key, class T,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_multimap = std::unordered_multimap<
        Key,T,Hash,Pred,allocator<std::pair<const Key,T>>>;

template <class Key,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_set = std::unordered_set<
    Key,Hash,Pred,allocator<Key>>;
template <class Key,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_multiset = std::unordered_multiset<
        Key,Hash,Pred,allocator<Key>>;
};

struct monotonic {

template <typename T>
    using allocator = std::scoped_allocator_adaptor<
        pool_adaptor<T, bdlma::BufferedSequentialPool>>;

template <class T>
    using list = std::list<T,allocator<T>>;
template <class T>
    using forward_list = std::forward_list<T,allocator<T>>;
template <class T>
    using deque = std::deque<T,allocator<T>>;
template <class T>
    using vector = std::vector<T,allocator<T>>;

template <class T>
    using basic_string =
        std::basic_string<T,std::char_traits<T>,allocator<T>>;

using string = basic_string<char>;

template <class Key, class T, class Compare = std::less<Key>>
    using map = std::map<
        Key,T,Compare,allocator<std::pair<const Key,T>>>;
template <class Key, class T, class Compare = std::less<Key>>
    using multimap = std::multimap<
        Key,T,Compare,allocator<std::pair<const Key,T>>>;

template <class Key, class Compare = std::less<Key>>
    using set =      std::set<Key,Compare,allocator<Key>>;
template <class Key, class Compare = std::less<Key>>
    using multiset = std::multiset<Key,Compare,allocator<Key>>;

template <class Key, class T,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_map = std::unordered_map<
        Key,T,Hash,Pred,allocator<std::pair<const Key,T>>>;
template <class Key, class T,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_multimap = std::unordered_multimap<
        Key,T,Hash,Pred,allocator<std::pair<const Key,T>>>;

template <class Key,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_set = std::unordered_set<
        Key,Hash,Pred,allocator<Key>>;
template <class Key,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_multiset = std::unordered_multiset<
        Key,Hash,Pred,allocator<Key>>;
};

struct overaligned_monotonic {

template <typename T>
    using allocator = std::scoped_allocator_adaptor<
        pool_adaptor<T, OveralignedBufferedSequentialPool>>;

template <class T>
    using list = std::list<T,allocator<T>>;
template <class T>
    using forward_list = std::forward_list<T,allocator<T>>;
template <class T>
    using deque = std::deque<T,allocator<T>>;
template <class T>
    using vector = std::vector<T,allocator<T>>;

template <class T>
    using basic_string =
        std::basic_string<T,std::char_traits<T>,allocator<T>>;

using string = basic_string<char>;

template <class Key, class T, class Compare = std::less<Key>>
    using map = std::map<
        Key,T,Compare,allocator<std::pair<const Key,T>>>;
template <class Key, class T, class Compare = std::less<Key>>
    using multimap = std::multimap<
        Key,T,Compare,allocator<std::pair<const Key,T>>>;

template <class Key, class Compare = std::less<Key>>
    using set =      std::set<Key,Compare,allocator<Key>>;
template <class Key, class Compare = std::less<Key>>
    using multiset = std::multiset<Key,Compare,allocator<Key>>;

template <class Key, class T,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_map = std::unordered_map<
        Key,T,Hash,Pred,allocator<std::pair<const Key,T>>>;
template <class Key, class T,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_multimap = std::unordered_multimap<
        Key,T,Hash,Pred,allocator<std::pair<const Key,T>>>;

template <class Key,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_set = std::unordered_set<
        Key,Hash,Pred,allocator<Key>>;
template <class Key,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_multiset = std::unordered_multiset<
        Key,Hash,Pred,allocator<Key>>;
};

struct multipool {

template <typename T>
    using allocator = std::scoped_allocator_adaptor<
        pool_adaptor<T,BloombergLP::bdlma::Multipool>>;

template <class T>
    using list = std::list<T,allocator<T>>;
template <class T>
    using forward_list = std::forward_list<T,allocator<T>>;
template <class T>
    using deque = std::deque<T,allocator<T>>;
template <class T>
    using vector = std::vector<T,allocator<T>>;

template <class T>
    using basic_string =
        std::basic_string<T,std::char_traits<T>,allocator<T>>;

using string = basic_string<char>;

template <class Key, class T, class Compare = std::less<Key>>
    using map = std::map<
        Key,T,Compare,allocator<std::pair<const Key,T>>>;
template <class Key, class T, class Compare = std::less<Key>>
    using multimap = std::multimap<
        Key,T,Compare,allocator<std::pair<const Key,T>>>;

template <class Key, class Compare = std::less<Key>>
    using set =      std::set<Key,Compare,allocator<Key>>;
template <class Key, class Compare = std::less<Key>>
    using multiset = std::multiset<Key,Compare,allocator<Key>>;

template <class Key, class T,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_map = std::unordered_map<
        Key,T,Hash,Pred,allocator<std::pair<const Key,T>>>;
template <class Key, class T,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_multimap = std::unordered_multimap<
        Key,T,Hash,Pred,allocator<std::pair<const Key,T>>>;

template <class Key,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_set = std::unordered_set<
        Key,Hash,Pred,allocator<Key>>;
template <class Key,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_multiset = std::unordered_multiset<
        Key,Hash,Pred,allocator<Key>>;
};

struct poly {

template <typename T>
    using allocator = std::scoped_allocator_adaptor<bsl::allocator<T>>;

template <class T>
    using list = std::list<T,allocator<T>>;
template <class T>
    using forward_list = std::forward_list<T,allocator<T>>;
template <class T>
    using deque = std::deque<T,allocator<T>>;
template <class T>
    using vector = std::vector<T,allocator<T>>;

template <class T>
    using basic_string =
        std::basic_string<T,std::char_traits<T>,allocator<T>>;

using string = basic_string<char>;

template <class Key, class T, class Compare = std::less<Key>>
    using map = std::map<
        Key,T,Compare,allocator<std::pair<const Key,T>>>;
template <class Key, class T, class Compare = std::less<Key>>
    using multimap = std::multimap<
        Key,T,Compare,allocator<std::pair<const Key,T>>>;

template <class Key, class Compare = std::less<Key>>
    using set =      std::set<Key,Compare,allocator<Key>>;
template <class Key, class Compare = std::less<Key>>
    using multiset = std::multiset<Key,Compare,allocator<Key>>;

template <class Key, class T,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_map = std::unordered_map<
        Key,T,Hash,Pred,allocator<std::pair<const Key,T>>>;
template <class Key, class T,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_multimap = std::unordered_multimap<
        Key,T,Hash,Pred,allocator<std::pair<const Key,T>>>;

template <class Key,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_set = std::unordered_set<
        Key,Hash,Pred,allocator<Key>>;
template <class Key,
          class Hash = std::hash<Key>, class Pred = std::equal_to<Key>>
    using unordered_multiset = std::unordered_multiset<
        Key,Hash,Pred,allocator<Key>>;
};

#endif

// ----------------------------------------------------------------------------
// Copyright 2015 Bloomberg Finance L.P.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ----------------------------- END-OF-FILE ----------------------------------
