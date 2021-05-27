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
 * @brief unit test for the txpool
 * @file TxPoolTest.cpp
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
using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(TxPoolTest, TestPromptFixture)
void txPoolInitAndSubmitTransactionTest(bool _sm, CryptoSuite::Ptr _cryptoSuite)
{
    auto signatureImpl = _cryptoSuite->signatureImpl();
    auto hashImpl = _cryptoSuite->hashImpl();
    auto keyPair = signatureImpl->generateKeyPair();
    std::string groupId = "group_test_for_txpool";
    std::string chainId = "chain_test_for_txpool";
    int64_t blockLimit = 10;
    auto frontService = std::make_shared<FakeFrontService>(keyPair->publicKey());
    auto faker = std::make_shared<TxPoolFixture>(
        keyPair->publicKey(), _cryptoSuite, groupId, chainId, blockLimit, frontService);
    faker->init();

    // check the txpool config
    auto txpoolConfig = faker->txpool()->txpoolConfig();
    BOOST_CHECK(txpoolConfig->txPoolNonceChecker());
    BOOST_CHECK(txpoolConfig->txValidator());
    BOOST_CHECK(txpoolConfig->blockFactory());
    BOOST_CHECK(txpoolConfig->txFactory());
    BOOST_CHECK(txpoolConfig->ledger());
    BOOST_CHECK(txpoolConfig->sealer());

    auto txpool = faker->txpool();
    auto txpoolStorage = txpool->txpoolStorage();
    // case1: the node is not in the consensus/observerList
    auto tx = fakeTransaction(_cryptoSuite, utcTime());
    checkTxSubmit(txpool, txpoolStorage, tx, HashType(),
        (uint32_t)TransactionStatus::RequestNotBelongToTheGroup, 0);

    // case2: transaction with invalid blockLimit
    faker->appendSealer(faker->nodeID());
    auto ledger = faker->ledger();
    tx = fakeTransaction(_cryptoSuite, utcTime(), ledger->blockNumber() + blockLimit + 1);
    checkTxSubmit(
        txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::BlockLimitCheckFail, 0);

    // case3: transaction with invalid nonce(conflict with the ledger nonce)
    auto const& blockData = ledger->ledgerData();
    auto duplicatedNonce =
        blockData[ledger->blockNumber() - blockLimit + 1]->transaction(0)->nonce();
    tx = fakeTransaction(_cryptoSuite, duplicatedNonce, ledger->blockNumber() + blockLimit - 4);
    checkTxSubmit(
        txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::NonceCheckFail, 0);

    // case4: invalid groupId
    tx = fakeTransaction(_cryptoSuite, utcTime(), ledger->blockNumber() + blockLimit - 4,
        faker->chainId(), "invalidGroup");
    checkTxSubmit(
        txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::InvalidGroupId, 0);

    // case5: invalid chainId
    tx = fakeTransaction(_cryptoSuite, utcTime(), ledger->blockNumber() + blockLimit - 4,
        "invalidChainId", faker->groupId());
    checkTxSubmit(
        txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::InvalidChainId, 0);

    // case6: invalid signature
    tx = fakeTransaction(_cryptoSuite, utcTime() + 100000, ledger->blockNumber() + blockLimit - 4,
        faker->chainId(), faker->groupId());

    auto pbTx = std::dynamic_pointer_cast<PBTransaction>(tx);
    auto invalidKeyPair = signatureImpl->generateKeyPair();
    auto invalidHash = hashImpl->hash(std::string("test"));
    auto signatureData = signatureImpl->sign(invalidKeyPair, invalidHash, true);
    pbTx->updateSignature(ref(*signatureData), bytes());
    size_t importedTxNum = 0;
    if (!_sm)
    {
        importedTxNum++;
        checkTxSubmit(txpool, txpoolStorage, pbTx, pbTx->hash(), (uint32_t)TransactionStatus::None,
            importedTxNum, false);
    }
    else
    {
        checkTxSubmit(txpool, txpoolStorage, pbTx, pbTx->hash(),
            (uint32_t)TransactionStatus::InvalidSignature, importedTxNum);
    }

    // case7: submit success
    importedTxNum++;
    tx = fakeTransaction(_cryptoSuite, utcTime() + 2000000, ledger->blockNumber() + blockLimit - 4,
        faker->chainId(), faker->groupId());
    checkTxSubmit(txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::None,
        importedTxNum, false);
    // case8: submit duplicated tx
    checkTxSubmit(txpool, txpoolStorage, tx, tx->hash(),
        (uint32_t)TransactionStatus::AlreadyInTxPool, importedTxNum);

    // batch import transactions with multiple thread
    auto threadPool = std::make_shared<ThreadPool>("txpoolSubmitter", 8);

    Transactions transactions;
    for (auto i = 0; i < 40; i++)
    {
        auto tmpTx = fakeTransaction(_cryptoSuite, utcTime() + 1000 + i,
            ledger->blockNumber() + blockLimit - 4, faker->chainId(), faker->groupId());
        transactions.push_back(tmpTx);
    }

    tbb::parallel_for(
        tbb::blocked_range<int>(0, transactions.size()), [&](const tbb::blocked_range<int>& _r) {
            for (auto i = _r.begin(); i < _r.end(); i++)
            {
                auto tmpTx = transactions[i];
                checkTxSubmit(txpool, txpoolStorage, tmpTx, tmpTx->hash(),
                    (uint32_t)TransactionStatus::None, 0, false, true);
            }
        });
    importedTxNum += transactions.size();
    while (txpoolStorage->size() < importedTxNum)
    {
        std::cout << "#### txpoolStorage->size:" << txpoolStorage->size() << std::endl;
        std::cout << "#### importedTxNum:" << importedTxNum << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cout << "#### txpoolStorage size:" << txpoolStorage->size() << std::endl;
    std::cout << "#### importedTxNum:" << importedTxNum << std::endl;
    // check txs submitted to the ledger
    auto const& txsHash2Data = ledger->txsHashToData();
    for (size_t i = 0; i < transactions.size(); i++)
    {
        BOOST_CHECK(txsHash2Data.count(transactions[i]->hash()));
    }
    // case9: the txpool is full
    txpoolConfig->setPoolLimit(importedTxNum);
    checkTxSubmit(txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::TxPoolIsFull,
        importedTxNum);

    // case10: malformed transaction
    auto encodedData = tx->encode();
    auto txData = std::make_shared<bytes>(encodedData.begin(), encodedData.end());
    // fake invalid txData
    for (size_t i = 0; i < txData->size(); i++)
    {
        (*txData)[i] += 100;
    }
    bool verifyFinish = false;
    txpool->asyncSubmit(
        txData,
        [&](Error::Ptr _error, TransactionSubmitResult::Ptr _result) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_result->txHash() == HashType());
            BOOST_CHECK(_result->status() == (uint32_t)(TransactionStatus::Malform));
            verifyFinish = true;
        },
        [&](Error::Ptr _error) { BOOST_CHECK(_error == nullptr); });
    while (!verifyFinish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

BOOST_AUTO_TEST_CASE(testTxPoolInitAndSubmitTransaction)
{
    auto hashImpl = std::make_shared<Keccak256Hash>();
    auto signatureImpl = std::make_shared<Secp256k1SignatureImpl>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    txPoolInitAndSubmitTransactionTest(false, cryptoSuite);
}

BOOST_AUTO_TEST_CASE(testSMTxPoolInitAndSubmitTransaction)
{
    auto hashImpl = std::make_shared<Sm3Hash>();
    auto signatureImpl = std::make_shared<SM2SignatureImpl>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    txPoolInitAndSubmitTransactionTest(true, cryptoSuite);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos