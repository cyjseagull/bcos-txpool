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
 * @brief implementation for transaction sync
 * @file TransactionSync.h
 * @author: yujiechen
 * @date 2021-05-10
 */
#pragma once

#include "sync/TransactionSyncConfig.h"
#include "sync/interfaces/TransactionSyncInterface.h"
#include <bcos-framework/interfaces/protocol/Protocol.h>
#include <bcos-framework/libutilities/ThreadPool.h>
#include <bcos-framework/libutilities/Worker.h>

namespace bcos
{
namespace sync
{
class TransactionSync : public TransactionSyncInterface,
                        public Worker,
                        public std::enable_shared_from_this<TransactionSync>
{
public:
    using Ptr = std::shared_ptr<TransactionSync>;
    explicit TransactionSync(TransactionSyncConfig::Ptr _config)
      : Worker("sync", 0),
        m_config(_config),
        m_downloadTxsBuffer(std::make_shared<TxsSyncMsgList>()),
        m_worker(std::make_shared<ThreadPool>("sync", 1))
    {}

    ~TransactionSync() {}

    void start() override;
    void stop() override;

    using SendResponseCallback = std::function<void(bytesConstRef _respData)>;
    void onRecvSyncMessage(bcos::Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
        bytesPointer _data, SendResponseCallback _sendResponse) override;

    using VerifyResponseCallback = std::function<void(Error::Ptr, bool)>;
    void requestMissedTxs(bcos::crypto::PublicPtr _generatedNodeID,
        bcos::crypto::HashListPtr _missedTxs, VerifyResponseCallback _onVerifyFinished) override;

protected:
    void executeWorker() override;
    virtual void maintainTransactions();
    virtual void broadcastTxsFromRpc(bcos::protocol::ConstTransactionsPtr _txs);
    virtual void forwardTxsFromP2P(bcos::protocol::ConstTransactionsPtr _txs);

    virtual void maintainDownloadingTransactions();

    virtual void onReceiveTxsRequest(
        TxsSyncMsgInterface::Ptr _txsRequest, SendResponseCallback _sendResponse);
    virtual void verifyFetchedTxs(Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, bcos::crypto::HashListPtr _missedTxs,
        VerifyResponseCallback _onVerifyFinished);

    virtual bool downloadTxsBufferEmpty()
    {
        ReadGuard l(x_downloadTxsBuffer);
        return (m_downloadTxsBuffer->size() == 0);
    }

    virtual void appendDownTxsBuffer(TxsSyncMsgInterface::Ptr _txsBuffer)
    {
        WriteGuard l(x_downloadTxsBuffer);
        m_downloadTxsBuffer->emplace_back(_txsBuffer);
    }

    virtual TxsSyncMsgListPtr swapDownloadTxsBuffer()
    {
        UpgradableGuard l(x_downloadTxsBuffer);
        auto localBuffer = m_downloadTxsBuffer;
        UpgradeGuard ul(l);
        m_downloadTxsBuffer = std::make_shared<TxsSyncMsgList>();
        return localBuffer;
    }
    virtual bool importDownloadedTxs(bcos::protocol::Block::Ptr _txsBuffer);

private:
    TransactionSyncConfig::Ptr m_config;
    TxsSyncMsgListPtr m_downloadTxsBuffer;
    SharedMutex x_downloadTxsBuffer;
    ThreadPool::Ptr m_worker;

    std::atomic_bool m_running = {false};

    std::atomic_bool m_needMaintainTransactions = {true};
    std::atomic_bool m_newTransactions = {false};

    // signal to notify all thread to work
    boost::condition_variable m_signalled;
    // mutex to access m_signalled
    boost::mutex x_signalled;
};
}  // namespace sync
}  // namespace bcos