// ============= wallet_main.cpp =============
#include "wallet.h"
#include <iostream>
#include <string>

int main() {
    std::cout << "=== Blockweave Wallet Generator ===\n\n";

    CWallet wallet;
    std::string str_address = wallet.GetAddress();

    std::cout << "Generated Wallet Address:\n";
    std::cout << str_address << "\n\n";

    std::cout << "Use this address with rest_daemon:\n";
    std::cout << "  ./rest_daemon <alice_address> <bob_address> <miner_address>\n";

    return 0;
}
