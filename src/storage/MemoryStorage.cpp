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
#include <tbb/parallel_invoke.h>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;

MemoryStorage::MemoryStorage(TxPoolConfig::Ptr _config) : m_config(_config)
{
    m_notifier = std::make_shared<ThreadPool>("txNotifier", m_config->notifierWorkerNum());
}

bool MemoryStorage::insert(Transaction::Ptr _tx)
{
    if (size() >= m_config->poolLimit())
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

Transaction::Ptr MemoryStorage::removeSubmittedTx(TransactionSubmitResult::Ptr _txSubmitResult)
{
    auto tx = remove(_txSubmitResult->txHash());
    if (!tx)
    {
        return nullptr;
    }
    notifyTxResult(tx, _txSubmitResult);
    return tx;
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

void MemoryStorage::batchRemove(BlockNumber _batchId, TransactionSubmitResults const& _txsResult)
{
    NonceListPtr nonceList = std::make_shared<NonceList>();
    for (auto txResult : _txsResult)
    {
        auto tx = removeSubmittedTx(txResult);
        nonceList->emplace_back(tx->nonce());
    }
    // update the ledger nonce
    m_config->txPoolNonceChecker()->batchInsert(_batchId, nonceList);
    // update the txpool nonce
    m_config->txPoolNonceChecker()->batchRemove(*nonceList);
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

TransactionsPtr MemoryStorage::fetchNewTxs(size_t _txsLimit)
{
    auto fetchedTxs = std::make_shared<Transactions>();
    for (auto tx : m_txsQueue)
    {
        if (tx->synced())
        {
            continue;
        }
        tx->setSynced(true);
        fetchedTxs->emplace_back(tx);
        if (fetchedTxs->size() >= _txsLimit)
        {
            break;
        }
    }
    return fetchedTxs;
}

TransactionsPtr MemoryStorage::batchFetchTxs(
    size_t _txsLimit, TxsHashSetPtr _avoidTxs, bool _avoidDuplicate)
{
    auto fetchedTxs = std::make_shared<Transactions>();
    for (auto tx : m_txsQueue)
    {
        if (_avoidDuplicate && tx->sealed())
        {
            continue;
        }
        auto txHash = tx->hash();
        if (m_invalidTxs.count(txHash))
        {
            continue;
        }
        auto result = m_config->txValidator()->duplicateTx(tx);
        if (result == TransactionStatus::NonceCheckFail)
        {
            continue;
        }
        if (result == TransactionStatus::BlockLimitCheckFail)
        {
            m_invalidTxs.insert(txHash);
            m_invalidNonces.insert(tx->nonce());
            continue;
        }
        if (_avoidTxs && _avoidTxs->count(txHash))
        {
            continue;
        }
        fetchedTxs->emplace_back(tx);
        tx->setSealed(true);
        if (fetchedTxs->size() >= _txsLimit)
        {
            break;
        }
    }
    removeInvalidTxs();
    return fetchedTxs;
}

void MemoryStorage::removeInvalidTxs()
{
    auto self = std::weak_ptr<MemoryStorage>(shared_from_this());
    m_notifier->enqueue([self]() {
        try
        {
            auto memoryStorage = self.lock();
            if (!memoryStorage)
            {
                return;
            }
            if (memoryStorage->m_invalidTxs.size() == 0)
            {
                return;
            }
            tbb::parallel_invoke(
                [memoryStorage]() {
                    // remove invalid txs
                    for (auto const& txHash : memoryStorage->m_invalidTxs)
                    {
                        auto txResult =
                            memoryStorage->m_config->txResultFactory()->createTxSubmitResult(
                                txHash, (int32_t)TransactionStatus::BlockLimitCheckFail);
                        memoryStorage->removeSubmittedTx(txResult);
                    }
                },
                [memoryStorage]() {
                    // remove invalid nonce
                    for (auto const& nonce : memoryStorage->m_invalidNonces)
                    {
                        memoryStorage->m_config->txPoolNonceChecker()->remove(nonce);
                    }
                });
            memoryStorage->m_invalidTxs.clear();
            memoryStorage->m_invalidNonces.clear();
        }
        catch (std::exception const& e)
        {
            TXPOOL_LOG(WARNING) << LOG_DESC("removeInvalidTxs exception")
                                << LOG_KV("errorInfo", boost::diagnostic_information(e));
        }
    });
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