// ============= wallet_main.cpp =============
#include "wallet.h"
#include "crypto/keystore.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <stdexcept>

// Platform-specific headers for hidden password input
#ifdef _WIN32
    #include <windows.h>
    #include <conio.h>
#else
    #include <termios.h>
    #include <unistd.h>
#endif

void PrintUsage() {
    std::cout << "=== Blockweave Wallet ===\n\n";
    std::cout << "Usage:\n";
    std::cout << "  wallet create <keystore_file> [password]  - Create new wallet\n";
    std::cout << "  wallet load <keystore_file> [password]    - Load existing wallet\n";
    std::cout << "  wallet show <keystore_file>               - Show address only\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  wallet create my_wallet.json\n";
    std::cout << "  wallet load my_wallet.json\n";
    std::cout << "  wallet show my_wallet.json\n";
    std::cout << "\n";
    std::cout << "Note: If password is not provided, you will be prompted interactively.\n";
}

std::string ReadPassword(const std::string& str_prompt = "Enter password: ") {
    std::string str_password;
    std::cout << str_prompt;
    std::cout.flush();

#ifdef _WIN32
    // Windows: Use _getch() to read characters without echo
    char ch;
    while ((ch = _getch()) != '\r') {  // '\r' is Enter key on Windows
        if (ch == '\b') {  // Backspace
            if (!str_password.empty()) {
                str_password.pop_back();
                std::cout << "\b \b";  // Erase character from display
            }
        } else if (ch >= 32 && ch <= 126) {  // Printable characters
            str_password.push_back(ch);
        }
    }
    std::cout << "\n";
#else
    // Unix/Linux/macOS: Use termios to disable echo
    struct termios old_term, new_term;

    // Get current terminal settings
    tcgetattr(STDIN_FILENO, &old_term);
    new_term = old_term;

    // Disable echo
    new_term.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);

    // Read password
    std::getline(std::cin, str_password);

    // Restore terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);

    std::cout << "\n";
#endif

    return str_password;
}

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            PrintUsage();
            return 1;
        }

        std::string str_command = argv[1];

        // Handle help commands
        if (str_command == "help" || str_command == "-h" || str_command == "--help") {
            PrintUsage();
            return 0;
        }

        if (str_command == "create") {
            if (argc < 3) {
                std::cerr << "Error: Missing keystore file path\n";
                PrintUsage();
                return 1;
            }

            std::string str_keystore_path = argv[2];
            std::string str_password;

            // Check if file already exists
            if (std::filesystem::exists(str_keystore_path)) {
                std::cerr << "Error: Keystore file already exists: " << str_keystore_path << "\n";
                return 1;
            }

            // Get password
            if (argc >= 4) {
                str_password = argv[3];
            } else {
                str_password = ReadPassword();

                // Confirm password
                std::string str_confirm = ReadPassword("Confirm password: ");

                if (str_password != str_confirm) {
                    std::cerr << "Error: Passwords do not match\n";
                    return 1;
                }
            }

            // Create new wallet
            CWallet wallet;
            std::string str_address = wallet.GetAddress();

            // Save to keystore
            wallet.SaveToKeystore(str_keystore_path, str_password);

            std::cout << "Wallet created successfully!\n\n";
            std::cout << "Address: 0x" << str_address << "\n";
            std::cout << "Keystore: " << str_keystore_path << "\n\n";
            std::cout << "IMPORTANT: Keep your password safe. It cannot be recovered!\n";

        } else if (str_command == "load") {
            if (argc < 3) {
                std::cerr << "Error: Missing keystore file path\n";
                PrintUsage();
                return 1;
            }

            std::string str_keystore_path = argv[2];
            std::string str_password;

            // Check if file exists
            if (!std::filesystem::exists(str_keystore_path)) {
                std::cerr << "Error: Keystore file not found: " << str_keystore_path << "\n";
                return 1;
            }

            // Get password
            if (argc >= 4) {
                str_password = argv[3];
            } else {
                str_password = ReadPassword();
            }

            // Load wallet
            CWallet wallet(str_keystore_path, str_password);
            std::string str_address = wallet.GetAddress();

            std::cout << "Wallet loaded successfully!\n\n";
            std::cout << "Address: 0x" << str_address << "\n";
            std::cout << "Keystore: " << str_keystore_path << "\n";

        } else if (str_command == "show") {
            if (argc < 3) {
                std::cerr << "Error: Missing keystore file path\n";
                PrintUsage();
                return 1;
            }

            std::string str_keystore_path = argv[2];

            // Check if file exists
            if (!std::filesystem::exists(str_keystore_path)) {
                std::cerr << "Error: Keystore file not found: " << str_keystore_path << "\n";
                return 1;
            }

            // Load keystore (no decryption)
            CKeystore keystore = CKeystore::LoadFromFile(str_keystore_path);
            std::string str_address = keystore.GetAddress();

            std::cout << "Address: 0x" << str_address << "\n";

        } else {
            std::cerr << "Error: Unknown command: " << str_command << "\n";
            PrintUsage();
            return 1;
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
