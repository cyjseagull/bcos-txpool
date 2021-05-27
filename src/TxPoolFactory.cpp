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
 * @brief factory to create txpool
 * @file TxPoolFactory.cpp
 * @author: yujiechen
 * @date 2021-05-19
 */
#include "TxPoolFactory.h"
#include "TxPool.h"
#include "sync/TransactionSync.h"
#include "txpool/storage/MemoryStorage.h"
#include "txpool/validator/LedgerNonceChecker.h"
#include "txpool/validator/TxPoolNonceChecker.h"
#include "txpool/validator/TxValidator.h"
#include <bcos-framework/libsync/protocol/PB/TxsSyncMsgFactoryImpl.h>
#include <bcos-framework/libtool/LedgerConfigFetcher.h>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::sync;
using namespace bcos::tool;
using namespace bcos::crypto;
using namespace bcos::protocol;

TxPoolFactory::TxPoolFactory(NodeIDPtr _nodeId, CryptoSuite::Ptr _cryptoSuite,
    TransactionSubmitResultFactory::Ptr _txResultFactory, BlockFactory::Ptr _blockFactory,
    bcos::front::FrontServiceInterface::Ptr _frontService,
    bcos::ledger::LedgerInterface::Ptr _ledger, std::string const& _groupId,
    std::string const& _chainId, int64_t _blockLimit)
  : m_blockLimit(_blockLimit)
{
    TXPOOL_LOG(INFO) << LOG_DESC("create transaction validator");
    auto txpoolNonceChecker = std::make_shared<TxPoolNonceChecker>();
    auto validator =
        std::make_shared<TxValidator>(txpoolNonceChecker, _cryptoSuite, _groupId, _chainId);

    TXPOOL_LOG(INFO) << LOG_DESC("create transaction config");
    m_txpoolConfig = std::make_shared<TxPoolConfig>(
        validator, _txResultFactory, _blockFactory, _ledger, txpoolNonceChecker);
    TXPOOL_LOG(INFO) << LOG_DESC("create transaction storage");
    auto txpoolStorage = std::make_shared<MemoryStorage>(m_txpoolConfig);

    auto syncMsgFactory = std::make_shared<TxsSyncMsgFactoryImpl>();
    TXPOOL_LOG(INFO) << LOG_DESC("create sync config");
    m_txsSyncConfig = std::make_shared<TransactionSyncConfig>(
        _nodeId, _frontService, txpoolStorage, syncMsgFactory, _blockFactory, _ledger);
    TXPOOL_LOG(INFO) << LOG_DESC("create sync engine");
    auto txsSync = std::make_shared<TransactionSync>(m_txsSyncConfig);

    TXPOOL_LOG(INFO) << LOG_DESC("create txpool");
    m_txpool = std::make_shared<TxPool>(m_txpoolConfig, txpoolStorage, txsSync);
    TXPOOL_LOG(INFO) << LOG_DESC("create txpool success");
}

void TxPoolFactory::init(bcos::sealer::SealerInterface::Ptr _sealer)
{
    m_txpoolConfig->setSealer(_sealer);
    auto ledgerConfigFetcher = std::make_shared<LedgerConfigFetcher>(m_txpoolConfig->ledger());
    TXPOOL_LOG(INFO) << LOG_DESC("fetch LedgerConfig information");
    ledgerConfigFetcher->fetchBlockNumberAndHash();
    ledgerConfigFetcher->fetchConsensusNodeList();
    ledgerConfigFetcher->fetchObserverNodeList();
    ledgerConfigFetcher->waitFetchFinished();
    TXPOOL_LOG(INFO) << LOG_DESC("fetch LedgerConfig success");

    auto ledgerConfig = ledgerConfigFetcher->ledgerConfig();
    auto startNumber = (ledgerConfig->blockNumber() > m_blockLimit ?
                            (ledgerConfig->blockNumber() - m_blockLimit + 1) :
                            0);
    auto toNumber = ledgerConfig->blockNumber();
    TXPOOL_LOG(INFO) << LOG_DESC("fetch history nonces information")
                     << LOG_KV("startNumber", startNumber) << LOG_KV("toNumber", toNumber);
    ledgerConfigFetcher->fetchNonceList(startNumber, m_blockLimit);
    ledgerConfigFetcher->waitFetchFinished();
    TXPOOL_LOG(INFO) << LOG_DESC("fetch history nonces success");

    // create LedgerNonceChecker and set it into the validator
    TXPOOL_LOG(INFO) << LOG_DESC("init txs validator");
    auto ledgerNonceChecker = std::make_shared<LedgerNonceChecker>(
        *(ledgerConfigFetcher->nonceList()), ledgerConfig->blockNumber(), m_blockLimit);

    auto validator = std::dynamic_pointer_cast<TxValidator>(m_txpoolConfig->txValidator());
    validator->setLedgerNonceChecker(ledgerNonceChecker);
    TXPOOL_LOG(INFO) << LOG_DESC("init txs validator success");

    // init syncConfig
    TXPOOL_LOG(INFO) << LOG_DESC("init sync config");
    m_txsSyncConfig->setConsensusNodeList(ledgerConfig->consensusNodeList());
    m_txsSyncConfig->setObserverList(ledgerConfig->observerNodeList());
    TXPOOL_LOG(INFO) << LOG_DESC("init sync config success");
    // TODO: fetch the connected nodeID from frontService
}