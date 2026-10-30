#include "CosmosCli.h"

#include <iostream>

int main(int ArgumentCount, char** ArgumentValues) {
    const std::vector<std::string> Arguments(ArgumentValues + 1, ArgumentValues + ArgumentCount);
    return CosmosCli::RunCosmosCli(Arguments, std::cout, std::cerr);
}
