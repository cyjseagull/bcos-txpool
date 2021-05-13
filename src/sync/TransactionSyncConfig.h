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
 * @brief config for transaction sync
 * @file TransactionSyncConfig.h
 * @author: yujiechen
 * @date 2021-05-11
 */
#pragma once
#include "txpool/interfaces/TxPoolStorageInterface.h"
#include <bcos-framework/interfaces/consensus/ConsensusNodeInterface.h>
#include <bcos-framework/interfaces/front/FrontServiceInterface.h>
#include <bcos-framework/interfaces/protocol/BlockFactory.h>
#include <bcos-framework/libsync/interfaces/TxsSyncMsgFactory.h>
namespace bcos
{
namespace sync
{
class TransactionSyncConfig
{
public:
    using Ptr = std::shared_ptr<TransactionSyncConfig>;
    TransactionSyncConfig(bcos::front::FrontServiceInterface::Ptr _frontService,
        bcos::txpool::TxPoolStorageInterface::Ptr _txpoolStorage,
        bcos::sync::TxsSyncMsgFactory::Ptr _msgFactory,
        bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::consensus::ConsensusNodeList const& _consensusNodes)
      : m_frontService(_frontService),
        m_txpoolStorage(_txpoolStorage),
        m_msgFactory(_msgFactory),
        m_blockFactory(_blockFactory),
        m_consensusNodeList(std::make_shared<bcos::consensus::ConsensusNodeList>(_consensusNodes))
    {}

    virtual ~TransactionSyncConfig() {}

    bcos::front::FrontServiceInterface::Ptr frontService() { return m_frontService; }
    bcos::txpool::TxPoolStorageInterface::Ptr txpoolStorage() { return m_txpoolStorage; }
    bcos::sync::TxsSyncMsgFactory::Ptr msgFactory() { return m_msgFactory; }

    bcos::protocol::BlockFactory::Ptr blockFactory() { return m_blockFactory; }

    unsigned networkTimeout() const { return m_networkTimeout; }
    void setNetworkTimeout(unsigned _networkTimeout) { m_networkTimeout = _networkTimeout; }

    virtual bcos::consensus::ConsensusNodeList consensusNodeList()
    {
        ReadGuard l(x_consensusNodeList);
        return *m_consensusNodeList;
    }
    virtual void setConsensusNodeList(bcos::consensus::ConsensusNodeList const& _consensusNodeList)
    {
        WriteGuard l(x_consensusNodeList);
        *m_consensusNodeList = _consensusNodeList;
    }

    virtual void setConsensusNodeList(bcos::consensus::ConsensusNodeList&& _consensusNodeList)
    {
        WriteGuard l(x_consensusNodeList);
        *m_consensusNodeList = std::move(_consensusNodeList);
    }

private:
    bcos::front::FrontServiceInterface::Ptr m_frontService;
    bcos::txpool::TxPoolStorageInterface::Ptr m_txpoolStorage;
    bcos::sync::TxsSyncMsgFactory::Ptr m_msgFactory;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;

    // TODO: fetch the consensusNodeList
    bcos::consensus::ConsensusNodeListPtr m_consensusNodeList;
    SharedMutex x_consensusNodeList;

    unsigned m_networkTimeout = 200;
};
}  // namespace sync
}  // namespace bcos