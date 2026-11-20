#include "GUID.h"

#include <random>
#include <sstream>
#include <iomanip>

std::string GUID::Generate()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());

    static std::uniform_int_distribution<int> dist(0, 15);

    static const char* hex = "0123456789ABCDEF";

    std::stringstream ss;

    int groups[] = { 8,4,4,4,12 };

    for (int g = 0; g < 5; g++)
    {
        if (g != 0)
            ss << "-";

        for (int i = 0; i < groups[g]; i++)
        {
            ss << hex[dist(gen)];
        }
    }

    return ss.str();
}