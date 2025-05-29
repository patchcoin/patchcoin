#ifndef BITCOINADDRESSLOOKUP_H
#define BITCOINADDRESSLOOKUP_H

#include <node/interface_ui.h>
#include <util/translation.h>

#include <string>
#include <memory>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <consensus/amount.h>
#include <logging.h>
#include <util/system.h>

#if !defined(_WIN32)
#  include <sys/wait.h>
#endif

class ElectrumInterface {
private:
    std::string m_electrum_path;

    std::pair<std::string, int> ExecuteCommand(const std::string& command) {
        std::string full_command;
#ifdef _WIN32
        full_command = m_electrum_path + " " + command + " 2>&1";
#else
        full_command = m_electrum_path + " " + command + " 2>&1";
#endif

        LogPrintf("ElectrumInterface: Executing command: %s\n", full_command.c_str());

        std::array<char, 1024> buffer;
        std::string result;

        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(full_command.c_str(), "r"), pclose);
        if (!pipe) {
            return {std::string("Failed to execute command: ") + full_command, -1};
        }

        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }

        int raw = pclose(pipe.release());
#if defined(WEXITSTATUS)
        int status = WEXITSTATUS(raw);
#else
        int status = raw;
#endif

        LogPrintf("ElectrumInterface: Command result (status %d): %s\n", status, result.c_str());
        return {result, status};
    }

    std::string EscapeArg(const std::string& arg) {
#ifdef _WIN32
        std::string escaped = "\"";
        for (char c : arg) {
            if (c == '\"') {
                escaped += "\\\"";
            } else if (c == '\\') {
                escaped += "\\\\";
            } else {
                escaped += c;
            }
        }
        escaped += "\"";
        return escaped;
#else
        std::string escaped = "'";
        for (char c : arg) {
            if (c == '\'') {
                escaped += "'\\''";
            } else {
                escaped += c;
            }
        }
        escaped += "'";
        return escaped;
#endif
    }

public:
    ElectrumInterface(const std::string& electrum_path = "electrum")
        : m_electrum_path(electrum_path) {}

    std::pair<std::string, bool> ValidateAddress(const std::string& address) {
        auto [output, status] = ExecuteCommand("--offline validateaddress " + EscapeArg(address));
        return {output, status == 0};
    }

    std::pair<std::string, bool> VerifyMessage(const std::string& address, const std::string& signature, const std::string& message) {
        std::string command = "--offline verifymessage " +
                             EscapeArg(address) + " " +
                             EscapeArg(signature) + " " +
                             EscapeArg(message);

        auto [output, status] = ExecuteCommand(command);
        bool success = (status == 0 && output.find("true") != std::string::npos);

        return {output, success};
    }
};

class BitcoinAddressLookup {
public:
    explicit BitcoinAddressLookup(const std::string& db_path)
        : db_path_(db_path), enabled_(!db_path.empty())
    {
        if (db_path_.empty()) {
            LogPrintf("BitcoinAddressLookup disabled (no database path)\n");
        } else {
            LogPrintf("BitcoinAddressLookup initialized with database %s\n", db_path_);
        }
    }

    ~BitcoinAddressLookup() = default;

    CAmount getBalance(const std::string& address) {
        if (!enabled_) return 0;

        {
            std::shared_lock<std::shared_mutex> lock(cache_mutex_);
            auto it = cache_.find(address);
            if (it != cache_.end()) {
                return it->second;
            }
        }

        CAmount balance = loadSingle(address);
        {
            std::unique_lock<std::shared_mutex> lock(cache_mutex_);
            cache_[address] = balance;
        }
        return balance;
    }

    bool isEnabled() const { return enabled_; }

    static bool InitializeGlobal() {
        std::string dbPath = gArgs.GetArg("-btcbalances", "");
        if (dbPath.empty()) {
            LogPrintf("BitcoinAddressLookup disabled (no -btcbalances)\n");
            return true;
        }
        try {
            LogPrintf("Initializing BitcoinAddressLookup with database %s\n", dbPath);
            instance_ = std::make_unique<BitcoinAddressLookup>(dbPath);
            if (!instance_->testDatabaseAccess()) {
                return InitError(Untranslated(strprintf("Failed to access database %s", dbPath)));
            }
        } catch (const std::exception& e) {
            return InitError(Untranslated(strprintf("Failed to initialize with database %s: %s", dbPath, e.what())));
        }
        return true;
    }

    static BitcoinAddressLookup* Get() { return instance_.get(); }

private:
    bool testDatabaseAccess() {
        std::string test_cmd;
#ifdef _WIN32
        test_cmd = "where sqlite3 >nul 2>&1";
        if (system(test_cmd.c_str()) != 0) {
            LogPrintf("BitcoinAddressLookup: sqlite3 command-line tool not found, can't verify database access\n");
            return true;
        }
        test_cmd = "sqlite3 \"" + db_path_ + "\" \".tables\" >nul 2>&1";
#else
        test_cmd = "which sqlite3 >/dev/null 2>&1";
        if (system(test_cmd.c_str()) != 0) {
            LogPrintf("BitcoinAddressLookup: sqlite3 command-line tool not found, can't verify database access\n");
            return true;
        }
        test_cmd = "sqlite3 \"" + db_path_ + "\" \".tables\" >/dev/null 2>&1";
#endif
        int result = system(test_cmd.c_str());
        if (result != 0) {
            LogPrintf("BitcoinAddressLookup: Failed to access database file: %s\n", db_path_);
            return false;
        }
        return true;
    }

    CAmount loadSingle(const std::string& address) const {
        std::lock_guard<std::mutex> lock(io_mutex_);

        std::string temp_file = db_path_ + ".tmp_output";

        std::string query = "SELECT balance FROM addresses WHERE address = '" + address + "';";
        std::string cmd;

#ifdef _WIN32
        cmd = "sqlite3 \"" + db_path_ + "\" \"" + query + "\" > \"" + temp_file + "\" 2>nul";
#else
        cmd = "sqlite3 \"" + db_path_ + "\" \"" + query + "\" > \"" + temp_file + "\" 2>/dev/null";
#endif

        int result = system(cmd.c_str());
        if (result != 0) {
            LogPrintf("BitcoinAddressLookup: Failed to execute SQLite query\n");
            return 0;
        }

        std::ifstream file(temp_file);
        if (!file.is_open()) {
            LogPrintf("BitcoinAddressLookup: Failed to open temporary result file\n");
            return 0;
        }

        std::string line;
        CAmount balance = 0;
        if (std::getline(file, line)) {
            try {
                balance = static_cast<CAmount>(std::stoull(line));
            } catch (...) {
                LogPrintf("BitcoinAddressLookup: Invalid balance value: %s\n", line);
                balance = 0;
            }
        }

        file.close();

        remove(temp_file.c_str());

        return balance;
    }

    std::string db_path_;
    bool enabled_;
    mutable std::unordered_map<std::string, CAmount> cache_;

    mutable std::mutex io_mutex_;
    mutable std::shared_mutex cache_mutex_;

    static inline std::unique_ptr<BitcoinAddressLookup> instance_;

    BitcoinAddressLookup(const BitcoinAddressLookup&) = delete;
    BitcoinAddressLookup& operator=(const BitcoinAddressLookup&) = delete;
};

#endif // BITCOINADDRESSLOOKUP_H
