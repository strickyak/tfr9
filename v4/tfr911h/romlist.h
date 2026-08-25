#ifndef _TMANAGER_ROMLIST_H_
#define _TMANAGER_ROMLIST_H_

#include <iostream>
#include <array>
#include <cstdint>

// Using const auto&... allows the compiler to deduce the exact array type and size
template <const auto&... Roms>
struct RomList {
    // 2. Calculate the combined size using a Fold Expression.
    // sizeof() on an array reference returns the total bytes of that array.
    static constexpr std::size_t total_size = (sizeof(Roms) + ... + 0);

    // 3. A constexpr function to generate the new combined array
    static constexpr std::array<char, total_size> concatenate() {
        std::array<char, total_size> result{};
        std::size_t offset = 0;

        // Lambda to copy a single array chunk into our result
        auto copy_chunk = [&](const auto& arr) {
            for (std::size_t i = 0; i < sizeof(arr); ++i) {
                result[offset++] = arr[i];
            }
        };

        // Fold expression: calls the lambda for every ROM in the template pack
        (copy_chunk(Roms), ...);

        return result;
    }

    // 4. The final, perfectly sized, concatenated ROM image
    static constexpr std::array<char, total_size> data = concatenate();
};

/*
// Arrays MUST be constexpr so the compiler can read their contents
constexpr char rom_header[] = { 0x4E, 0x45, 0x53, 0x1A }; 
constexpr char rom_prg[]    = { 0x01, 0x02, 0x03 };
constexpr char rom_chr[]    = { 0x08, 0x09 };

int main() {
    // Pass the array variables as a varargs template list
    using MyGame = RomList<rom_header, rom_prg, rom_chr>;

    std::cout << "Total Size: " << MyGame::total_size << " bytes\nData: ";
    
    for (char byte : MyGame::data) {
        // Cast to uint8_t so hex printing formats correctly
        std::cout << std::hex << (int)(uint8_t)byte << " "; 
    }
    std::cout << std::endl;

    return 0;
}
*/

#endif //  _TMANAGER_ROMLIST_H_
