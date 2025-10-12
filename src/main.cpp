// ============= main.cpp =============
#include "blockweave.h"
#include "wallet/wallet.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

void PrintUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <alice_address> <bob_address> <miner_address>\n";
    std::cout << "\nGenerate wallet addresses first using the 'wallet' executable:\n";
    std::cout << "  ./wallet\n\n";
    std::cout << "Then run this program with three addresses:\n";
    std::cout << "  " << program_name << " <addr1> <addr2> <addr3>\n";
}

// Mining thread function
void MiningThread(CBlockweave* p_weave, const std::string& str_miner_address) {
    std::cout << "[Mining Thread] Started\n";

    while (!p_weave->ShouldStopMining()) {
        if (p_weave->IsMiningEnabled() && p_weave->GetMempoolSize() > 0) {
            p_weave->MineBlock(str_miner_address);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    std::cout << "[Mining Thread] Stopped\n";
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Error: Expected 3 wallet addresses as arguments.\n\n";
        PrintUsage(argv[0]);
        return 1;
    }

    std::string str_alice_address = argv[1];
    std::string str_bob_address = argv[2];
    std::string str_miner_address = argv[3];

    std::cout << "=== Arweave-like Blockweave System (Threaded) ===\n\n";
    std::cout << "Alice's address: " << str_alice_address.substr(0, 16) << "...\n";
    std::cout << "Bob's address: " << str_bob_address.substr(0, 16) << "...\n";
    std::cout << "Miner's address: " << str_miner_address.substr(0, 16) << "...\n\n";

    CBlockweave weave;

    // Create wallets
    CWallet alice, bob;

    // Start mining thread
    weave.StartMining();
    std::thread mining_thread(MiningThread, &weave, str_miner_address);

    std::cout << "[Main Thread] Creating transactions...\n\n";

    // Transaction 1: Alice -> Bob
    std::string str_data1 = "Hello, permanent storage!";
    std::vector<uint8_t> bytes1(str_data1.begin(), str_data1.end());
    auto tx1 = alice.CreateTransaction(str_bob_address, bytes1, 100);
    weave.AddTransaction(tx1);

    // Transaction 2: Bob -> Alice
    std::string str_data2 = "This data will last forever on the blockweave";
    std::vector<uint8_t> bytes2(str_data2.begin(), str_data2.end());
    auto tx2 = bob.CreateTransaction(str_alice_address, bytes2, 150);
    weave.AddTransaction(tx2);

    // Wait for mining
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Transaction 3: Alice -> Bob
    std::string str_data3 = "Another piece of permanent data";
    std::vector<uint8_t> bytes3(str_data3.begin(), str_data3.end());
    auto tx3 = alice.CreateTransaction(str_bob_address, bytes3, 200);
    weave.AddTransaction(tx3);

    // Wait for mining to complete
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Stop mining and wait for thread to finish
    weave.StopMining();
    mining_thread.join();

    // Print final state
    weave.PrintChain();

    // Verify data retrieval
    std::cout << "Retrieving data from transaction 1...\n";
    auto retrieved_data = weave.GetData(tx1->m_id);
    if(!retrieved_data.empty()) {
        std::string str_retrieved(retrieved_data.begin(), retrieved_data.end());
        std::cout << "Retrieved: " << str_retrieved << "\n";
    }

    return 0;
}

