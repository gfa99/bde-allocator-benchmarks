
#include <iostream>
#include <iomanip>
#include <memory>
#include <random>
#include <iterator>

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#ifndef _WIN32
# include <unistd.h>
# include <sys/types.h>
# include <sys/wait.h>
#endif

#include <bsl_memory.h>
#include <bslma_testallocator.h>
#include <bslma_newdeleteallocator.h>
#include <bsls_stopwatch.h>

#include <bdlma_sequentialpool.h>
#include <bdlma_sequentialallocator.h>
#include <bdlma_bufferedsequentialallocator.h>
#include <bdlma_multipoolallocator.h>

#include <vector>
#include <string>
#include <unordered_set>
#include <scoped_allocator>
#include "allocont.h"

#define DEBUG
#define INT INT_HIDE

using namespace BloombergLP;

static const int max_problem_logsize = 30;

void usage(char const* cmd, int result)
{
    std::cerr <<
"usage: " << cmd << " <size> <split> <ds> <as> <reference>\n"
"    size:  log2 of total element count, 20 -> 1,000,000\n"
"    split: log2 of container size, 10 -> 1,000\n"
"    ds: [1..12]\n"
"    as: [1..14]\n"
"    reference: float, time of corresponding as=1 or as=8 run, or 0\n"
"    Number of containers used is 2^(size - split)\n"
"    1 <= size <= " << max_problem_logsize << ", 1 <= split <= size\n";
    exit(result);
}

// side_effect() touches memory in a way that optimizers will not optimize away

void side_effect(void* p, size_t len)
{
    static volatile thread_local char* target;
    target = (char*) p;
    memset((char*)target, 0xff, len);
}

// (not standard yet)
struct range {
    int start; int stop;
    struct iter {
        int i;
        bool operator!=(iter other) { return other.i != i; };
        iter& operator++() { ++i; return *this; };
        int operator*() { return i; }
    };
    iter begin() { return iter{start}; }
    iter end() { return iter{stop}; }
};

char trash[1 << 16];  // random characters to construct string keys from

alignas(long long) static char pool[1 << 30];

std::default_random_engine random_engine;
std::uniform_int_distribution<int> pos_dist(0, sizeof(trash) - 1000);
std::uniform_int_distribution<int> length_dist(33, 1000);

char* sptr() { return trash + pos_dist(random_engine); }
size_t slen() {return length_dist(random_engine); }

enum {
    VEC=1<<0, HASH=1<<1,
        VECVEC=1<<2, VECHASH=1<<3,
        HASHVEC=1<<4, HASHHASH=1<<5,
    INT=1<<6, STR=1<<7,
    SA=1<<8, MT=1<<9, MTD=1<<10, PL=1<<11, PLD=1<<12,
        PM=1<<13, PMD=1<<14, CT=1<<15, RT=1<<16
};

char const* const names[] = {
    "vector", "unordered_set", "vector:vector", "vector:unordered_set",
        "unordered_set:vector", "unordered_set:unordered_set",
    "int", "string",
    "new/delete", "monotonic", "monotonic/drop", "multipool", "multipool/drop",
        "multipool/monotonic", "multipool/monotonic/drop",
    "compile-time", "run-time"
};

void print_case(int mask)
{
    for (int i = 8, m = SA; m <= RT; ++i, m <<= 1) {
        if (m & mask) {
            if (m < CT)
                std::cout << "allocator: " << names[i] << ", ";
            else
                std::cout << "bound: " << names[i];
        }}
}
void print_datastruct(int mask)
{
    for (int i = 0, m = VEC; m < SA; ++i, (m <<= 1)) {
        if (m & mask) {
            std::cout << names[i];
            if (m < INT)
                std::cout << ":";
        }}
}

struct subsystem {
    explicit subsystem(int init_length) : dist(25, 128) {
        for (int i : range{0, init_length})
            dats.emplace_back(sptr(), dist(random_engine));
    }
    bool empty() { return dats.empty(); }
    void alter(subsystem& other) {
        dats.pop_front();
        other.dats.emplace_back(sptr(), dist(random_engine));
    }
    std::uniform_int_distribution<int> dist;
    std::list<std::string> dats;
};

void shuffle()
{
    const int subsystems = 1<<7;
    const int init_length = 1<<16;
    const int churns = subsystems * init_length * 5;
    std::uniform_int_distribution<int> dist(0, subsystems-1);
    std::vector<subsystem*> system;
    system.reserve(subsystems);
    for (int i : range{0, subsystems})
        system.push_back(new subsystem(init_length));
    for (int i = 0; i < churns; ) {
        size_t k = dist(random_engine);
        if (! system[k]->empty()) {
            system[k]->alter(*system[i % subsystems]);
            ++i;
        }
    }
    // leak every fourth subsystem:
    for (int i = 0; i < subsystems; ++i)
        if (i & 3)
            delete system[i];
}

template <typename Test>
double measure(int mask, bool csv, double reference, Test test)
{
    // if (mask & SA)
    //     std::cout << std::endl;
    if (!csv) {
        if (mask & SA) {
            print_datastruct(mask);
            std::cout << ":\n\n";
        }
        std::cout << "   ";
        print_case(mask);
        std::cout << std::endl;
    }

#ifndef DEBUG
    int pipes[2];
    int result = pipe(pipes);
    if (result < 0) {
        std::cerr << "\nFailed pipe\n";
        std::cout << "\nFailed pipe\n";
        exit(-1);
    }
    union { double result_time; char buf[sizeof(double)]; };
    int pid = fork();
    if (pid < 0) {
        std::cerr << "\nFailed fork\n";
        std::cout << "\nFailed fork\n";
        exit(-1);
    } else if (pid > 0) {  // parent
        close(pipes[1]);
        int status = 0;
        waitpid(pid, &status, 0);
        if (status == 0) {
            int got = read(pipes[0], buf, sizeof(buf));
            if (got != sizeof(buf)) {
                std::cerr << "\nFailed read\n";
                std::cout << "\nFailed read\n";
                exit(-1);
            }
        } else {
            if (!csv) {
                std::cout << "   (failed)\n" << std::endl;
            } else {
                std::cout << "(failed), (failed%), ";
                print_datastruct(mask);
                std::cout << ", ";
                print_case(mask);
                std::cout << std::endl;
            }
        }
        close(pipes[0]);
    } else {  // child
        close(pipes[0]);
#else
        double result_time;
#endif

        if (mask & SA)
            shuffle();   // Make malloc messy

        bool failed = false;
        bsls::Stopwatch timer;
        try {
            timer.start(true);
            test();
        } catch (std::bad_alloc&) {
            failed = true;
        }
        timer.stop();

        double times[3] = { 0.0, 0.0, 0.0 };
        if (!failed)
            timer.accumulatedTimes(times, times+1, times+2);

        if (!csv) {
            if (!failed) {
                std::cout << "   sys: " << times[0]
                          << " user: " << times[1]
                          << " wall: " << times[2] << ", ";
                if ((mask & (SA|CT)) == (SA|CT)) {
                    std::cout << "(100%)\n";
                } else if (reference == 0.0) {  // reference run failed
                    std::cout << "(N/A%)\n";
                } else {
                    std::cout << (times[2] * 100.)/reference << "%\n";
                }
            } else {
                std::cout << "   (failed)\n";
            }
            std::cout << std::endl;
        } else {
            if (!failed) {
                std::cout << times[2] << ", ";
                if ((mask & (SA|CT)) == (SA|CT)) {
                    std::cout << "(100%), ";
                } else if (reference == 0.0) {  // reference run failed
                    std::cout << "(N/A%), ";
                } else {
                    std::cout << (times[2] * 100.)/reference << "%, ";
                }
            } else {
                std::cout << "(failed), (failed%), ";
            }
            print_datastruct(mask);
            std::cout << ", ";
            print_case(mask);
            std::cout << std::endl;
        }
        result_time = times[2];
#ifndef DEBUG
        write(pipes[1], buf, sizeof(buf));
        close(pipes[1]);
        exit(0);
    }
#endif

    return ((mask & (SA|CT)) == (SA|CT)) ? result_time : reference;
}

#ifdef __GLIBCXX__
namespace std {
  template<typename C, typename T, typename A>
    struct hash<basic_string<C, T, A>>
    {
      using result_type = size_t;
      using argument_type = basic_string<C, T, A>;

      result_type operator()(const argument_type& s) const noexcept
      { return std::_Hash_impl::hash(s.data(), s.length()); }
    };
}
#endif

template <typename T>
struct my_hash {
    using result_type = size_t;
    using argument_type = T;
    result_type operator()(T const& t) const
        { return 1048583 * (1 + reinterpret_cast<ptrdiff_t>(&t)); }
};
template <typename T>
struct my_equal {
    bool operator()(T const& t, T const& u) const
        { return &t == &u; }
};

template <
    typename StdCont,
    typename MonoCont,
    typename MultiCont,
    typename PolyCont,
    typename Work>
void apply_allocation_strategies(int as, double reference,
    int mask, int runs, int split, bool csv, Work work)
{

switch(as)
{
case 1:
// allocator: std::allocator, bound: compile-time
    reference = measure((SA|mask|CT), csv, 0.0,
        [runs,split,work]() {
                for (int run: range{0, runs}) {
                    StdCont c;
                    c.reserve(split);
                    work(c,split);
                }});

break; case 2:
// allocator: monotonic, bound: compile-time
    measure((MT|mask|CT), csv, reference,
        [runs,split,work]() {
                for (int run: range{0, runs}) {
                    bdlma::BufferedSequentialPool bsp(pool, sizeof(pool));
                    MonoCont c(&bsp);
                    c.reserve(split);
                    work(c, split);
                }});

break; case 3:
// allocator: monotonic, bound: compile-time, drop
    measure((MTD|mask|CT), csv, reference,
        [runs,split,work]() {
                for (int run: range{0, runs}) {
                    bdlma::BufferedSequentialPool bsp(pool, sizeof(pool));
                    auto* c = new(bsp) MonoCont(&bsp);
                    c->reserve(split);
                    work(*c, split);
                }});


break; case 4:
// allocator: multipool, bound: compile-time
    measure((PL|mask|CT), csv, reference,
        [runs,split,work]() {
                for (int run: range{0, runs}) {
                    bdlma::Multipool mp;
                    MultiCont c(&mp);
                    c.reserve(split);
                    work(c, split);
                }});

break; case 5:
// allocator: multipool, bound: compile-time, drop
    measure((PLD|mask|CT), csv, reference,
        [runs,split,work]() {
                for (int run: range{0, runs}) {
                    bdlma::Multipool mp;
                    auto* c = new(mp) MultiCont(&mp);
                    c->reserve(split);
                    work(*c, split);
                }});

break; case 6:
// allocator: multipool/monotonic, bound: compile-time
    measure((PM|mask|CT), csv, reference,
        [runs,split,work]() {
                for (int run: range{0, runs}) {
                    bdlma::BufferedSequentialAllocator bsa(pool, sizeof(pool));
                    bdlma::Multipool mp(&bsa);
                    MultiCont c(&mp);
                    c.reserve(split);
                    work(c, split);
                }});

break; case 7:
// allocator: multipool/monotonic, bound: compile-time, drop monotonic
    measure((PMD|mask|CT), csv, reference,
        [runs,split,work]() {
                for (int run: range{0, runs}) {
                    bdlma::BufferedSequentialAllocator bsa(pool, sizeof(pool));
                    bdlma::Multipool* mp =
                            new(bsa) bdlma::Multipool(&bsa);
                    auto* c = new(*mp) MultiCont(mp);
                    c->reserve(split);
                    work(*c, split);
                }});


break; case 8:
// allocator: newdelete, bound: run-time
    measure((SA|mask|RT), csv, reference,
        [runs,split,work]() {
                bslma::NewDeleteAllocator mfa;
                for (int run: range{0, runs}) {
                    PolyCont c(&mfa);
                    c.reserve(split);
                    work(c, split);
                }});

break; case 9:
// allocator: monotonic, bound: run-time
    measure((MT|mask|RT), csv, reference,
        [runs,split,work]() {
                for (int run: range{0, runs}) {
                    bdlma::BufferedSequentialAllocator bsa(pool, sizeof(pool));
                    PolyCont c(&bsa);
                    c.reserve(split);
                    work(c, split);
                }});

break; case 10:
// allocator: monotonic, bound: run-time, drop
    measure((MTD|mask|RT), csv, reference,
        [runs,split,work]() {
                for (int run: range{0, runs}) {
                    bdlma::BufferedSequentialAllocator bsa(pool, sizeof(pool));
                    auto* c = new(bsa) PolyCont(&bsa);
                    c->reserve(split);
                    work(*c, split);
                }});

break; case 11:
// allocator: multipool, bound: run-time
    measure((PL|mask|RT), csv, reference,
        [runs,split,work]() {
                for (int run: range{0, runs}) {
                    bdlma::MultipoolAllocator mpa;
                    PolyCont c(&mpa);
                    c.reserve(split);
                    work(c, split);
                }});

break; case 12:
// allocator: multipool, bound: run-time, drop
    measure((PLD|mask|RT), csv, reference,
        [runs,split,work]() {
                for (int run: range{0, runs}) {
                    bdlma::MultipoolAllocator mpa;
                    auto* c = new(mpa) PolyCont(&mpa);
                    c->reserve(split);
                    work(*c, split);
                }});

break; case 13:
// allocator: multipool/monotonic, bound: run-time
    measure((PM|mask|RT), csv, reference,
        [runs,split,work]() {
                for (int run: range{0, runs}) {
                    bdlma::BufferedSequentialAllocator bsa(pool, sizeof(pool));
                    bdlma::MultipoolAllocator mpa(&bsa);
                    PolyCont c(&mpa);
                    c.reserve(split);
                    work(c, split);
                }});

break; case 14:
// allocator: multipool/monotonic, bound: run-time, drop monotonic
    measure((PMD|mask|RT), csv, reference,
        [runs,split,work]() {
                for (int run: range{0, runs}) {
                    bdlma::BufferedSequentialAllocator bsa(pool, sizeof(pool));
                    bdlma::MultipoolAllocator* mp =
                            new(bsa) bdlma::MultipoolAllocator(&bsa);
                    auto* c = new(*mp) PolyCont(mp);
                    c->reserve(split);
                    work(*c, split);
                }});
}
}

void apply_containers(
    int ds, int as, double reference, int runs, int split, bool csv)
{
switch(ds) {
case 1:
    apply_allocation_strategies<
            std::vector<int>,monotonic::vector<int>,
            multipool::vector<int>,poly::vector<int>>(as, reference,
        VEC|INT, runs * 128, split, csv,
        [] (auto& c, int elems) {
            for (int elt: range{0, elems}) {
                c.emplace_back(elt);
                side_effect(&c.back(), 4);
        }});

break; case 2:
    apply_allocation_strategies<
            std::vector<std::string>,
            monotonic::vector<monotonic::string>,
            multipool::vector<multipool::string>,
            poly::vector<poly::string>>(as, reference,
        VEC|STR, runs * 128, split, csv,
        [] (auto& c, int elems) {
            for (int elt: range{0, elems}) {
                c.emplace_back(sptr(), slen());
                side_effect(const_cast<char*>(c.back().data()), 4);
        }});

break; case 3:
    apply_allocation_strategies<
            std::unordered_set<int>,monotonic::unordered_set<int>,
            multipool::unordered_set<int>,poly::unordered_set<int>>(as, reference,
        HASH|INT, runs * 128, split, csv,
        [] (auto& c, int elems) {
            for (int elt: range{0, elems})
                c.emplace(elt);
        });

break; case 4:
    apply_allocation_strategies<
            std::unordered_set<std::string>,
            monotonic::unordered_set<monotonic::string>,
            multipool::unordered_set<multipool::string>,
            poly::unordered_set<poly::string>>(as, reference,
        HASH|STR, runs * 128, split, csv,
        [] (auto& c, int elems) {
            for (int elt: range{0, elems})
                c.emplace(sptr(), slen());
        });

break; case 5:
    apply_allocation_strategies<
            std::vector<std::vector<int>>,
            monotonic::vector<monotonic::vector<int>>,
            multipool::vector<multipool::vector<int>>,
            poly::vector<poly::vector<int>>>(as, reference,
        VECVEC|INT, runs, split, csv,
        [] (auto& c, int elems) {
            c.emplace_back(128, 1);
            for (int elt: range{0, elems})
                c.emplace_back(c.back());
        });

break; case 6:
    apply_allocation_strategies<
            std::vector<std::vector<std::string>>,
            monotonic::vector<monotonic::vector<monotonic::string>>,
            multipool::vector<multipool::vector<multipool::string>>,
            poly::vector<poly::vector<poly::string>>>(as, reference,
        VECVEC|STR, runs, split, csv,
        [split] (auto& c, int elems) {
            typename std::decay<decltype(c)>::type::value_type s(
                c.get_allocator());
            s.reserve(128);
            for (int i : range{0,128})
                s.emplace_back(sptr(), slen());
            c.emplace_back(std::move(s));
            for (int elt: range{0, split})
                c.emplace_back(c.back());
        });

break; case 7:
    apply_allocation_strategies<
            std::vector<std::unordered_set<int>>,
            monotonic::vector<monotonic::unordered_set<int>>,
            multipool::vector<multipool::unordered_set<int>>,
            poly::vector<poly::unordered_set<int>>>(as, reference,
        VECHASH|INT, runs, split, csv,
        [split] (auto& c, int elems) {
            int in[128]; std::generate(in, in+128, random_engine);
            typename std::decay<decltype(c)>::type::value_type s(
                c.get_allocator());
            s.reserve(128);
            s.insert(in, in+128);
            c.push_back(std::move(s));
            for (int elt: range{0, split})
                c.emplace_back(c.back());
        });

break; case 8:
    apply_allocation_strategies<
            std::vector<std::unordered_set<std::string>>,
            monotonic::vector<monotonic::unordered_set<monotonic::string>>,
            multipool::vector<multipool::unordered_set<multipool::string>>,
            poly::vector<poly::unordered_set<poly::string>>>(as, reference,
        VECHASH|STR, runs, split, csv,
        [split] (auto& c, int elems) {
            typename std::decay<decltype(c)>::type::value_type s(
                c.get_allocator());
            s.reserve(128);
            for (int i : range{0,128})
                s.emplace(sptr(), slen());
            c.push_back(std::move(s));
            for (int elt: range{0, split})
                c.emplace_back(c.back());
        });

break; case 9:
    apply_allocation_strategies<
            std::unordered_set<std::vector<int>,
                    my_hash<std::vector<int>>,
                    my_equal<std::vector<int>>>,
            monotonic::unordered_set<monotonic::vector<int>,
                    my_hash<monotonic::vector<int>>,
                    my_equal<monotonic::vector<int>>>,
            multipool::unordered_set<multipool::vector<int>,
                    my_hash<multipool::vector<int>>,
                    my_equal<multipool::vector<int>>>,
            poly::unordered_set<poly::vector<int>,
                    my_hash<poly::vector<int>>,
                    my_equal<poly::vector<int>>>>(as, reference,
        HASHVEC|INT, runs, split, csv,
        [split] (auto& c, int elems) {
            typename std::decay<decltype(c)>::type::key_type s(
                c.get_allocator());
            s.reserve(128);
            for (int i: range{0, 128})
                s.emplace_back(random_engine());
            c.emplace(std::move(s));
            for (int elt: range{0, split})
                c.emplace(*c.begin());
        });

break; case 10:
    apply_allocation_strategies<
            std::unordered_set<std::vector<std::string>,
                my_hash<std::vector<std::string>>,
                my_equal<std::vector<std::string>>>,
            monotonic::unordered_set<monotonic::vector<monotonic::string>,
                my_hash<monotonic::vector<monotonic::string>>,
                my_equal<monotonic::vector<monotonic::string>>>,
            multipool::unordered_set<multipool::vector<multipool::string>,
                my_hash<multipool::vector<multipool::string>>,
                my_equal<multipool::vector<multipool::string>>>,
            poly::unordered_set<poly::vector<poly::string>,
                my_hash<poly::vector<poly::string>>,
                my_equal<poly::vector<poly::string>>>>(as, reference,
        HASHVEC|STR, runs, split, csv,
        [split] (auto& c, int elems) {
            typename std::decay<decltype(c)>::type::key_type s(
                c.get_allocator());
            s.reserve(128);
            for (int i: range{0, 128})
                s.emplace_back(sptr(), slen());
            c.emplace(std::move(s));
            for (int elt: range{0, split})
                c.emplace(*c.begin());
        });

break; case 11:
    apply_allocation_strategies<
            std::unordered_set<std::unordered_set<int>,
                    my_hash<std::unordered_set<int>>,
                    my_equal<std::unordered_set<int>>>,
            monotonic::unordered_set<
                monotonic::unordered_set<int>,
                    my_hash<monotonic::unordered_set<int>>,
                    my_equal<monotonic::unordered_set<int>>>,
            multipool::unordered_set<
                multipool::unordered_set<int>,
                    my_hash<multipool::unordered_set<int>>,
                    my_equal<multipool::unordered_set<int>>>,
            poly::unordered_set<
                poly::unordered_set<int>,
                    my_hash<poly::unordered_set<int>>,
                    my_equal<poly::unordered_set<int>>>>(as, reference,
        HASHHASH|INT, runs, split, csv,
        [split] (auto& c, int elems) {
            typename std::decay<decltype(c)>::type::key_type s(
                c.get_allocator());
            s.reserve(128);
            for (int i: range{0, 128})
                s.emplace(random_engine());
            c.emplace(std::move(s));
            for (int elt: range{0, split})
                c.emplace(*c.begin());
        });

break; case 12:
    apply_allocation_strategies<
            std::unordered_set<std::unordered_set<std::string>,
                    my_hash<std::unordered_set<std::string>>,
                    my_equal<std::unordered_set<std::string>>>,
            monotonic::unordered_set<
                monotonic::unordered_set<monotonic::string>,
                    my_hash<monotonic::unordered_set<monotonic::string>>,
                    my_equal<monotonic::unordered_set<monotonic::string>>>,
            multipool::unordered_set<
                multipool::unordered_set<multipool::string>,
                    my_hash<multipool::unordered_set<multipool::string>>,
                    my_equal<multipool::unordered_set<multipool::string>>>,
            poly::unordered_set<
                poly::unordered_set<poly::string>,
                    my_hash<poly::unordered_set<poly::string>>,
                    my_equal<poly::unordered_set<poly::string>>>>(as, reference,
        HASHHASH|STR, runs, split, csv,
        [split] (auto& c, int elems) {
            typename std::decay<decltype(c)>::type::key_type s(
                c.get_allocator());
            s.reserve(128);
            for (int i: range{0, 128})
                s.emplace(sptr(), slen());
            c.emplace(std::move(s));
            for (int elt: range{0, split})
                c.emplace(*c.begin());
        });
break;
} // switch
}


int main(int ac, char** av)
{
    std::ios::sync_with_stdio(false);
    if (ac != 6)
        usage(*av, 1);
    int logsize = atoi(av[1]);
    int logsplit = atoi(av[2]);
    int ds = atoi(av[3]);
    int as = atoi(av[4]);
    double reference = atof(av[5]);
    if (logsize < 1 || logsize > max_problem_logsize)
        usage(*av, 2);
    if (logsplit < 1 || logsplit > logsize)
        usage(*av, 3);
    if (ds < 1 || ds > 12)
        usage(*av, 4);
    if (as < 1 || as > 14)
        usage(*av, 5);
    if (reference < 0 || (reference == 0 && as != 1 && as != 8))
        usage(*av, 6);
    bool csv = true;

    if (!csv) {
        std::cout << "Total # of objects = 2^" << logsize
                  << ", # elements per container = 2^" << logsplit
                  << ", # rounds = 2^" << logsize - logsplit << "\n";
    }

    int size = 1 << logsize;
    int split = 1 << logsplit;
    int runs = size / split;

    std::uniform_int_distribution<int> char_dist((int)'!', (int)'~');
    for (char& c : trash)
        c = (char) char_dist(random_engine);

    // The actual storage
    memset(pool, 1, sizeof(pool));  // Fault in real memory

    std::cout << std::setprecision(3);

    apply_containers(ds, as, reference, runs, split, csv);

    return 0;
}

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
