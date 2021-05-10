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
#include "interfaces/NonceCheckerInterface.h"
#include "interfaces/TxPoolStorageInterface.h"
#include "interfaces/TxValidatorInterface.h"
#include <bcos-framework/interfaces/protocol/TransactionSubmitResultFactory.h>
namespace bcos
{
namespace txpool
{
class TxPoolConfig
{
public:
    using Ptr = std::shared_ptr<TxPoolConfig>;
    TxPoolConfig(TxValidatorInterface::Ptr _txValidator,
        bcos::protocol::TransactionSubmitResultFactory::Ptr _txResultFactory, size_t _poolLimit,
        size_t _notifierWorkerNum = 1)
      : m_txValidator(_txValidator),
        m_txResultFactory(_txResultFactory),
        m_poolLimit(_poolLimit),
        m_notifierWorkerNum(_notifierWorkerNum)
    {}

    virtual ~TxPoolConfig() {}

    virtual void setNotifierWorkerNum(size_t _notifierWorkerNum)
    {
        m_notifierWorkerNum = _notifierWorkerNum;
    }
    virtual void setPoolLimit(size_t _poolLimit) { m_poolLimit = _poolLimit; }

    virtual size_t notifierWorkerNum() const { return m_notifierWorkerNum; }
    virtual size_t poolLimit() const { return m_poolLimit; }

    NonceCheckerInterface::Ptr txPoolNonceChecker() { return m_txPoolNonceChecker; }
    NonceCheckerInterface::Ptr ledgerNonceChecker() { return m_ledgerNonceChecker; }

    TxValidatorInterface::Ptr txValidator() { return m_txValidator; }
    bcos::protocol::TransactionSubmitResultFactory::Ptr txResultFactory()
    {
        return m_txResultFactory;
    }

private:
    TxValidatorInterface::Ptr m_txValidator;
    bcos::protocol::TransactionSubmitResultFactory::Ptr m_txResultFactory;
    // TODO: create the nonceChecker
    NonceCheckerInterface::Ptr m_txPoolNonceChecker;
    NonceCheckerInterface::Ptr m_ledgerNonceChecker;
    size_t m_poolLimit;
    size_t m_notifierWorkerNum;
};
}  // namespace txpool
}  // namespace bcos