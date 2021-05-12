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
 * @brief factory for create the txs-sync-message
 * @file TxsSyncMsgFactory.h
 * @author: yujiechen
 * @date 2021-05-11
 */
#pragma once
#include "sync/interfaces/TxsSyncMsgInterface.h"

namespace bcos
{
namespace sync
{
class TxsSyncMsgFactory
{
public:
    using Ptr = std::shared_ptr<TxsSyncMsgFactory>;
    TxsSyncMsgFactory() = default;
    virtual ~TxsSyncMsgFactory() {}

    virtual TxsSyncMsgInterface::Ptr createTxsSyncMsg() = 0;
    virtual TxsSyncMsgInterface::Ptr createTxsSyncMsg(bytesConstRef _data) = 0;
}
}  // namespace sync
}  // namespace bcos
