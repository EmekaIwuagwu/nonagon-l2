#pragma once

#include <unordered_map>
#include <mutex>
#include "nonagon/types.hpp"

namespace nonagon {
namespace execution {

class StateManager {
public:
    struct Account {
        uint64_t balance{0};
        uint64_t nonce{0};
    };

    StateManager() = default;

    bool apply_transaction(const Transaction& tx) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto& sender = accounts_[tx.from.to_string()];
        
        // Validate nonce
        if (tx.nonce != sender.nonce) {
            return false;
        }

        // Validate balance for amount + gas
        uint64_t total_cost = tx.amount + (tx.gas_limit * tx.gas_price);
        if (sender.balance < total_cost) {
            return false;
        }

        // Apply state changes
        sender.balance -= total_cost;
        sender.nonce++;

        auto& recipient = accounts_[tx.to.to_string()];
        recipient.balance += tx.amount;

        return true;
    }

    uint64_t get_balance(const Address& addr) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = accounts_.find(addr.to_string());
        return (it != accounts_.end()) ? it->second.balance : 0;
    }

    void set_balance(const Address& addr, uint64_t balance) {
        std::lock_guard<std::mutex> lock(mutex_);
        accounts_[addr.to_string()].balance = balance;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Account> accounts_;
};

} // namespace execution
} // namespace nonagon
