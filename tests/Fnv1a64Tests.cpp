#include "Fnv1a64.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

int main()
{
    const char* empty = "";
    const char* a = "a";
    const char* hello = "hello";
    if (fnv1a64::Hash(empty, 0) != 0xcbf29ce484222325ull ||
        fnv1a64::Hash(a, std::strlen(a)) != 0xaf63dc4c8601ec8cull ||
        fnv1a64::Hash(hello, std::strlen(hello)) != 0xa430d84680aabd0bull)
    {
        std::cerr << "FNV-1a 64 standard vectors failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "FNV-1a 64 vectors passed\n";
    return EXIT_SUCCESS;
}
