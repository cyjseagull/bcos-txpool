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
 * @file TxPool.cpp
 * @author: yujiechen
 * @date 2021-05-10
 */
#include "TxPool.h"
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <tbb/parallel_for.h>
using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::sync;
using namespace bcos::consensus;

void TxPool::asyncSubmit(bytesPointer _txData, TxSubmitCallback _txSubmitCallback)
{
    // verify and try to submit the valid transaction
    auto self = std::weak_ptr<TxPool>(shared_from_this());
    m_worker->enqueue([self, _txData, _txSubmitCallback]() {
        try
        {
            auto txpool = self.lock();
            if (!txpool)
            {
                return;
            }
            auto syncConfig = txpool->m_transactionSync->config();
            auto config = txpool->m_config;
            if (_txSubmitCallback && !syncConfig->existsInGroup())
            {
                // notify txResult
                auto txResult =
                    config->txResultFactory()->createTxSubmitResult(bcos::crypto::HashType(),
                        (int32_t)TransactionStatus::RequestNotBelongToTheGroup);
                auto error = std::make_shared<Error>(CommonError::SUCCESS, "success");
                _txSubmitCallback(error, txResult);
                TXPOOL_LOG(WARNING)
                    << LOG_DESC("Do not send transactions to nodes that are not in the group");
                return;
            }
            auto txpoolStorage = txpool->m_txpoolStorage;
            txpoolStorage->submitTransaction(_txData, _txSubmitCallback);
        }
        catch (std::exception const& e)
        {
            TXPOOL_LOG(WARNING) << LOG_DESC("asyncSubmit excepiton")
                                << LOG_KV("errorInfo", boost::diagnostic_information(e));
        }
    });
}

void TxPool::asyncSealTxs(size_t _txsLimit, TxsHashSetPtr _avoidTxs,
    std::function<void(Error::Ptr, ConstTransactionsPtr)> _sealCallback)
{
    auto fetchedTxs = m_txpoolStorage->batchFetchTxs(_txsLimit, _avoidTxs, true);
    auto error = std::make_shared<Error>(CommonError::SUCCESS, "success");
    _sealCallback(error, fetchedTxs);
}

void TxPool::asyncFetchNewTxs(
    size_t _txsLimit, std::function<void(Error::Ptr, ConstTransactionsPtr)> _onReceiveNewTxs)
{
    auto fetchedTxs = m_txpoolStorage->fetchNewTxs(_txsLimit);
    auto error = std::make_shared<Error>(CommonError::SUCCESS, "success");
    _onReceiveNewTxs(error, fetchedTxs);
}

void TxPool::asyncNotifyBlockResult(bcos::protocol::BlockNumber _blockNumber,
    bcos::protocol::TransactionSubmitResultsPtr _txsResult,
    std::function<void(Error::Ptr)> _onNotifyFinished)
{
    m_txpoolStorage->batchRemove(_blockNumber, *_txsResult);
    auto error = std::make_shared<Error>(CommonError::SUCCESS, "success");
    _onNotifyFinished(error);
}

void TxPool::asyncVerifyBlock(PublicPtr _generatedNodeID, bytesConstRef const& _block,
    std::function<void(Error::Ptr, bool)> _onVerifyFinished)
{
    auto block = m_config->blockFactory()->createBlock(_block);
    size_t txsSize = block->transactionsHashSize();
    if (txsSize == 0)
    {
        _onVerifyFinished(std::make_shared<Error>(CommonError::SUCCESS, "success"), true);
        return;
    }
    auto missedTxs = std::make_shared<HashList>();
    for (size_t i = 0; i < txsSize; i++)
    {
        auto const& txHash = block->transactionHash(i);
        if (!m_txpoolStorage->exist(txHash))
        {
            missedTxs->emplace_back(txHash);
        }
    }
    TXPOOL_LOG(DEBUG) << LOG_DESC("asyncVerifyBlock") << LOG_KV("totoalTxs", txsSize)
                      << LOG_KV("missedTxs", missedTxs->size());

    // TODO: fetch missed transactions from the local ledger
    // fetch missed txs to the _generatedNodeID
    m_transactionSync->requestMissedTxs(_generatedNodeID, missedTxs, _onVerifyFinished);
}

void TxPool::sendTxsSyncMessage(bcos::Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
    bytesPointer _data, std::function<void(bytesConstRef _respData)> _sendResponse)
{
    m_transactionSync->onRecvSyncMessage(_error, _nodeID, _data, _sendResponse);
}

void TxPool::notifyConnectedNodes(
    bcos::crypto::NodeIDSet const& _connectedNodes, std::function<void(Error::Ptr)> _onRecvResponse)
{
    m_transactionSync->config()->setConnectedNodeList(_connectedNodes);
    _onRecvResponse(std::make_shared<Error>(CommonError::SUCCESS, "success"));
}


void TxPool::notifyConsensusNodeList(
    ConsensusNodeList const& _consensusNodeList, std::function<void(Error::Ptr)> _onRecvResponse)
{
    m_transactionSync->config()->setConsensusNodeList(_consensusNodeList);
    _onRecvResponse(std::make_shared<Error>(CommonError::SUCCESS, "success"));
}

void TxPool::notifyObserverNodeList(
    ConsensusNodeList const& _observerNodeList, std::function<void(Error::Ptr)> _onRecvResponse)
{
    m_transactionSync->config()->setObserverList(_observerNodeList);
    _onRecvResponse(std::make_shared<Error>(CommonError::SUCCESS, "success"));
}

// Note: the transaction must be all hit in local txpool
void TxPool::asyncFillBlock(
    HashListPtr _txsHash, std::function<void(Error::Ptr, Block::Ptr)> _onBlockFilled)
{
    HashListPtr missedTxs = std::make_shared<HashList>();
    auto txs = m_txpoolStorage->fetchTxs(*missedTxs, *_txsHash);
    if (missedTxs->size() > 0)
    {
        TXPOOL_LOG(WARNING) << LOG_DESC("asyncFillBlock failed for missing some transactions")
                            << LOG_KV("missedTxsSize", missedTxs->size());
        _onBlockFilled(
            std::make_shared<Error>(CommonError::TransactionsMissing, "TransactionsMissing"),
            nullptr);
        return;
    }
    TXPOOL_LOG(DEBUG) << LOG_DESC("asyncFillBlock: hit all transactions")
                      << LOG_KV("size", txs->size());
    // TODO: the Block provide method to batch fetch txsHash, and batch set transactions
    auto block = m_config->blockFactory()->createBlock();
    for (size_t i = 0; i < txs->size(); i++)
    {
        block->setTransaction(i, std::const_pointer_cast<Transaction>((*txs)[i]));
    }
    _onBlockFilled(std::make_shared<Error>(CommonError::SUCCESS, "SUCCESS"), block);
}
