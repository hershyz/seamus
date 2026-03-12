#include "lib/vector.h"
#include <cassert>
#include "lib/string.h"

// Test default constructor
void test_default_constructor()
{
    vector<int> v;
    assert(v.size() == 0);
    assert(v.capacity() == 0);
}

// Test resize constructor
void test_resize_constructor()
{
    vector<int> v(5);
    assert(v.size() == 5);
    assert(v.capacity() == 5);
}

// Test fill constructor
void test_fill_constructor()
{
    vector<int> v(5, 42);
    assert(v.size() == 5);
    assert(v.capacity() == 5);
    for (size_t i = 0; i < v.size(); ++i)
    {
        assert(v[i] == 42);
    }
}

// Test copy constructor
void test_copy_constructor()
{
    vector<int> v1(3, 10);
    vector<int> v2(v1);

    assert(v2.size() == v1.size());
    assert(v2.capacity() == v1.capacity());
    for (size_t i = 0; i < v1.size(); ++i)
    {
        assert(v2[i] == v1[i]);
    }

    // Verify deep copy
    v2[0] = 99;
    assert(v1[0] == 10);
    assert(v2[0] == 99);
}

// Test assignment operator
void test_assignment_operator()
{
    vector<int> v1(3, 10);
    vector<int> v2;

    v2 = v1;
    assert(v2.size() == v1.size());
    for (size_t i = 0; i < v1.size(); ++i)
    {
        assert(v2[i] == v1[i]);
    }

    // Test self-assignment
    v1 = v1;
    assert(v1.size() == 3);
    assert(v1[0] == 10);
}

// Test move constructor
void test_move_constructor()
{
    vector<int> v1(3, 42);
    int *old_ptr = v1.begin();

    vector<int> v2(static_cast<vector<int> &&>(v1));

    assert(v2.size() == 3);
    assert(v2.capacity() == 3);
    assert(v2[0] == 42);
    assert(v2.begin() == old_ptr);

    assert(v1.size() == 0);
    assert(v1.capacity() == 0);
    assert(v1.begin() == nullptr);
}

// Test move assignment operator
void test_move_assignment_operator()
{
    vector<int> v1(3, 42);
    vector<int> v2(2, 99);
    int *old_ptr = v1.begin();

    v2 = static_cast<vector<int> &&>(v1);

    assert(v2.size() == 3);
    assert(v2.capacity() == 3);
    assert(v2[0] == 42);
    assert(v2.begin() == old_ptr);

    assert(v1.size() == 0);
    assert(v1.capacity() == 0);
    assert(v1.begin() == nullptr);

    // Test self-assignment
    vector<int> v3(3, 7);
    v3 = static_cast<vector<int> &&>(v3);
    assert(v3.size() == 3);
}

// Test push_back
void test_push_back()
{
    vector<int> v;

    // Push to empty vector
    v.push_back(1);
    assert(v.size() == 1);
    assert(v.capacity() == 1);
    assert(v[0] == 1);

    // Push causing reallocation
    v.push_back(2);
    assert(v.size() == 2);
    assert(v.capacity() == 2);
    assert(v[1] == 2);

    // Push without reallocation
    v.push_back(3);
    assert(v.size() == 3);
    assert(v.capacity() == 4);
    assert(v[2] == 3);

    // Push multiple elements
    for (int i = 4; i <= 10; ++i)
    {
        v.push_back(i);
    }
    assert(v.size() == 10);
    for (size_t i = 0; i < 10; ++i)
    {
        assert(v[i] == static_cast<int>(i + 1));
    }
}

// Test popBack
void test_pop_back()
{
    vector<int> v(5, 10);

    v.pop_back();
    assert(v.size() == 4);
    assert(v.capacity() == 5);

    v.pop_back();
    v.pop_back();
    assert(v.size() == 2);
}

// Test reserve
void test_reserve()
{
    vector<int> v;
    v.push_back(1);
    v.push_back(2);

    v.reserve(10);
    assert(v.size() == 2);
    assert(v.capacity() == 10);
    assert(v[0] == 1);
    assert(v[1] == 2);
}

// Test operator[]
void test_subscript_operator()
{
    vector<int> v(5);

    // Test writing
    for (size_t i = 0; i < v.size(); ++i)
    {
        v[i] = static_cast<int>(i * 2);
    }

    // Test reading
    for (size_t i = 0; i < v.size(); ++i)
    {
        assert(v[i] == static_cast<int>(i * 2));
    }

    // Test const version
    const vector<int> &cv = v;
    assert(cv[0] == 0);
    assert(cv[4] == 8);
}

// Test iterators
void test_iterators()
{
    vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);

    // Test mutable iterators
    int sum = 0;
    for (int *it = v.begin(); it != v.end(); ++it)
    {
        sum += *it;
    }
    assert(sum == 6);

    // Modify through iterator
    *v.begin() = 10;
    assert(v[0] == 10);

    // Test const iterators
    const vector<int> &cv = v;
    sum = 0;
    for (const int *it = cv.begin(); it != cv.end(); ++it)
    {
        sum += *it;
    }
    assert(sum == 15);
}

// Test with struct type
struct Point
{
    int x, y;
    Point() : x(0), y(0) {}
    Point(int x_, int y_) : x(x_), y(y_) {}
};

void test_with_structs()
{
    vector<Point> v;
    v.push_back(Point(1, 2));
    v.push_back(Point(3, 4));

    assert(v.size() == 2);
    assert(v[0].x == 1 && v[0].y == 2);
    assert(v[1].x == 3 && v[1].y == 4);

    vector<Point> v2(v);
    assert(v2[0].x == 1);
}

// Test edge cases
void test_edge_cases()
{
    // Empty vector operations
    vector<int> v;
    assert(v.size() == 0);
    assert(v.begin() == v.end());

    // Single element
    v.push_back(42);
    assert(v.size() == 1);
    assert(*v.begin() == 42);

    v.pop_back();
    assert(v.size() == 0);
}

// Test capacity growth
void test_capacity_growth()
{
    vector<int> v;

    size_t prev_capacity = 0;
    for (int i = 0; i < 100; ++i)
    {
        v.push_back(i);
        if (v.capacity() > prev_capacity)
        {
            // Verify capacity doubled (or started at 1)
            if (prev_capacity > 0)
            {
                assert(v.capacity() == prev_capacity * 2);
            }
            prev_capacity = v.capacity();
        }
    }

    assert(v.size() == 100);
    for (int i = 0; i < 100; ++i)
    {
        assert(v[i] == i);
    }
}


// Tests with custom string

void test_vector_empty() {
    vector<string> vec;
    assert(vec.size() == 0);
    assert(vec.capacity() == 0);
    assert(vec.empty() == true);
}

void test_vector_push_back_rvalue() {
    vector<string> vec;

    // Pushing a temporary (rvalue). Should invoke push_back(T&&)
    vec.push_back(string("apple"));

    assert(vec.size() == 1);
    assert(vec[0] == "apple");
}

void test_vector_reallocation_move_safety() {
    vector<string> vec;

    // Push enough elements to force multiple reallocations.
    // If realloc_() accidentally copies instead of moving,
    // the compiler will throw a "deleted function" error here.
    const int num_elements = 50;
    for (int i = 0; i < num_elements; ++i) {
        // Mix of short strings (SSO) and long strings (Heap)
        if (i % 2 == 0) {
            vec.push_back(string("short"));
        } else {
            vec.push_back(string("this_is_a_very_long_heap_string"));
        }
    }

    assert(vec.size() == num_elements);
    assert(vec.capacity() >= num_elements); // Ensure it grew

    // Verify data integrity after all those memory moves
    assert(vec[0] == "short");
    assert(vec[1] == "this_is_a_very_long_heap_string");
    assert(vec[49] == "this_is_a_very_long_heap_string");
}

void test_vector_pop_back() {
    vector<string> vec;
    vec.push_back(string("first"));
    vec.push_back(string("second"));

    assert(vec.size() == 2);

    // Should call the destructor of "second"
    vec.pop_back();

    assert(vec.size() == 1);
    assert(vec.back() == "first");
}

void test_vector_move_constructor() {
    vector<string> v1;
    v1.push_back(string("hello"));
    v1.push_back(string("world"));

    // Steal v1's pointers
    vector<string> v2(move(v1));

    assert(v2.size() == 2);
    assert(v2[0] == "hello");
    assert(v2[1] == "world");

    // v1 should be completely gutted
    assert(v1.size() == 0);
    assert(v1.capacity() == 0);
}

void test_vector_move_assignment() {
    vector<string> v1;
    v1.push_back(string("data"));

    vector<string> v2;
    v2.push_back(string("to_be_overwritten"));

    // v2 should destroy its own contents, then steal v1's pointers
    v2 = move(v1);

    assert(v2.size() == 1);
    assert(v2[0] == "data");
    assert(v1.size() == 0);
}

void test_vector_iteration() {
    vector<string> vec;
    vec.push_back(string("a"));
    vec.push_back(string("b"));
    vec.push_back(string("c"));

    int count = 0;
    // Testing begin() and end()
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        count++;
    }
    assert(count == 3);
}

void test_vector_with_default_constructible_types() {
    // Because your `string` class has NO default constructor and NO copy constructor,
    // methods like resize() or the copy constructor will fail to compile for vector<string>.
    // We test those methods here using `int` to ensure the vector logic is sound.

    // Test fill constructor
    vector<int> v1(5, 42);
    assert(v1.size() == 5);
    assert(v1[4] == 42);

    // Test resize (growing)
    v1.resize(10);
    assert(v1.size() == 10);
    assert(v1[9] == 0); // Default constructed int is 0

    // Test resize (shrinking)
    v1.resize(3);
    assert(v1.size() == 3);
    assert(v1[2] == 42);

    // Test copy constructor
    vector<int> v2(v1);
    assert(v2.size() == 3);
    assert(v2[0] == 42);
}

int main()
{
    test_default_constructor();
    test_resize_constructor();
    test_fill_constructor();
    test_copy_constructor();
    test_assignment_operator();
    test_move_constructor();
    test_move_assignment_operator();
    test_push_back();
    test_pop_back();
    test_reserve();
    test_subscript_operator();
    test_iterators();
    test_with_structs();
    test_edge_cases();
    test_capacity_growth();

    // Tests with custom string
    test_vector_empty();
    test_vector_push_back_rvalue();
    test_vector_reallocation_move_safety();
    test_vector_pop_back();
    test_vector_move_constructor();
    test_vector_move_assignment();
    test_vector_iteration();
    test_vector_with_default_constructible_types();

    // If all tests pass, program exits normally
    return 0;
}
