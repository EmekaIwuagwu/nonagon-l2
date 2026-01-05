#pragma once

#include <vector>
#include <queue>
#include <mutex>
#include <unordered_map>
#include "nonagon/types.hpp"

namespace nonagon {

class Mempool {
public:
    Mempool() = default;

    bool add_transaction(const Transaction& tx) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Basic validation
        if (transactions_.find(tx.hash().to_hex()) != transactions_.end()) {
            return false; // Already exists
        }

        // TODO: More robust validation (signature, balance, nonce)
        
        transactions_[tx.hash().to_hex()] = tx;
        priority_queue_.push(tx);
        return true;
    }

    std::vector<Transaction> get_batch(size_t max_size) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Transaction> batch;
        
        while (!priority_queue_.empty() && batch.size() < max_size) {
            batch.push_back(priority_queue_.top());
            transactions_.erase(priority_queue_.top().hash().to_hex());
            priority_queue_.pop();
        }
        
        return batch;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return transactions_.size();
    }

private:
    struct TxPriority {
        bool operator()(const Transaction& a, const Transaction& b) const {
            return a.gas_price < b.gas_price; // Higher price = higher priority
        }
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Transaction> transactions_;
    std::priority_queue<Transaction, std::vector<Transaction>, TxPriority> priority_queue_;
};

} // namespace nonagon
