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
 * @file MemoryStorage.cpp
 * @author: yujiechen
 * @date 2021-05-07
 */
#include "MemoryStorage.h"
#include <bcos-framework/interfaces/protocol/CommonError.h>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;

MemoryStorage::MemoryStorage(
    TxValidatorInterface::Ptr _txValidator, size_t _poolLimit, size_t _notifierWorkerNum)
  : m_txValidator(_txValidator), m_poolLimit(_poolLimit)
{
    m_notifier = std::make_shared<ThreadPool>("txNotifier", _notifierWorkerNum);
}

bool MemoryStorage::insert(Transaction::Ptr _tx)
{
    if (size() >= m_poolLimit)
    {
        return false;
    }
    auto txHash = _tx->hash();
    if (m_txsTable.count(txHash))
    {
        return false;
    }
    auto txIterator = m_txsQueue.emplace(_tx).first;
    m_txsTable[txHash] = txIterator;
    return true;
}

void MemoryStorage::batchInsert(Transactions const& _txs)
{
    for (auto tx : _txs)
    {
        insert(tx);
    }
}

Transaction::Ptr MemoryStorage::remove(bcos::crypto::HashType const& _txHash)
{
    if (!m_txsTable.count(_txHash))
    {
        return nullptr;
    }
    auto txIterator = m_txsTable[_txHash];
    auto tx = *(txIterator);
    m_txsTable.unsafe_erase(_txHash);
    m_txsQueue.unsafe_erase(txIterator);
    return tx;
}

void MemoryStorage::removeSubmittedTx(TransactionSubmitResult::Ptr _txSubmitResult)
{
    auto tx = remove(_txSubmitResult->txHash());
    if (!tx)
    {
        return;
    }
    notifyTxResult(tx, _txSubmitResult);
}

void MemoryStorage::notifyTxResult(bcos::protocol::Transaction::Ptr _tx,
    bcos::protocol::TransactionSubmitResult::Ptr _txSubmitResult)
{
    auto txSubmitCallback = _tx->submitCallback();
    if (!txSubmitCallback)
    {
        return;
    }
    // notify the transaction result to RPC
    auto self = std::weak_ptr<MemoryStorage>(shared_from_this());
    m_notifier->enqueue([self, _tx, _txSubmitResult, txSubmitCallback]() {
        try
        {
            auto memoryStorage = self.lock();
            if (!memoryStorage)
            {
                return;
            }
            auto error = std::make_shared<Error>(CommonError::SUCCESS, "success");
            txSubmitCallback(error, _txSubmitResult);
            // TODO: remove this log
            TXPOOL_LOG(TRACE) << LOG_DESC("notify submit result")
                              << LOG_KV("tx", _tx->hash().abridged());
        }
        catch (std::exception const& e)
        {
            TXPOOL_LOG(WARNING) << LOG_DESC("notifyTxResult failed")
                                << LOG_KV("tx", _tx->hash().abridged())
                                << LOG_KV("errorInfo", boost::diagnostic_information(e));
        }
    });
}

void MemoryStorage::batchRemove(TransactionSubmitResults const& _txsResult)
{
    for (auto txResult : _txsResult)
    {
        removeSubmittedTx(txResult);
    }
}

TransactionsPtr MemoryStorage::fetchTxs(TxsHashSetPtr _missedTxs, TxsHashSetPtr _txs)
{
    auto fetchedTxs = std::make_shared<Transactions>();
    for (auto const& hash : *_txs)
    {
        if (!m_txsTable.count(hash))
        {
            _missedTxs->insert(hash);
            continue;
        }
        auto tx = *(m_txsTable[hash]);
        fetchedTxs->emplace_back(tx);
    }
    return fetchedTxs;
}

TransactionsPtr MemoryStorage::fetchNewTxs()
{
    return nullptr;
}

TransactionsPtr MemoryStorage::batchFetchTxs(size_t, TxsHashSetPtr)
{
    return nullptr;
}

size_t MemoryStorage::size() const
{
    return m_txsTable.size();
}

void MemoryStorage::clear()
{
    // Note: must clear m_txsTable firstly
    m_txsTable.clear();
    m_txsQueue.clear();
}