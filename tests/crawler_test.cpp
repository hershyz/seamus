#include <cassert>
#include <iostream>
#include <cstring>
#include "../crawler/parser.h"


void test_serialize_deserialize_basic() {
    std::cout << "Testing serialize/deserialize basic..." << std::endl;

    CrawlTarget original{string("example.com"), string("https://example.com/page"), 3, 1};

    size_t buf_size = crawl_target_serialized_size(original);
    char* buf = new char[buf_size];

    // Serialize
    char* end = serialize_crawl_target(buf, original);
    assert(static_cast<size_t>(end - buf) == buf_size);

    // Deserialize
    CrawlTarget result{string(""), string(""), 0, 0};
    const char* read_end = deserialize_crawl_target(buf, buf_size, result);
    assert(read_end != nullptr);
    assert(static_cast<size_t>(read_end - buf) == buf_size);

    assert(result.domain == "example.com");
    assert(result.domain.size() == 11);
    assert(result.url == "https://example.com/page");
    assert(result.url.size() == 24);
    assert(result.seed_distance == 3);
    assert(result.domain_dist == 1);

    // Original unchanged
    assert(original.domain == "example.com");
    assert(original.url == "https://example.com/page");
    assert(original.seed_distance == 3);
    assert(original.domain_dist == 1);

    delete[] buf;
}


void test_serialize_deserialize_empty_strings() {
    std::cout << "Testing serialize/deserialize with empty strings..." << std::endl;

    CrawlTarget original{string(""), string(""), 0, 0};

    size_t buf_size = crawl_target_serialized_size(original);
    // Should be just the headers: 4 + 0 + 4 + 0 + 2 + 2 = 12
    assert(buf_size == 12);

    char* buf = new char[buf_size];
    serialize_crawl_target(buf, original);

    CrawlTarget result{string(""), string(""), 0, 0};
    const char* read_end = deserialize_crawl_target(buf, buf_size, result);
    assert(read_end != nullptr);

    assert(result.domain == "");
    assert(result.domain.size() == 0);
    assert(result.url == "");
    assert(result.url.size() == 0);
    assert(result.seed_distance == 0);
    assert(result.domain_dist == 0);

    delete[] buf;
}


void test_serialize_deserialize_max_values() {
    std::cout << "Testing serialize/deserialize with max uint16 values..." << std::endl;

    CrawlTarget original{string("test.org"), string("http://test.org"), UINT16_MAX, UINT16_MAX};

    size_t buf_size = crawl_target_serialized_size(original);
    char* buf = new char[buf_size];
    serialize_crawl_target(buf, original);

    CrawlTarget result{string(""), string(""), 0, 0};
    const char* read_end = deserialize_crawl_target(buf, buf_size, result);
    assert(read_end != nullptr);

    assert(result.domain == "test.org");
    assert(result.url == "http://test.org");
    assert(result.seed_distance == UINT16_MAX);
    assert(result.domain_dist == UINT16_MAX);

    delete[] buf;
}


void test_deserialize_truncated_buffer() {
    std::cout << "Testing deserialize with truncated buffer..." << std::endl;

    CrawlTarget original{string("example.com"), string("https://example.com"), 1, 2};

    size_t buf_size = crawl_target_serialized_size(original);
    char* buf = new char[buf_size];
    serialize_crawl_target(buf, original);

    CrawlTarget result{string(""), string(""), 0, 0};

    // Too small for even the first length prefix
    assert(deserialize_crawl_target(buf, 2, result) == nullptr);

    // Has domain length but not enough for domain bytes
    assert(deserialize_crawl_target(buf, 6, result) == nullptr);

    // Cut off in the middle of url
    assert(deserialize_crawl_target(buf, buf_size - 10, result) == nullptr);

    // One byte short (missing last byte of domain_dist)
    assert(deserialize_crawl_target(buf, buf_size - 1, result) == nullptr);

    delete[] buf;
}


void test_serialize_multiple_sequential() {
    std::cout << "Testing multiple sequential serialize/deserialize..." << std::endl;

    CrawlTarget ct1{string("foo.com"), string("https://foo.com/a"), 1, 0};
    CrawlTarget ct2{string("bar.org"), string("https://bar.org/b"), 5, 3};

    size_t total = crawl_target_serialized_size(ct1) + crawl_target_serialized_size(ct2);
    char* buf = new char[total];

    char* mid = serialize_crawl_target(buf, ct1);
    char* end = serialize_crawl_target(mid, ct2);
    assert(static_cast<size_t>(end - buf) == total);

    CrawlTarget r1{string(""), string(""), 0, 0};
    CrawlTarget r2{string(""), string(""), 0, 0};
    const char* r_mid = deserialize_crawl_target(buf, total, r1);
    assert(r_mid != nullptr);
    size_t remaining = total - static_cast<size_t>(r_mid - buf);
    const char* r_end = deserialize_crawl_target(r_mid, remaining, r2);
    assert(r_end != nullptr);

    assert(r1.domain == "foo.com");
    assert(r1.url == "https://foo.com/a");
    assert(r1.seed_distance == 1);
    assert(r1.domain_dist == 0);

    assert(r2.domain == "bar.org");
    assert(r2.url == "https://bar.org/b");
    assert(r2.seed_distance == 5);
    assert(r2.domain_dist == 3);

    delete[] buf;
}


int main() {
    std::cout << "--- Running Crawler Tests ---\n";

    test_serialize_deserialize_basic();
    test_serialize_deserialize_empty_strings();
    test_serialize_deserialize_max_values();
    test_deserialize_truncated_buffer();
    test_serialize_multiple_sequential();

    std::cout << "All crawler tests passed successfully!\n";
    std::cout << "----------------------------------------\n";

    return 0;
}
