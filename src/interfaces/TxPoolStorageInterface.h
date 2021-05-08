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

    virtual bool insert(bcos::protocol::Transaction::Ptr _tx) = 0;
    virtual void batchInsert(bcos::protocol::Transactions const& _txs) = 0;

    virtual bcos::protocol::Transaction::Ptr remove(bcos::crypto::HashType const& _txHash) = 0;
    virtual void removeSubmittedTx(
        bcos::protocol::TransactionSubmitResult::Ptr _txSubmitResult) = 0;
    virtual void batchRemove(bcos::protocol::TransactionSubmitResults const& _txsResult) = 0;

    // Note: the transactions may be missing from the transaction pool
    virtual bcos::protocol::TransactionsPtr fetchTxs(
        TxsHashSetPtr _missedTxs, TxsHashSetPtr _txsList) = 0;
    virtual bcos::protocol::TransactionsPtr fetchNewTxs() = 0;
    virtual bcos::protocol::TransactionsPtr batchFetchTxs(
        size_t _txsLimit, TxsHashSetPtr _avoidTxs) = 0;

    virtual size_t size() const = 0;
    virtual void clear() = 0;
};
}  // namespace txpool
}  // namespace bcos