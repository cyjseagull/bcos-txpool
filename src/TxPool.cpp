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

void TxPool::start()
{
    m_transactionSync->start();
}

void TxPool::stop()
{
    m_transactionSync->stop();
}

void TxPool::asyncSubmit(bytesPointer _txData, TxSubmitCallback _txSubmitCallback,
    std::function<void(Error::Ptr)> _onRecv)
{
    asyncSubmitTransaction(_txData, _txSubmitCallback);
    if (!_onRecv)
    {
        return;
    }
    _onRecv(nullptr);
}

bool TxPool::checkExistsInGroup(TxSubmitCallback _txSubmitCallback)
{
    auto syncConfig = m_transactionSync->config();
    if (!_txSubmitCallback || syncConfig->existsInGroup())
    {
        return true;
    }
    // notify txResult
    auto txResult = m_config->txResultFactory()->createTxSubmitResult(
        HashType(), (int32_t)TransactionStatus::RequestNotBelongToTheGroup);
    _txSubmitCallback(nullptr, txResult);
    TXPOOL_LOG(WARNING) << LOG_DESC("Do not send transactions to nodes that are not in the group");
    return false;
}

void TxPool::asyncSealTxs(size_t _txsLimit, TxsHashSetPtr _avoidTxs,
    std::function<void(Error::Ptr, HashListPtr)> _sealCallback)
{
    auto fetchedTxs = m_txpoolStorage->batchFetchTxs(_txsLimit, _avoidTxs, true);
    _sealCallback(nullptr, fetchedTxs);
}

void TxPool::asyncFetchNewTxs(
    size_t _txsLimit, std::function<void(Error::Ptr, ConstTransactionsPtr)> _onReceiveNewTxs)
{
    auto fetchedTxs = m_txpoolStorage->fetchNewTxs(_txsLimit);
    _onReceiveNewTxs(nullptr, fetchedTxs);
}

void TxPool::asyncNotifyBlockResult(BlockNumber _blockNumber,
    TransactionSubmitResultsPtr _txsResult, std::function<void(Error::Ptr)> _onNotifyFinished)
{
    m_txpoolStorage->batchRemove(_blockNumber, *_txsResult);
    _onNotifyFinished(nullptr);
}

void TxPool::asyncVerifyBlock(PublicPtr _generatedNodeID, bytesConstRef const& _block,
    std::function<void(Error::Ptr, bool)> _onVerifyFinished)
{
    auto onVerifyFinishedWrapper = [_onVerifyFinished](Error::Ptr _error, bool _ret) {
        if (!_onVerifyFinished)
        {
            return;
        }
        _onVerifyFinished(_error, _ret);
    };

    auto block = m_config->blockFactory()->createBlock(_block);
    size_t txsSize = block->transactionsHashSize();
    if (txsSize == 0)
    {
        onVerifyFinishedWrapper(nullptr, true);
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
    if (missedTxs->size() == 0)
    {
        TXPOOL_LOG(DEBUG) << LOG_DESC("asyncVerifyBlock: hit all transactions in txpool");
        onVerifyFinishedWrapper(nullptr, true);
        return;
    }
    TXPOOL_LOG(DEBUG) << LOG_DESC("asyncVerifyBlock") << LOG_KV("totoalTxs", txsSize)
                      << LOG_KV("missedTxs", missedTxs->size());
    m_transactionSync->requestMissedTxs(_generatedNodeID, missedTxs, onVerifyFinishedWrapper);
}

void TxPool::asyncNotifyTxsSyncMessage(Error::Ptr _error, NodeIDPtr _nodeID, bytesPointer _data,
    std::function<void(bytesConstRef _respData)> _sendResponse,
    std::function<void(Error::Ptr _error)> _onRecv)
{
    m_transactionSync->onRecvSyncMessage(_error, _nodeID, _data, _sendResponse);
    if (!_onRecv)
    {
        return;
    }
    _onRecv(nullptr);
}

void TxPool::notifyConnectedNodes(
    NodeIDSet const& _connectedNodes, std::function<void(Error::Ptr)> _onRecvResponse)
{
    m_transactionSync->config()->setConnectedNodeList(_connectedNodes);
    _onRecvResponse(nullptr);
}


void TxPool::notifyConsensusNodeList(
    ConsensusNodeList const& _consensusNodeList, std::function<void(Error::Ptr)> _onRecvResponse)
{
    m_transactionSync->config()->setConsensusNodeList(_consensusNodeList);
    _onRecvResponse(nullptr);
}

void TxPool::notifyObserverNodeList(
    ConsensusNodeList const& _observerNodeList, std::function<void(Error::Ptr)> _onRecvResponse)
{
    m_transactionSync->config()->setObserverList(_observerNodeList);
    _onRecvResponse(nullptr);
}

// Note: the transaction must be all hit in local txpool
void TxPool::asyncFillBlock(HashListPtr _txsHash,
    std::function<void(Error::Ptr, bcos::protocol::TransactionsPtr)> _onBlockFilled)
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
    _onBlockFilled(nullptr, txs);
}


void TxPool::asyncMarkTxs(
    HashListPtr _txsHash, bool _sealedFlag, std::function<void(Error::Ptr)> _onRecvResponse)
{
    m_txpoolStorage->batchMarkTxs(*_txsHash, _sealedFlag);
    _onRecvResponse(nullptr);
}