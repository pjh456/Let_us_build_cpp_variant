// main.cpp
#include <iostream>
#include <string>
#include <cassert>
#include "variant.hpp" // Your header file

// A helper struct to trace lifecycle events.
struct Logger
{
    int id;
    Logger(int i) : id(i) { std::cout << "Logger(" << id << "): Constructed\n"; }
    ~Logger() { std::cout << "Logger(" << id << "): Destructed\n"; }
    Logger(const Logger &other) : id(other.id) { std::cout << "Logger(" << id << "): Copied\n"; }
    Logger(Logger &&other) noexcept : id(other.id)
    {
        std::cout << "Logger(" << id << "): Moved\n";
        other.id = -1;
    }
    Logger &operator=(const Logger &other)
    {
        id = other.id;
        std::cout << "Logger(" << id << "): Copy Assigned\n";
        return *this;
    }
    Logger &operator=(Logger &&other) noexcept
    {
        id = other.id;
        std::cout << "Logger(" << id << "): Move Assigned\n";
        other.id = -1;
        return *this;
    }
};

int main()
{
    std::cout << "--- Testing Value Construction ---\n";
    Variant<int, std::string, Logger> v1(10);
    assert(v1.index() == 0);
    assert(v1.get<0>() == 10);
    assert(v1.holds_alternative<int>());

    Variant<int, std::string, Logger> v2((std::string) "hello");
    assert(v2.index() == 1);
    assert(v2.get<std::string>() == "hello");

    std::cout << "\n--- Testing Assignment from Value ---\n";
    {
        Variant<int, Logger> v(100);
        std::cout << "Assigning Logger(1) to v...\n";
        v = Logger(1); // Destroys int, constructs Logger
        assert(v.index() == 1);
        assert(v.get<Logger>().id == 1);
        std::cout << "v is going out of scope...\n";
    } // Logger(1) is destructed here

    std::cout << "\n--- Testing Copy Construction ---\n";
    Variant<int, std::string, Logger> v3(Logger(3));
    Variant<int, std::string, Logger> v4 = v3; // Copy constructor
    // std::cout << v3.index() << " " << v4.index() << std::endl;
    assert(v3.index() == 2);
    assert(v4.index() == 2);
    assert(v3.get<Logger>().id == 3);
    assert(v4.get<Logger>().id == 3);

    std::cout << "\n--- Testing Move Construction ---\n";
    Variant<int, std::string, Logger> v5 = std::move(v3); // Move constructor
    assert(v5.index() == 2);
    assert(v5.get<Logger>().id == 3);
    assert(v3.index() == -1); // Moved-from variant is empty

    std::cout << "\n--- Testing Copy Assignment ---\n";
    v1 = v2; // Assign string to int variant
    assert(v1.index() == 1);
    assert(v1.get<std::string>() == "hello");

    std::cout << "\n--- Testing Move Assignment ---\n";
    Variant<int, std::string, Logger> v6(999);
    std::cout << "Before move assignment: v6 contains int, v5 contains Logger\n";
    v6 = std::move(v5); // Destroys int, moves Logger
    assert(v6.index() == 2);
    assert(v6.get<Logger>().id == 3);
    assert(v5.index() == -1);

    std::cout << "\n--- All Tests Passed ---\n";
    // Watch the destructors fire for v1, v2, v4, v6, v7
    return 0;
}