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
 * @file TransactionSync.cpp
 * @author: yujiechen
 * @date 2021-05-11
 */
#include "TransactionSync.h"
#include "sync/utilities/Common.h"
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-framework/interfaces/protocol/Protocol.h>

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::crypto;
using namespace bcos::txpool;
using namespace bcos::protocol;

static unsigned const c_maxSendTransactions = 1000;

void TransactionSync::start()
{
    startWorking();
    m_running.store(true);
    SYNC_LOG(DEBUG) << LOG_DESC("start TransactionSync");
}

void TransactionSync::stop()
{
    if (!m_running)
    {
        SYNC_LOG(DEBUG) << LOG_DESC("TransactionSync already stopped");
        return;
    }

    m_running.store(false);
    finishWorker();
    stopWorking();
    // will not restart worker, so terminate it
    terminate();
    SYNC_LOG(DEBUG) << LOG_DESC("stop SyncTransaction");
}

void TransactionSync::executeWorker()
{
    if (!downloadTxsBufferEmpty())
    {
        maintainDownloadingTransactions();
    }
    if (m_config->existsInGroup() && m_newTransactions && downloadTxsBufferEmpty())
    {
        maintainTransactions();
    }
    if (!m_newTransactions && downloadTxsBufferEmpty())
    {
        boost::unique_lock<boost::mutex> l(x_signalled);
        m_signalled.wait_for(l, boost::chrono::milliseconds(10));
    }
}

void TransactionSync::onRecvSyncMessage(
    Error::Ptr _error, NodeIDPtr _nodeID, bytesPointer _data, SendResponseCallback _sendResponse)
{
    try
    {
        if (_error->errorCode() != CommonError::SUCCESS)
        {
            SYNC_LOG(WARNING) << LOG_DESC("onRecvSyncMessage error")
                              << LOG_KV("errorCode", _error->errorCode())
                              << LOG_KV("errorMsg", _error->errorMessage());
            return;
        }
        auto txsSyncMsg = m_config->msgFactory()->createTxsSyncMsg(ref(*_data));
        // receive transactions
        if (txsSyncMsg->type() == TxsSyncPacketType::TxsPacket)
        {
            txsSyncMsg->setFrom(_nodeID);
            appendDownTxsBuffer(txsSyncMsg);
            m_signalled.notify_all();
            return;
        }
        // receive txs request, and response the transactions
        if (txsSyncMsg->type() == TxsSyncPacketType::TxsRequestPacket)
        {
            auto self = std::weak_ptr<TransactionSync>(shared_from_this());
            m_worker->enqueue([self, txsSyncMsg, _sendResponse, _nodeID]() {
                try
                {
                    auto transactionSync = self.lock();
                    if (!transactionSync)
                    {
                        return;
                    }
                    transactionSync->onReceiveTxsRequest(txsSyncMsg, _sendResponse);
                }
                catch (std::exception const& e)
                {
                    SYNC_LOG(WARNING) << LOG_DESC("onRecvSyncMessage: send txs response exception")
                                      << LOG_KV("error", boost::diagnostic_information(e))
                                      << LOG_KV("peer", _nodeID->shortHex());
                }
            });
        }
        if (txsSyncMsg->type() == TxsSyncPacketType::TxsStatusPacket)
        {
            auto self = std::weak_ptr<TransactionSync>(shared_from_this());
            m_txsRequester->enqueue([self, _nodeID, txsSyncMsg]() {
                try
                {
                    auto transactionSync = self.lock();
                    if (!transactionSync)
                    {
                        return;
                    }
                    transactionSync->onPeerTxsStatus(_nodeID, txsSyncMsg);
                }
                catch (std::exception const& e)
                {
                    SYNC_LOG(WARNING) << LOG_DESC("onRecvSyncMessage:  onPeerTxsStatus exception")
                                      << LOG_KV("error", boost::diagnostic_information(e))
                                      << LOG_KV("peer", _nodeID->shortHex());
                }
            });
        }
    }
    catch (std::exception const& e)
    {
        SYNC_LOG(WARNING) << LOG_DESC("onRecvSyncMessage exception")
                          << LOG_KV("error", boost::diagnostic_information(e))
                          << LOG_KV("peer", _nodeID->shortHex());
    }
}

void TransactionSync::onReceiveTxsRequest(
    TxsSyncMsgInterface::Ptr _txsRequest, SendResponseCallback _sendResponse)
{
    auto const& txsHash = _txsRequest->txsHash();
    HashList missedTxs;
    auto txs = m_config->txpoolStorage()->fetchTxs(missedTxs, txsHash);
    if (missedTxs.size() > 0)
    {
        SYNC_LOG(WARNING) << LOG_DESC("onReceiveTxsRequest: fetch txs from local txpool")
                          << LOG_KV("missedTxs", missedTxs.size());
    }
    // response the txs
    auto block = m_config->blockFactory()->createBlock();
    size_t i = 0;
    for (auto constTx : *txs)
    {
        auto tx = std::const_pointer_cast<Transaction>(constTx);
        block->setTransaction(i, tx);
    }
    bytesPointer txsData = std::make_shared<bytes>();
    block->encode(*txsData);
    auto txsResponse = m_config->msgFactory()->createTxsSyncMsg(
        TxsSyncPacketType::TxsResponsePacket, std::move(*txsData));
    auto packetData = txsResponse->encode();
    _sendResponse(ref(*packetData));
}

void TransactionSync::requestMissedTxs(PublicPtr _generatedNodeID, HashListPtr _missedTxs,
    std::function<void(Error::Ptr, bool)> _onVerifyFinished)
{
    auto txsRequest =
        m_config->msgFactory()->createTxsSyncMsg(TxsSyncPacketType::TxsRequestPacket, *_missedTxs);
    auto encodedData = txsRequest->encode();
    auto self = std::weak_ptr<TransactionSync>(shared_from_this());
    m_config->frontService()->asyncSendMessageByNodeID(ModuleID::TxsSync, _generatedNodeID,
        ref(*encodedData), m_config->networkTimeout(),
        [self, _missedTxs, _onVerifyFinished](
            Error::Ptr _error, NodeIDPtr _nodeID, bytesConstRef _data, SendResponseCallback) {
            try
            {
                auto transactionSync = self.lock();
                if (!transactionSync)
                {
                    return;
                }
                transactionSync->verifyFetchedTxs(_error, _nodeID, _data, _missedTxs,
                    [_onVerifyFinished](Error::Ptr _error, bool _result) {
                        if (!_onVerifyFinished)
                        {
                            return;
                        }
                        _onVerifyFinished(_error, _result);

                        SYNC_LOG(DEBUG) << LOG_DESC("requestMissedTxs: response verify result")
                                        << LOG_KV("_result", _result)
                                        << LOG_KV("retCode", _error->errorMessage());
                    });
            }
            catch (std::exception const& e)
            {
                SYNC_LOG(WARNING)
                    << LOG_DESC(
                           "requestMissedTxs: verifyFetchedTxs when recv txs response exception")
                    << LOG_KV("error", boost::diagnostic_information(e))
                    << LOG_KV("_peer", _nodeID->shortHex());
            }
        });
}

void TransactionSync::verifyFetchedTxs(Error::Ptr _error, NodeIDPtr _nodeID, bytesConstRef _data,
    HashListPtr _missedTxs, VerifyResponseCallback _onVerifyFinished)
{
    if (_error->errorCode() != CommonError::SUCCESS)
    {
        SYNC_LOG(WARNING) << LOG_DESC("asyncVerifyBlock: fetch missed txs failed")
                          << LOG_KV("peer", _nodeID->shortHex())
                          << LOG_KV("missedTxsSize", _missedTxs->size())
                          << LOG_KV("errorCode", _error->errorCode())
                          << LOG_KV("errorMsg", _error->errorMessage());
        _onVerifyFinished(_error, false);
        return;
    }
    auto txsResponse = m_config->msgFactory()->createTxsSyncMsg(_data);
    auto error = std::make_shared<Error>(CommonError::SUCCESS, "Success");
    if (txsResponse->type() != TxsSyncPacketType::TxsResponsePacket)
    {
        SYNC_LOG(WARNING) << LOG_DESC("requestMissedTxs: receive invalid txsResponse")
                          << LOG_KV("peer", _nodeID->shortHex())
                          << LOG_KV("expectedType", TxsSyncPacketType::TxsResponsePacket)
                          << LOG_KV("recvType", txsResponse->type());
        error = std::make_shared<Error>(
            CommonError::FetchTransactionsFailed, "FetchTransactionsFailed");
        _onVerifyFinished(error, false);
        return;
    }
    // verify missedTxs
    bool verifyResponsed = false;
    auto transactions = m_config->blockFactory()->createBlock(txsResponse->txsData(), true, false);
    if (_missedTxs->size() != transactions->transactionsSize())
    {
        // response the verify result
        error = std::make_shared<Error>(CommonError::TransactionsMissing, "TransactionsMissing");
        _onVerifyFinished(error, false);
        verifyResponsed = true;
    }
    // try to import the transactions even when verify failed
    if (!importDownloadedTxs(_nodeID, transactions))
    {
        error = std::make_shared<Error>(
            CommonError::TxsSignatureVerifyFailed, "TxsSignatureVerifyFailed");
        if (!verifyResponsed)
        {
            _onVerifyFinished(error, false);
        }
        return;
    }
    // check the transaction hash
    for (size_t i = 0; i < _missedTxs->size(); i++)
    {
        if ((*_missedTxs)[i] != transactions->transaction(i)->hash())
        {
            error = std::make_shared<Error>(
                CommonError::InconsistentTransactions, "InconsistentTransactions");
            _onVerifyFinished(error, false);
            return;
        }
    }
    _onVerifyFinished(error, true);
    SYNC_LOG(DEBUG) << LOG_DESC("requestMissedTxs and response the verify result");
}

void TransactionSync::maintainDownloadingTransactions()
{
    if (downloadTxsBufferEmpty())
    {
        return;
    }
    auto localBuffer = swapDownloadTxsBuffer();
    if (!m_config->existsInGroup())
    {
        SYNC_LOG(DEBUG)
            << LOG_DESC(
                   "stop maintainDownloadingTransactions for the node is not belong to the group")
            << LOG_KV("txpoolSize", m_config->txpoolStorage()->size())
            << LOG_KV("shardSize", m_downloadTxsBuffer->size());
        return;
    }
    for (size_t i = 0; i < localBuffer->size(); ++i)
    {
        auto txsBuffer = (*localBuffer)[i];
        auto transactions =
            m_config->blockFactory()->createBlock(txsBuffer->txsData(), true, false);
        importDownloadedTxs(txsBuffer->from(), transactions);
    }
}

bool TransactionSync::importDownloadedTxs(
    bcos::crypto::NodeIDPtr _fromNode, Block::Ptr _transactions)
{
    auto txsSize = _transactions->transactionsSize();
    // Note: only need verify the signature for the transactions
    std::atomic_bool verifySuccess = true;
    // verify the transactions
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, txsSize), [&](const tbb::blocked_range<size_t>& _r) {
            for (size_t i = _r.begin(); i < _r.end(); i++)
            {
                auto tx = _transactions->transaction(i);
                tx->appendKnownNode(_fromNode);
                if (m_config->txpoolStorage()->exist(tx->hash()))
                {
                    continue;
                }
                try
                {
                    tx->verify();
                }
                catch (std::exception const& e)
                {
                    tx->setInvalid(true);
                    verifySuccess = false;
                    SYNC_LOG(WARNING) << LOG_DESC("verify sender for tx failed")
                                      << LOG_KV("reason", boost::diagnostic_information(e))
                                      << LOG_KV("hash", tx->hash().abridged());
                }
            }
        });
    // import the transactions into txpool
    auto txpool = m_config->txpoolStorage();
    size_t successImportTxs = 0;
    for (size_t i = 0; i < txsSize; i++)
    {
        auto tx = _transactions->transaction(i);
        if (tx->invalid())
        {
            continue;
        }
        auto result = txpool->submitTransaction(tx);
        if (result != TransactionStatus::None)
        {
            SYNC_LOG(TRACE) << LOG_BADGE("importDownloadedTxs")
                            << LOG_DESC("Import transaction into txpool failed")
                            << LOG_KV("errorCode", result) << LOG_KV("tx", tx->hash().abridged());
            continue;
        }
        successImportTxs++;
    }
    SYNC_LOG(DEBUG) << LOG_DESC("maintainDownloadingTransactions success")
                    << LOG_KV("successImportTxs", successImportTxs) << LOG_KV("totalTxs", txsSize);
    return verifySuccess;
}

void TransactionSync::maintainTransactions()
{
    auto txs = m_config->txpoolStorage()->fetchNewTxs(c_maxSendTransactions);
    broadcastTxsFromRpc(txs);
    forwardTxsFromP2P(txs);
}

// Randomly select a number of nodes to forward the transaction status
void TransactionSync::forwardTxsFromP2P(ConstTransactionsPtr _txs)
{
    auto consensusNodeList = m_config->consensusNodeList();
    auto connectedNodeList = m_config->connectedNodeList();
    auto expectedPeers = (consensusNodeList.size() * m_config->forwardPercent() + 99) / 100;
    std::map<NodeIDPtr, HashListPtr, KeyCompare> peerToForwardedTxs;
    for (auto tx : *_txs)
    {
        auto selectedPeers = selectPeers(tx, connectedNodeList, consensusNodeList, expectedPeers);
        for (auto peer : *selectedPeers)
        {
            if (!peerToForwardedTxs.count(peer))
            {
                peerToForwardedTxs[peer] = std::make_shared<HashList>();
            }
            peerToForwardedTxs[peer]->emplace_back(tx->hash());
        }
    }
    // broadcast the txsStatus
    for (auto const& it : peerToForwardedTxs)
    {
        auto peer = it.first;
        auto txsHash = it.second;
        auto txsStatus =
            m_config->msgFactory()->createTxsSyncMsg(TxsSyncPacketType::TxsStatusPacket, *txsHash);
        auto packetData = txsStatus->encode();
        m_config->frontService()->asyncSendMessageByNodeID(
            ModuleID::TxsSync, peer, ref(*packetData), 0, nullptr);
    }
}

NodeIDListPtr TransactionSync::selectPeers(Transaction::ConstPtr _tx,
    NodeIDSet const& _connectedPeers, bcos::consensus::ConsensusNodeList const& _consensusNodeList,
    size_t _expectedSize)
{
    auto selectedPeers = std::make_shared<NodeIDs>();
    for (auto consensusNode : _consensusNodeList)
    {
        auto nodeId = consensusNode->nodeID();
        // check connection
        if (!_connectedPeers.count(nodeId))
        {
            continue;
        }
        // the node self or not
        if (nodeId->data() == m_config->nodeID()->data())
        {
            continue;
        }
        // check tx existence
        if (_tx->isKnownBy(nodeId))
        {
            continue;
        }
        selectedPeers->emplace_back(nodeId);
        _tx->appendKnownNode(nodeId);
        if (selectedPeers->size() >= _expectedSize)
        {
            break;
        }
    }
    return selectedPeers;
}

void TransactionSync::broadcastTxsFromRpc(bcos::protocol::ConstTransactionsPtr _txs)
{
    auto block = m_config->blockFactory()->createBlock();
    // get the transactions from RPC
    size_t index = 0;
    for (auto tx : *_txs)
    {
        if (!tx->submitCallback())
        {
            continue;
        }
        block->setTransaction(index++, std::const_pointer_cast<Transaction>(tx));
    }
    // broadcast the txs to all consensus node
    auto encodedData = std::make_shared<bytes>();
    block->encode(*encodedData);
    auto txsPacket = m_config->msgFactory()->createTxsSyncMsg(
        TxsSyncPacketType::TxsPacket, std::move(*encodedData));
    auto packetData = txsPacket->encode();
    auto consensusNodeList = m_config->consensusNodeList();
    for (auto const& consensusNode : consensusNodeList)
    {
        m_config->frontService()->asyncSendMessageByNodeID(
            ModuleID::TxsSync, consensusNode->nodeID(), ref(*packetData), 0, nullptr);
        SYNC_LOG(DEBUG) << LOG_DESC("broadcastTxsFromRpc")
                        << LOG_KV("toNodeId", consensusNode->nodeID()->shortHex())
                        << LOG_KV("txsNum", block->transactionsSize())
                        << LOG_KV("messageSize(B)", packetData->size());
    }
}

void TransactionSync::onPeerTxsStatus(
    bcos::crypto::NodeIDPtr _fromNode, TxsSyncMsgInterface::Ptr _txsStatus)
{
    // insert all downloaded transaction into the txpool
    while (!downloadTxsBufferEmpty())
    {
        maintainDownloadingTransactions();
    }
    if (_txsStatus->txsHash().size() == 0)
    {
        return;
    }
    auto requestTxs = m_config->txpoolStorage()->filterUnknownTxs(_txsStatus->txsHash(), _fromNode);
    if (requestTxs->size() == 0)
    {
        return;
    }
    requestMissedTxs(_fromNode, requestTxs, nullptr);
    SYNC_LOG(DEBUG) << LOG_DESC("onPeerTxsStatus") << LOG_KV("reqSize", requestTxs->size())
                    << LOG_KV("peerTxsSize", _txsStatus->txsHash().size())
                    << LOG_KV("peer", _fromNode->shortHex());
}