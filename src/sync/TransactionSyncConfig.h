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
#include <bcos-framework/interfaces/crypto/KeyInterface.h>
#include <bcos-framework/interfaces/front/FrontServiceInterface.h>
#include <bcos-framework/interfaces/ledger/LedgerInterface.h>
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
    TransactionSyncConfig(bcos::crypto::NodeIDPtr _nodeId,
        bcos::front::FrontServiceInterface::Ptr _frontService,
        bcos::txpool::TxPoolStorageInterface::Ptr _txpoolStorage,
        bcos::sync::TxsSyncMsgFactory::Ptr _msgFactory,
        bcos::protocol::BlockFactory::Ptr _blockFactory,
        std::shared_ptr<bcos::ledger::LedgerInterface> _ledger)
      : m_nodeId(_nodeId),
        m_frontService(_frontService),
        m_txpoolStorage(_txpoolStorage),
        m_msgFactory(_msgFactory),
        m_blockFactory(_blockFactory),
        m_ledger(_ledger),
        m_consensusNodeList(std::make_shared<bcos::consensus::ConsensusNodeList>()),
        m_observerNodeList(std::make_shared<bcos::consensus::ConsensusNodeList>()),
        m_nodeList(std::make_shared<bcos::crypto::NodeIDSet>())
    {}

    virtual ~TransactionSyncConfig() {}

    bcos::front::FrontServiceInterface::Ptr frontService() { return m_frontService; }
    bcos::txpool::TxPoolStorageInterface::Ptr txpoolStorage() { return m_txpoolStorage; }
    bcos::sync::TxsSyncMsgFactory::Ptr msgFactory() { return m_msgFactory; }

    bcos::protocol::BlockFactory::Ptr blockFactory() { return m_blockFactory; }

    unsigned networkTimeout() const { return m_networkTimeout; }
    void setNetworkTimeout(unsigned _networkTimeout) { m_networkTimeout = _networkTimeout; }

    // Note: copy here to remove multithreading issues
    virtual bcos::crypto::NodeIDSet connectedNodeList()
    {
        ReadGuard l(x_connectedNodeList);
        return *m_connectedNodeList;
    }

    virtual void setConnectedNodeList(bcos::crypto::NodeIDSet const& _connectedNodeList)
    {
        WriteGuard l(x_connectedNodeList);
        *m_connectedNodeList = _connectedNodeList;
    }

    virtual void setConnectedNodeList(bcos::crypto::NodeIDSet&& _connectedNodeList)
    {
        WriteGuard l(x_connectedNodeList);
        *m_connectedNodeList = std::move(_connectedNodeList);
    }

    bcos::crypto::NodeIDPtr nodeID() { return m_nodeId; }

    unsigned forwardPercent() const { return m_forwardPercent; }
    void setForwardPercent(unsigned _forwardPercent) { m_forwardPercent = _forwardPercent; }

    // Note: copy here to remove multithreading issues
    virtual bcos::consensus::ConsensusNodeList consensusNodeList()
    {
        ReadGuard l(x_consensusNodeList);
        return *m_consensusNodeList;
    }
    virtual void setConsensusNodeList(bcos::consensus::ConsensusNodeList const& _consensusNodeList)
    {
        {
            WriteGuard l(x_consensusNodeList);
            *m_consensusNodeList = _consensusNodeList;
        }
        updateNodeList();
    }

    virtual void setConsensusNodeList(bcos::consensus::ConsensusNodeList&& _consensusNodeList)
    {
        {
            WriteGuard l(x_consensusNodeList);
            *m_consensusNodeList = std::move(_consensusNodeList);
        }
        updateNodeList();
    }

    virtual void setObserverList(bcos::consensus::ConsensusNodeList const& _observerNodeList)
    {
        {
            WriteGuard l(x_observerNodeList);
            *m_observerNodeList = _observerNodeList;
        }
        updateNodeList();
    }

    bcos::consensus::ConsensusNodeList observerNodeList()
    {
        ReadGuard l(x_observerNodeList);
        return *m_observerNodeList;
    }

    virtual bool existsInGroup()
    {
        ReadGuard l(x_nodeList);
        return m_nodeList->count(m_nodeId);
    }
    std::shared_ptr<bcos::ledger::LedgerInterface> ledger() { return m_ledger; }

private:
    void updateNodeList()
    {
        auto nodeList = consensusNodeList() + observerNodeList();
        WriteGuard l(x_nodeList);
        m_nodeList->clear();
        for (auto node : nodeList)
        {
            m_nodeList->insert(node->nodeID());
        }
    }

private:
    bcos::crypto::NodeIDPtr m_nodeId;
    bcos::front::FrontServiceInterface::Ptr m_frontService;
    bcos::txpool::TxPoolStorageInterface::Ptr m_txpoolStorage;
    bcos::sync::TxsSyncMsgFactory::Ptr m_msgFactory;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    std::shared_ptr<bcos::ledger::LedgerInterface> m_ledger;

    bcos::consensus::ConsensusNodeListPtr m_consensusNodeList;
    SharedMutex x_consensusNodeList;

    bcos::consensus::ConsensusNodeListPtr m_observerNodeList;
    SharedMutex x_observerNodeList;

    bcos::crypto::NodeIDSetPtr m_nodeList;
    SharedMutex x_nodeList;

    bcos::crypto::NodeIDSetPtr m_connectedNodeList;
    SharedMutex x_connectedNodeList;
    unsigned m_networkTimeout = 200;

    unsigned m_forwardPercent = 25;
};
}  // namespace sync
}  // namespace bcos