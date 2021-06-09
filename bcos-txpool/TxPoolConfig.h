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
 * @brief Transaction pool configuration module,
 *        including transaction pool dependent modules and related configuration information
 * @file TxPoolConfig.h
 * @author: yujiechen
 * @date 2021-05-08
 */
#pragma once
#include "bcos-txpool/txpool/interfaces/NonceCheckerInterface.h"
#include "bcos-txpool/txpool/interfaces/TxPoolStorageInterface.h"
#include "bcos-txpool/txpool/interfaces/TxValidatorInterface.h"
#include <bcos-framework/interfaces/ledger/LedgerInterface.h>
#include <bcos-framework/interfaces/protocol/BlockFactory.h>
#include <bcos-framework/interfaces/protocol/TransactionSubmitResultFactory.h>
#include <bcos-framework/interfaces/sealer/SealerInterface.h>
namespace bcos
{
namespace txpool
{
class TxPoolConfig
{
public:
    using Ptr = std::shared_ptr<TxPoolConfig>;
    TxPoolConfig(TxValidatorInterface::Ptr _txValidator,
        bcos::protocol::TransactionSubmitResultFactory::Ptr _txResultFactory,
        bcos::protocol::BlockFactory::Ptr _blockFactory,
        std::shared_ptr<bcos::ledger::LedgerInterface> _ledger,
        NonceCheckerInterface::Ptr _txpoolNonceChecker)
      : m_txValidator(_txValidator),
        m_txResultFactory(_txResultFactory),
        m_blockFactory(_blockFactory),
        m_ledger(_ledger),
        m_txPoolNonceChecker(_txpoolNonceChecker)
    {}

    virtual ~TxPoolConfig() {}

    virtual void setNotifierWorkerNum(size_t _notifierWorkerNum)
    {
        m_notifierWorkerNum = _notifierWorkerNum;
    }
    virtual size_t notifierWorkerNum() const { return m_notifierWorkerNum; }

    virtual void setVerifyWorkerNum(size_t _verifyWorkerNum)
    {
        m_verifyWorkerNum = _verifyWorkerNum;
    }
    virtual size_t verifyWorkerNum() const { return m_verifyWorkerNum; }

    virtual void setPoolLimit(size_t _poolLimit) { m_poolLimit = _poolLimit; }
    virtual size_t poolLimit() const { return m_poolLimit; }

    NonceCheckerInterface::Ptr txPoolNonceChecker() { return m_txPoolNonceChecker; }

    TxValidatorInterface::Ptr txValidator() { return m_txValidator; }
    bcos::protocol::TransactionSubmitResultFactory::Ptr txResultFactory()
    {
        return m_txResultFactory;
    }

    bcos::protocol::BlockFactory::Ptr blockFactory() { return m_blockFactory; }
    void setBlockFactory(bcos::protocol::BlockFactory::Ptr _blockFactory)
    {
        m_blockFactory = _blockFactory;
    }

    bcos::protocol::TransactionFactory::Ptr txFactory()
    {
        return m_blockFactory->transactionFactory();
    }
    std::shared_ptr<bcos::ledger::LedgerInterface> ledger() { return m_ledger; }

    bcos::sealer::SealerInterface::Ptr sealer() { return m_sealer; }
    void setSealer(bcos::sealer::SealerInterface::Ptr _sealer) { m_sealer = _sealer; }

private:
    TxValidatorInterface::Ptr m_txValidator;
    bcos::protocol::TransactionSubmitResultFactory::Ptr m_txResultFactory;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    std::shared_ptr<bcos::ledger::LedgerInterface> m_ledger;
    bcos::sealer::SealerInterface::Ptr m_sealer;
    NonceCheckerInterface::Ptr m_txPoolNonceChecker;
    size_t m_poolLimit = 15000;
    size_t m_notifierWorkerNum = 1;
    size_t m_verifyWorkerNum = 1;
};
}  // namespace txpool
}  // namespace bcos