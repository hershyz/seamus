#include <cassert>
#include <iostream>
#include <vector>
#include <string>

#include "InvertedIndex.h"

void testSingleTermQuery() {
    InvertedIndex idx;
    idx.addDoc({"the", "quick", "brown", "fox"}, "url1.com");
    idx.addDoc({"the", "lazy", "dog"}, "url2.com");
    idx.addDoc({"quick", "dog"}, "url3.com");

    auto results = idx.findQuery({"the"});
    assert(results.size() == 2);
    assert(results[0].docId == 0);
    assert(results[1].docId == 1);
}

void testSingleTermSingleDoc() {
    InvertedIndex idx;
    idx.addDoc({"hello", "world"}, "example.com");

    auto results = idx.findQuery({"hello"});
    assert(results.size() == 1);
    assert(results[0].docId == 0);
    assert(results[0].location == 0);
}

void testGlobalLocationIsMonotonic() {
    InvertedIndex idx;
    idx.addDoc({"foo", "bar"}, "a.com");
    idx.addDoc({"foo", "baz"}, "b.com");

    auto results = idx.findQuery({"foo"});
    assert(results.size() == 2);
    assert(results[0].location < results[1].location);
}

void testMultiTermQuery() {
    InvertedIndex idx;
    idx.addDoc({"cat", "dog"}, "url1.com");
    idx.addDoc({"dog", "fish"}, "url2.com");
    idx.addDoc({"cat", "dog", "fish"}, "url3.com");

    auto results = idx.findQuery({"cat", "dog"});
    assert(results.size() == 2);
    assert(results[0].docId == 0);
    assert(results[1].docId == 2);
}

void testMultiTermNoMatch() {
    InvertedIndex idx;
    idx.addDoc({"apple", "banana"}, "url1.com");
    idx.addDoc({"cherry", "apple"}, "url2.com");

    auto results = idx.findQuery({"cherry", "banana"});
    assert(results.size() == 0);
}

int main() {
    testSingleTermQuery();
    testSingleTermSingleDoc();
    testGlobalLocationIsMonotonic();
    testMultiTermQuery();
    testMultiTermNoMatch();

    std::cout << "All tests passed." << std::endl;
    return 0;
}
