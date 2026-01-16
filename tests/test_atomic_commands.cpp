// Tests for atomic counter and string commands
// INCR, DECR, INCRBY, DECRBY, APPEND, STRLEN

#include "../src/storage/kv_store.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <limits>

void test_incr() {
    std::cout << "Testing INCR...\n";
    KVStore kv;

    // INCR on new key initializes to 1
    auto [val1, err1] = kv.incr("counter");
    assert(err1.empty());
    assert(val1 == 1);

    // INCR on existing numeric key increments
    auto [val2, err2] = kv.incr("counter");
    assert(err2.empty());
    assert(val2 == 2);

    // INCR on string value returns error
    kv.set("name", "alice");
    auto [val3, err3] = kv.incr("name");
    assert(!err3.empty());

    // INCR on numeric string works
    kv.set("strnum", "100");
    auto [val4, err4] = kv.incr("strnum");
    assert(err4.empty());
    assert(val4 == 101);

    // INCR on negative number
    kv.set("neg", "-5");
    auto [val5, err5] = kv.incr("neg");
    assert(err5.empty());
    assert(val5 == -4);

    std::cout << "INCR tests passed!\n";
}

void test_decr() {
    std::cout << "Testing DECR...\n";
    KVStore kv;

    // DECR on new key initializes to -1
    auto [val1, err1] = kv.decr("counter");
    assert(err1.empty());
    assert(val1 == -1);

    // DECR on existing numeric key decrements
    auto [val2, err2] = kv.decr("counter");
    assert(err2.empty());
    assert(val2 == -2);

    // DECR on non-numeric value returns error
    kv.set("name", "bob");
    auto [val3, err3] = kv.decr("name");
    assert(!err3.empty());

    // DECR on positive number
    kv.set("pos", "10");
    auto [val4, err4] = kv.decr("pos");
    assert(err4.empty());
    assert(val4 == 9);

    std::cout << "DECR tests passed!\n";
}

void test_incrby() {
    std::cout << "Testing INCRBY...\n";
    KVStore kv;

    // INCRBY on new key initializes to delta
    auto [val1, err1] = kv.incrby("counter", 5);
    assert(err1.empty());
    assert(val1 == 5);

    // INCRBY with positive delta
    auto [val2, err2] = kv.incrby("counter", 10);
    assert(err2.empty());
    assert(val2 == 15);

    // INCRBY with negative delta (acts like DECRBY)
    auto [val3, err3] = kv.incrby("counter", -3);
    assert(err3.empty());
    assert(val3 == 12);

    // INCRBY on non-numeric value returns error
    kv.set("name", "carol");
    auto [val4, err4] = kv.incrby("name", 1);
    assert(!err4.empty());

    std::cout << "INCRBY tests passed!\n";
}

void test_decrby() {
    std::cout << "Testing DECRBY...\n";
    KVStore kv;

    // DECRBY on new key initializes to -delta
    auto [val1, err1] = kv.decrby("counter", 5);
    assert(err1.empty());
    assert(val1 == -5);

    // DECRBY with positive delta
    auto [val2, err2] = kv.decrby("counter", 3);
    assert(err2.empty());
    assert(val2 == -8);

    // DECRBY with negative delta (acts like INCRBY)
    auto [val3, err3] = kv.decrby("counter", -10);
    assert(err3.empty());
    assert(val3 == 2);

    std::cout << "DECRBY tests passed!\n";
}

void test_append() {
    std::cout << "Testing APPEND...\n";
    KVStore kv;

    // APPEND to new key creates key with value
    size_t len1 = kv.append("msg", "Hello");
    assert(len1 == 5);
    std::string val1;
    kv.get("msg", val1);
    assert(val1 == "Hello");

    // APPEND to existing key
    size_t len2 = kv.append("msg", " World");
    assert(len2 == 11);
    std::string val2;
    kv.get("msg", val2);
    assert(val2 == "Hello World");

    // APPEND empty string
    size_t len3 = kv.append("msg", "");
    assert(len3 == 11);

    std::cout << "APPEND tests passed!\n";
}

void test_strlen() {
    std::cout << "Testing STRLEN...\n";
    KVStore kv;

    // STRLEN on non-existent key returns 0
    assert(kv.strlen("missing") == 0);

    // STRLEN on empty string
    kv.set("empty", "");
    assert(kv.strlen("empty") == 0);

    // STRLEN on normal string
    kv.set("hello", "Hello World");
    assert(kv.strlen("hello") == 11);

    // STRLEN on numeric value
    kv.set("num", "12345");
    assert(kv.strlen("num") == 5);

    std::cout << "STRLEN tests passed!\n";
}

void run_atomic_command_tests() {
    test_incr();
    test_decr();
    test_incrby();
    test_decrby();
    test_append();
    test_strlen();
}

