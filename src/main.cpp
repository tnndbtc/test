// ============= main.cpp =============
#include "blockweave.h"
#include "wallet.h"
#include <iostream>
#include <string>
#include <vector>

int main() {
    std::cout << "=== Arweave-like Blockweave System ===\n\n";

    CBlockweave weave;

    CWallet alice, bob, miner;
    std::cout << "Alice's address: " << alice.GetAddress().substr(0, 16) << "...\n";
    std::cout << "Bob's address: " << bob.GetAddress().substr(0, 16) << "...\n\n";

    std::string str_data1 = "Hello, permanent storage!";
    std::vector<uint8_t> bytes1(str_data1.begin(), str_data1.end());
    auto tx1 = alice.CreateTransaction(bob.GetAddress(), bytes1, 100);
    weave.AddTransaction(tx1);

    std::string str_data2 = "This data will last forever on the blockweave";
    std::vector<uint8_t> bytes2(str_data2.begin(), str_data2.end());
    auto tx2 = bob.CreateTransaction(alice.GetAddress(), bytes2, 150);
    weave.AddTransaction(tx2);

    weave.MineBlock(miner.GetAddress());

    std::string str_data3 = "Another piece of permanent data";
    std::vector<uint8_t> bytes3(str_data3.begin(), str_data3.end());
    auto tx3 = alice.CreateTransaction(bob.GetAddress(), bytes3, 200);
    weave.AddTransaction(tx3);

    weave.MineBlock(miner.GetAddress());

    weave.PrintChain();

    std::cout << "Retrieving data from transaction 1...\n";
    auto retrieved_data = weave.GetData(tx1->m_id);
    if(!retrieved_data.empty()) {
        std::string str_retrieved(retrieved_data.begin(), retrieved_data.end());
        std::cout << "Retrieved: " << str_retrieved << "\n";
    }

    return 0;
}

