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
 * @brief unit test for txs-fetching related logic of txpool
 * @file TxsSyncTest.cpp.cpp
 * @author: yujiechen
 * @date 2021-05-26
 */
#include "test/unittests/txpool/TxPoolFixture.h"
#include <bcos-framework/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/testutils/TestPromptFixture.h>
#include <bcos-framework/testutils/crypto/HashImpl.h>
#include <bcos-framework/testutils/crypto/SignatureImpl.h>
#include <bcos-framework/testutils/protocol/FakeTransaction.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::txpool;
using namespace bcos::sync;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(txsSyncTest, TestPromptFixture)

void importTransactions(size_t _txsNum, CryptoSuite::Ptr _cryptoSuite, TxPoolFixture::Ptr _faker)
{
    auto txpool = _faker->txpool();
    auto ledger = _faker->ledger();
    Transactions transactions;
    for (size_t i = 0; i < _txsNum; i++)
    {
        auto tx = fakeTransaction(_cryptoSuite, utcTime() + 1000 + i, ledger->blockNumber() + 1,
            _faker->chainId(), _faker->groupId());
        transactions.push_back(tx);
        auto encodedData = tx->encode();
        auto txData = std::make_shared<bytes>(encodedData.begin(), encodedData.end());
        txpool->asyncSubmit(
            txData, [&](Error::Ptr, TransactionSubmitResult::Ptr) {}, nullptr);
    }
    while (txpool->txpoolStorage()->size() < _txsNum)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void testTransactionSync(bool _onlyTxsStatus = false)
{
    auto hashImpl = std::make_shared<Keccak256Hash>();
    auto signatureImpl = std::make_shared<Secp256k1SignatureImpl>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
    std::string groupId = "test-group";
    std::string chainId = "test-chain";
    int64_t blockLimit = 15;
    auto frontService = std::make_shared<FakeFrontService>(keyPair->publicKey());
    auto faker = std::make_shared<TxPoolFixture>(
        keyPair->publicKey(), cryptoSuite, groupId, chainId, blockLimit, frontService);
    if (_onlyTxsStatus)
    {
        faker->resetToFakeTransactionSync();
    }
    faker->appendSealer(keyPair->publicKey());
    // init the config
    faker->init();
    auto txpool = faker->txpool();
    auto ledger = faker->ledger();
    // append sessions
    size_t sessionSize = 8;
    std::vector<TxPoolFixture::Ptr> txpoolPeerList;
    for (size_t i = 0; i < sessionSize; i++)
    {
        auto nodeId = signatureImpl->generateKeyPair()->publicKey();
        auto sessionFaker = std::make_shared<TxPoolFixture>(
            nodeId, cryptoSuite, groupId, chainId, blockLimit, frontService);
        sessionFaker->init();
        if (_onlyTxsStatus)
        {
            sessionFaker->resetToFakeTransactionSync();
        }
        faker->appendSealer(nodeId);
        // make sure the session in the group
        sessionFaker->appendSealer(nodeId);
        txpoolPeerList.push_back(sessionFaker);
    }
    size_t txsNum = 10;
    importTransactions(txsNum, cryptoSuite, faker);

    // check maintain transactions
    faker->sync()->maintainTransactions();
    if (_onlyTxsStatus)
    {
        for (auto txpoolPeer : txpoolPeerList)
        {
            // all the peers has received the txsStatus, and fetch txs from other peers
            BOOST_CHECK(faker->frontService()->getAsyncSendSizeByNodeID(txpoolPeer->nodeID()) >= 1);
            while (txpoolPeer->txpool()->txpoolStorage()->size() < txsNum)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            BOOST_CHECK(txpoolPeer->txpool()->txpoolStorage()->size() == txsNum);
        }
        // maintain transactions again
        auto originSendSize = faker->frontService()->totalSendMsgSize();
        faker->sync()->maintainTransactions();
        BOOST_CHECK(faker->frontService()->totalSendMsgSize() == originSendSize);
        return;
    }
    // check the transactions has been broadcasted to all the node
    // maintainDownloadingTransactions and check the size
    for (auto txpoolPeer : txpoolPeerList)
    {
        BOOST_CHECK(faker->frontService()->getAsyncSendSizeByNodeID(txpoolPeer->nodeID()) >= 1);
        txpoolPeer->sync()->maintainDownloadingTransactions();
        while (txpoolPeer->txpool()->txpoolStorage()->size() < txsNum)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        BOOST_CHECK(txpoolPeer->txpool()->txpoolStorage()->size() == txsNum);
    }
    // +1 for include the node self
    auto forwardSize =
        ((txpoolPeerList.size() + 1) * faker->sync()->config()->forwardPercent() + 99) / 100;

    // with requestMissedTxs request
    auto maxSendSize = txpoolPeerList.size() + forwardSize + forwardSize;
    BOOST_CHECK(faker->frontService()->totalSendMsgSize() <= maxSendSize);
    auto originSendSize = faker->frontService()->totalSendMsgSize();
    faker->sync()->maintainTransactions();
    std::cout << "##### totalSendMsgSize: " << faker->frontService()->totalSendMsgSize()
              << std::endl;
    std::cout << "#### txpoolPeerList size:" << txpoolPeerList.size() << std::endl;
    std::cout << "##### forwardSize:" << forwardSize << std::endl;
    BOOST_CHECK(faker->frontService()->totalSendMsgSize() == originSendSize);

    // test forward txs status
    auto syncPeer = txpoolPeerList[0];
    // update connected node list
    for (auto txpoolPeer : txpoolPeerList)
    {
        syncPeer->appendSealer(txpoolPeer->nodeID());
    }
    syncPeer->sync()->maintainTransactions();
    auto expectedSize = originSendSize + forwardSize;
    BOOST_CHECK(faker->frontService()->totalSendMsgSize() == expectedSize);

    syncPeer->sync()->maintainTransactions();
    BOOST_CHECK(faker->frontService()->totalSendMsgSize() == expectedSize);

    // import new transaction to the syncPeer, but not broadcast the imported transaction
    std::cout << "###### test fetch and verify block" << std::endl;
    auto newTxsSize = 10;
    importTransactions(newTxsSize, cryptoSuite, syncPeer);
    // the syncPeer sealTxs
    HashListPtr txsHash;
    bool finish = false;
    syncPeer->txpool()->asyncSealTxs(
        100000, nullptr, [&](Error::Ptr _error, HashListPtr _fetchedTxs) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_fetchedTxs->size() == syncPeer->txpool()->txpoolStorage()->size());
            txsHash = _fetchedTxs;
            finish = true;
        });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // assume the faker verify the syncPeer generated proposal
    auto block = faker->txpool()->txpoolConfig()->blockFactory()->createBlock();
    for (auto const& txHash : *txsHash)
    {
        block->appendTransactionHash(txHash);
    }
    auto encodedData = std::make_shared<bytes>();
    block->encode(*encodedData);
    finish = false;
    faker->txpool()->asyncVerifyBlock(
        syncPeer->nodeID(), ref(*encodedData), [&](Error::Ptr _error, bool _result) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_result == true);
            finish = true;
        });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

BOOST_AUTO_TEST_CASE(testMatainTransactions)
{
    testTransactionSync(false);
}

BOOST_AUTO_TEST_CASE(testOnPeerTxsStatus)
{
    testTransactionSync(true);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos