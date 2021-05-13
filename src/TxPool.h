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
 * @brief implementation for txpool
 * @file TxPool.h
 * @author: yujiechen
 * @date 2021-05-10
 */
#include "TxPoolConfig.h"
#include "sync/interfaces/TransactionSyncInterface.h"
#include "txpool/interfaces/TxPoolStorageInterface.h"
#include <bcos-framework/interfaces/txpool/TxPoolInterface.h>
#include <bcos-framework/libutilities/ThreadPool.h>
namespace bcos
{
namespace txpool
{
class TxPool : public TxPoolInterface, public std::enable_shared_from_this<TxPool>
{
public:
    TxPool(TxPoolConfig::Ptr _config, TxPoolStorageInterface::Ptr _txpoolStorage,
        bcos::sync::TransactionSyncInterface::Ptr _transactionSync)
      : m_config(_config), m_txpoolStorage(_txpoolStorage), m_transactionSync(_transactionSync)
    {
        m_worker = std::make_shared<ThreadPool>("submitter", _config->verifyWorkerNum());
    }

    ~TxPool() override {}

    void asyncSubmit(
        bytesPointer _txData, bcos::protocol::TxSubmitCallback _txSubmitCallback) override;

    void asyncSealTxs(size_t _txsLimit, TxsHashSetPtr _avoidTxs,
        std::function<void(Error::Ptr, bcos::protocol::ConstTransactionsPtr)> _sealCallback)
        override;

    void asyncFetchNewTxs(size_t _txsLimit,
        std::function<void(Error::Ptr, bcos::protocol::ConstTransactionsPtr)> _onReceiveNewTxs)
        override;

    void asyncNotifyBlockResult(bcos::protocol::BlockNumber _blockNumber,
        bcos::protocol::TransactionSubmitResultsPtr _txsResult,
        std::function<void(Error::Ptr)> _onNotifyFinished) override;

    void asyncVerifyBlock(bcos::crypto::PublicPtr _generatedNodeID, bytesConstRef const& _block,
        std::function<void(Error::Ptr, bool)> _onVerifyFinished) override;

    void asyncStoreTxs(bcos::crypto::HashType const& _proposalHash, bytesConstRef _txsToStore,
        std::function<void(Error::Ptr)> _onTxsStored) override;

    void asyncFillBlock(
        bcos::protocol::Block::Ptr _block, std::function<void(Error)> _onBlockFilled) override;

    void sendTxsSyncMessage(bcos::Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
        bytesPointer _data, std::function<void(bytesConstRef _respData)> _sendResponse) override;

private:
    TxPoolConfig::Ptr m_config;
    TxPoolStorageInterface::Ptr m_txpoolStorage;
    bcos::sync::TransactionSyncInterface::Ptr m_transactionSync;
    ThreadPool::Ptr m_worker;
};
}  // namespace txpool
}  // namespace bcos