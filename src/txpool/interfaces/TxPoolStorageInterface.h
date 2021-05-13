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
 * @brief Storage interface for TxPool
 * @file TxPoolStorageInterface.h
 * @author: yujiechen
 * @date 2021-05-07
 */
#pragma once
#include <bcos-framework/interfaces/protocol/Block.h>
#include <bcos-framework/interfaces/protocol/Transaction.h>
#include <bcos-framework/interfaces/txpool/TxPoolTypeDef.h>
#include <bcos-framework/libprotocol/TransactionStatus.h>

namespace bcos
{
namespace txpool
{
class TxPoolStorageInterface
{
public:
    using Ptr = std::shared_ptr<TxPoolStorageInterface>;
    TxPoolStorageInterface() = default;
    virtual ~TxPoolStorageInterface() {}

    virtual bcos::protocol::TransactionStatus submitTransaction(
        bytesPointer _txData, bcos::protocol::TxSubmitCallback _txSubmitCallback = nullptr) = 0;
    virtual bcos::protocol::TransactionStatus submitTransaction(
        bcos::protocol::Transaction::ConstPtr _tx) = 0;

    virtual bcos::protocol::TransactionStatus insert(bcos::protocol::Transaction::ConstPtr _tx) = 0;
    virtual void batchInsert(bcos::protocol::Transactions const& _txs) = 0;

    virtual bcos::protocol::Transaction::ConstPtr remove(bcos::crypto::HashType const& _txHash) = 0;
    virtual bcos::protocol::Transaction::ConstPtr removeSubmittedTx(
        bcos::protocol::TransactionSubmitResult::Ptr _txSubmitResult) = 0;
    virtual void batchRemove(bcos::protocol::BlockNumber _batchId,
        bcos::protocol::TransactionSubmitResults const& _txsResult) = 0;

    // Note: the transactions may be missing from the transaction pool
    virtual bcos::protocol::ConstTransactionsPtr fetchTxs(
        bcos::crypto::HashList& _missedTxs, bcos::crypto::HashList const& _txsList) = 0;

    /**
     * @brief Get newly inserted transactions from the txpool
     *
     * @param _txsLimit Maximum number of transactions that can be obtained at a time
     * @return List of new transactions
     */
    virtual bcos::protocol::ConstTransactionsPtr fetchNewTxs(size_t _txsLimit) = 0;
    virtual bcos::protocol::ConstTransactionsPtr batchFetchTxs(
        size_t _txsLimit, TxsHashSetPtr _avoidTxs, bool _avoidDuplicate = true) = 0;

    virtual bool exist(bcos::crypto::HashType const& _txHash) = 0;

    virtual size_t size() const = 0;
    virtual void clear() = 0;
};
}  // namespace txpool
}  // namespace bcos