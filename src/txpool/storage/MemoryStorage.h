/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief an implementation of using memory to store transactions
 * @file MemoryStorage.h
 * @author: yujiechen
 * @date 2021-05-07
 */
#pragma once
#include "TxPoolConfig.h"
#include <bcos-framework/libutilities/ThreadPool.h>
#include <tbb/concurrent_unordered_map.h>
#define TBB_PREVIEW_CONCURRENT_ORDERED_CONTAINERS 1
#include <tbb/concurrent_set.h>
namespace bcos
{
namespace txpool
{
struct TransactionCompare
{
    bool operator()(bcos::protocol::Transaction::ConstPtr _first,
        bcos::protocol::Transaction::ConstPtr _second) const
    {
        return _first->importTime() <= _second->importTime();
    }
};
class MemoryStorage : public TxPoolStorageInterface,
                      public std::enable_shared_from_this<MemoryStorage>
{
public:
    explicit MemoryStorage(TxPoolConfig::Ptr _config);
    ~MemoryStorage() override {}

    bcos::protocol::TransactionStatus submitTransaction(bytesPointer _txData,
        bcos::protocol::TxSubmitCallback _txSubmitCallback = nullptr) override;
    bcos::protocol::TransactionStatus submitTransaction(bcos::protocol::Transaction::Ptr _tx,
        bcos::protocol::TxSubmitCallback _txSubmitCallback = nullptr) override;

    bcos::protocol::TransactionStatus insert(bcos::protocol::Transaction::ConstPtr _tx) override;
    void batchInsert(bcos::protocol::Transactions const& _txs) override;

    bcos::protocol::Transaction::ConstPtr remove(bcos::crypto::HashType const& _txHash) override;
    void batchRemove(bcos::protocol::BlockNumber _batchId,
        bcos::protocol::TransactionSubmitResults const& _txsResult) override;
    bcos::protocol::Transaction::ConstPtr removeSubmittedTx(
        bcos::protocol::TransactionSubmitResult::Ptr _txSubmitResult) override;

    bcos::protocol::TransactionsPtr fetchTxs(
        bcos::crypto::HashList& _missedTxs, bcos::crypto::HashList const& _txsList) override;

    bcos::protocol::ConstTransactionsPtr fetchNewTxs(size_t _txsLimit) override;
    bcos::crypto::HashListPtr batchFetchTxs(
        size_t _txsLimit, TxsHashSetPtr _avoidTxs, bool _avoidDuplicate = true) override;

    bool exist(bcos::crypto::HashType const& _txHash) override
    {
        ReadGuard l(x_txpoolMutex);
        return m_txsTable.count(_txHash);
    }
    size_t size() const override;
    size_t unSealedTxsSize() override;
    void clear() override;

    bcos::crypto::HashListPtr filterUnknownTxs(
        bcos::crypto::HashList const& _txsHashList, bcos::crypto::NodeIDPtr _peer) override;

    void batchMarkTxs(bcos::crypto::HashList const& _txsHashList, bool _sealFlag) override;

protected:
    bcos::protocol::TransactionStatus txpoolStorageCheck(bcos::protocol::Transaction::ConstPtr _tx);

    virtual bcos::protocol::Transaction::ConstPtr removeWithoutLock(
        bcos::crypto::HashType const& _txHash);
    virtual bcos::protocol::Transaction::ConstPtr removeSubmittedTxWithoutLock(
        bcos::protocol::TransactionSubmitResult::Ptr _txSubmitResult);

    virtual void notifyInvalidReceipt(bcos::crypto::HashType const& _txHash,
        bcos::protocol::TransactionStatus _status,
        bcos::protocol::TxSubmitCallback _txSubmitCallback);

    virtual void notifyTxResult(bcos::protocol::Transaction::ConstPtr _tx,
        bcos::protocol::TransactionSubmitResult::Ptr _txSubmitResult);

    virtual void removeInvalidTxs();

    virtual void preCommitTransaction(bcos::protocol::Transaction::ConstPtr _tx);

    virtual void notifyUnsealedTxsSize();

private:
    TxPoolConfig::Ptr m_config;
    ThreadPool::Ptr m_notifier;
    ThreadPool::Ptr m_worker;

    tbb::concurrent_unordered_map<bcos::crypto::HashType, bcos::protocol::Transaction::ConstPtr,
        std::hash<bcos::crypto::HashType>>
        m_txsTable;

    mutable SharedMutex x_txpoolMutex;

    tbb::concurrent_set<bcos::crypto::HashType> m_invalidTxs;
    tbb::concurrent_set<bcos::protocol::NonceType> m_invalidNonces;

    tbb::concurrent_set<bcos::crypto::HashType> m_missedTxs;
    mutable SharedMutex x_missedTxs;
    std::atomic<size_t> m_sealedTxsSize = {0};
};
}  // namespace txpool
}  // namespace bcos