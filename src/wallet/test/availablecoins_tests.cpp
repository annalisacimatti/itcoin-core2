// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/spend.h>
#include <wallet/test/util.h>
#include <wallet/test/wallet_test_fixture.h>

#include <boost/test/unit_test.hpp>

namespace wallet {
BOOST_FIXTURE_TEST_SUITE(availablecoins_tests, WalletTestingSetup)
class AvailableCoinsTestingSetup : public TestChain100Setup
{
public:
    AvailableCoinsTestingSetup()
    {
        CreateAndProcessBlock({}, {});
        wallet = CreateSyncedWallet(*m_node.chain, m_node.chainman->ActiveChain(), m_args, coinbaseKey);
    }

    ~AvailableCoinsTestingSetup()
    {
        wallet.reset();
    }
    CWalletTx& AddTx(CRecipient recipient)
    {
        CTransactionRef tx;
        CCoinControl dummy;
        {
            constexpr int RANDOM_CHANGE_POSITION = -1;
            auto res = CreateTransaction(*wallet, {recipient}, RANDOM_CHANGE_POSITION, dummy);
            BOOST_CHECK(res);
            tx = res->tx;
        }
        wallet->CommitTransaction(tx, {}, {});
        CMutableTransaction blocktx;
        {
            LOCK(wallet->cs_wallet);
            blocktx = CMutableTransaction(*wallet->mapWallet.at(tx->GetHash()).tx);
        }
        CreateAndProcessBlock({CMutableTransaction(blocktx)}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));

        LOCK(wallet->cs_wallet);
        LOCK(m_node.chainman->GetMutex());
        wallet->SetLastBlockProcessed(wallet->GetLastBlockHeight() + 1, m_node.chainman->ActiveChain().Tip()->GetBlockHash());
        auto it = wallet->mapWallet.find(tx->GetHash());
        BOOST_CHECK(it != wallet->mapWallet.end());
        it->second.m_state = TxStateConfirmed{m_node.chainman->ActiveChain().Tip()->GetBlockHash(), m_node.chainman->ActiveChain().Height(), /*index=*/1};
        return it->second;
    }

    std::unique_ptr<CWallet> wallet;
};

BOOST_FIXTURE_TEST_CASE(BasicOutputTypesTest, AvailableCoinsTestingSetup)
{
    CoinsResult available_coins;
    util::Result<CTxDestination> dest{util::Error{}};
    LOCK(wallet->cs_wallet);

    // Verify our wallet has 100 (ITCOIN_SPECIFIC: it was one) usable coinbase UTXO before starting
    // These UTXOs are P2PK, so they should show up in the Other bucket (ITCOIN_SPECIFIC: it was "This UTXO is a P2PK, so it"...)
    available_coins = AvailableCoins(*wallet);
    BOOST_CHECK_EQUAL(available_coins.Size(), 100U); // ITCOIN_SPECIFIC: it was 1.
    BOOST_CHECK_EQUAL(available_coins.coins[OutputType::UNKNOWN].size(), 100U); // ITCOIN_SPECIFIC: it was 1.

    // We will create a self transfer for each of the OutputTypes and
    // verify it is put in the correct bucket after running GetAvailablecoins
    //
    // For each OutputType, We expect 2 UTXOs in our wallet following the self transfer:
    //   1. One UTXO as the recipient
    //   2. One UTXO from the change, due to payment address matching logic

    // Bech32m
    dest = wallet->GetNewDestination(OutputType::BECH32M, "");
    BOOST_ASSERT(dest);
    AddTx(CRecipient{{GetScriptForDestination(*dest)}, 1 * COIN, /*fSubtractFeeFromAmount=*/true});
    available_coins = AvailableCoins(*wallet);
    BOOST_CHECK_EQUAL(available_coins.coins[OutputType::BECH32M].size(), 2U);

    // Bech32
    dest = wallet->GetNewDestination(OutputType::BECH32, "");
    BOOST_ASSERT(dest);
    AddTx(CRecipient{{GetScriptForDestination(*dest)}, 2 * COIN, /*fSubtractFeeFromAmount=*/true});
    available_coins = AvailableCoins(*wallet);
    BOOST_CHECK_EQUAL(available_coins.coins[OutputType::BECH32].size(), 2U);

    // P2SH-SEGWIT
    dest = wallet->GetNewDestination(OutputType::P2SH_SEGWIT, "");
    BOOST_ASSERT(dest);
    AddTx(CRecipient{{GetScriptForDestination(*dest)}, 3 * COIN, /*fSubtractFeeFromAmount=*/true});
    available_coins = AvailableCoins(*wallet);
    BOOST_CHECK_EQUAL(available_coins.coins[OutputType::P2SH_SEGWIT].size(), 2U);

    // Legacy (P2PKH)
    dest = wallet->GetNewDestination(OutputType::LEGACY, "");
    BOOST_ASSERT(dest);
    AddTx(CRecipient{{GetScriptForDestination(*dest)}, 4 * COIN, /*fSubtractFeeFromAmount=*/true});
    available_coins = AvailableCoins(*wallet);
    BOOST_CHECK_EQUAL(available_coins.coins[OutputType::LEGACY].size(), 2U);
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
