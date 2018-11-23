/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "simulator/impl/simulator.hpp"

#include <vector>

#include "backend/protobuf/proto_block_factory.hpp"
#include "backend/protobuf/transaction.hpp"
#include "builders/protobuf/transaction.hpp"
#include "framework/specified_visitor.hpp"
#include "framework/test_subscriber.hpp"
#include "module/irohad/ametsuchi/ametsuchi_mocks.hpp"
#include "module/irohad/network/network_mocks.hpp"
#include "module/irohad/validation/validation_mocks.hpp"
#include "module/shared_model/builders/protobuf/proposal.hpp"
#include "module/shared_model/builders/protobuf/test_block_builder.hpp"
#include "module/shared_model/builders/protobuf/test_proposal_builder.hpp"
#include "module/shared_model/cryptography/crypto_model_signer_mock.hpp"
#include "module/shared_model/validators/validators.hpp"

using namespace iroha;
using namespace iroha::validation;
using namespace iroha::ametsuchi;
using namespace iroha::simulator;
using namespace iroha::network;
using namespace framework::test_subscriber;

using ::testing::_;
using ::testing::A;
using ::testing::Return;
using ::testing::ReturnArg;

using wBlock = std::shared_ptr<shared_model::interface::Block>;

class SimulatorTest : public ::testing::Test {
 public:
  void SetUp() override {
    shared_model::crypto::crypto_signer_expecter =
        std::make_shared<shared_model::crypto::CryptoModelSignerExpecter>();

    validator = std::make_shared<MockStatefulValidator>();
    factory = std::make_shared<MockTemporaryFactory>();
    query = std::make_shared<MockBlockQuery>();
    ordering_gate = std::make_shared<MockOrderingGate>();
    crypto_signer = std::make_shared<shared_model::crypto::CryptoModelSigner<>>(
        shared_model::crypto::DefaultCryptoAlgorithmType::generateKeypair());
    block_query_factory = std::make_shared<MockBlockQueryFactory>();
    EXPECT_CALL(*block_query_factory, createBlockQuery())
        .WillRepeatedly(testing::Return(boost::make_optional(
            std::shared_ptr<iroha::ametsuchi::BlockQuery>(query))));
    block_factory = std::make_unique<shared_model::proto::ProtoBlockFactory>(
        std::make_unique<shared_model::validation::MockValidator<
            shared_model::interface::Block>>());
  }

  void TearDown() override {
    shared_model::crypto::crypto_signer_expecter.reset();
  }

  void init() {
    simulator = std::make_shared<Simulator>(ordering_gate,
                                            validator,
                                            factory,
                                            block_query_factory,
                                            crypto_signer,
                                            std::move(block_factory));
  }

  std::shared_ptr<MockStatefulValidator> validator;
  std::shared_ptr<MockTemporaryFactory> factory;
  std::shared_ptr<MockBlockQuery> query;
  std::shared_ptr<MockBlockQueryFactory> block_query_factory;
  std::shared_ptr<MockOrderingGate> ordering_gate;
  std::shared_ptr<shared_model::crypto::CryptoModelSigner<>> crypto_signer;
  std::unique_ptr<shared_model::interface::UnsafeBlockFactory> block_factory;

  std::shared_ptr<Simulator> simulator;
};

shared_model::proto::Block makeBlock(int height) {
  return TestBlockBuilder()
      .transactions(std::vector<shared_model::proto::Transaction>())
      .height(height)
      .prevHash(shared_model::crypto::Hash(std::string(32, '0')))
      .build();
}

auto makeProposal(int height) {
  auto tx = shared_model::proto::TransactionBuilder()
                .createdTime(iroha::time::now())
                .creatorAccountId("admin@ru")
                .addAssetQuantity("coin#coin", "1.0")
                .quorum(1)
                .build()
                .signAndAddSignature(
                    shared_model::crypto::DefaultCryptoAlgorithmType::
                        generateKeypair())
                .finish();
  std::vector<shared_model::proto::Transaction> txs = {tx, tx};
  auto proposal = shared_model::proto::ProposalBuilder()
                      .height(height)
                      .createdTime(iroha::time::now())
                      .transactions(txs)
                      .build();
  return std::make_shared<shared_model::proto::Proposal>(std::move(proposal));
}

TEST_F(SimulatorTest, ValidWhenInitialized) {
  // simulator constructor => onProposal subscription called
  EXPECT_CALL(*ordering_gate, onProposal())
      .WillOnce(Return(rxcpp::observable<>::empty<OrderingEvent>()));

  init();
}

TEST_F(SimulatorTest, ValidWhenPreviousBlock) {
  // proposal with height 2 => height 1 block present => new block generated
  auto tx = shared_model::proto::TransactionBuilder()
                .createdTime(iroha::time::now())
                .creatorAccountId("admin@ru")
                .addAssetQuantity("coin#coin", "1.0")
                .quorum(1)
                .build()
                .signAndAddSignature(
                    shared_model::crypto::DefaultCryptoAlgorithmType::
                        generateKeypair())
                .finish();
  std::vector<shared_model::proto::Transaction> txs = {tx, tx};
  auto proposal = std::make_shared<shared_model::proto::Proposal>(
      shared_model::proto::ProposalBuilder()
          .height(2)
          .createdTime(iroha::time::now())
          .transactions(txs)
          .build());
  shared_model::proto::Block block = makeBlock(proposal->height() - 1);

  EXPECT_CALL(*factory, createTemporaryWsv()).Times(1);
  EXPECT_CALL(*query, getTopBlock())
      .WillOnce(Return(expected::makeValue(wBlock(clone(block)))));

  EXPECT_CALL(*query, getTopBlockHeight()).WillOnce(Return(block.height()));

  EXPECT_CALL(*validator, validate(_, _))
      .WillOnce(Return(
          std::make_pair(proposal, iroha::validation::TransactionsErrors{})));

  EXPECT_CALL(*ordering_gate, onProposal())
      .WillOnce(Return(rxcpp::observable<>::empty<OrderingEvent>()));

  EXPECT_CALL(*shared_model::crypto::crypto_signer_expecter,
              sign(A<shared_model::interface::Block &>()))
      .Times(1);

  init();

  auto proposal_wrapper =
      make_test_subscriber<CallExact>(simulator->onVerifiedProposal(), 1);
  proposal_wrapper.subscribe([&proposal](auto verified_proposal) {
    ASSERT_EQ(verified_proposal->first->height(), proposal->height());
    ASSERT_EQ(verified_proposal->first->transactions(),
              proposal->transactions());
    ASSERT_TRUE(verified_proposal->second.empty());
  });

  auto block_wrapper =
      make_test_subscriber<CallExact>(simulator->on_block(), 1);
  block_wrapper.subscribe([&proposal](const auto block) {
    ASSERT_EQ(block->height(), proposal->height());
    ASSERT_EQ(block->transactions(), proposal->transactions());
  });

  simulator->processProposal(*proposal);

  ASSERT_TRUE(proposal_wrapper.validate());
  ASSERT_TRUE(block_wrapper.validate());
}

TEST_F(SimulatorTest, FailWhenNoBlock) {
  // height 2 proposal => height 1 block not present => no validated proposal
  auto proposal = makeProposal(2);

  EXPECT_CALL(*factory, createTemporaryWsv()).Times(0);
  EXPECT_CALL(*query, getTopBlock())
      .WillOnce(Return(expected::makeError("no block")));

  EXPECT_CALL(*validator, validate(_, _)).Times(0);

  EXPECT_CALL(*ordering_gate, onProposal())
      .WillOnce(Return(rxcpp::observable<>::empty<OrderingEvent>()));

  EXPECT_CALL(*shared_model::crypto::crypto_signer_expecter,
              sign(A<shared_model::interface::Block &>()))
      .Times(0);

  init();

  auto proposal_wrapper =
      make_test_subscriber<CallExact>(simulator->onVerifiedProposal(), 0);
  proposal_wrapper.subscribe();

  auto block_wrapper =
      make_test_subscriber<CallExact>(simulator->on_block(), 0);
  block_wrapper.subscribe();

  simulator->processProposal(*proposal);

  ASSERT_TRUE(proposal_wrapper.validate());
  ASSERT_TRUE(block_wrapper.validate());
}

TEST_F(SimulatorTest, FailWhenSameAsProposalHeight) {
  // proposal with height 2 => height 2 block present => no validated proposal
  auto proposal = makeProposal(2);

  auto block = makeBlock(proposal->height());

  EXPECT_CALL(*factory, createTemporaryWsv()).Times(0);

  EXPECT_CALL(*query, getTopBlock())
      .WillOnce(Return(expected::makeValue(wBlock(clone(block)))));

  EXPECT_CALL(*validator, validate(_, _)).Times(0);

  EXPECT_CALL(*ordering_gate, onProposal())
      .WillOnce(Return(rxcpp::observable<>::empty<OrderingEvent>()));

  EXPECT_CALL(*shared_model::crypto::crypto_signer_expecter,
              sign(A<shared_model::interface::Block &>()))
      .Times(0);

  init();

  auto proposal_wrapper =
      make_test_subscriber<CallExact>(simulator->onVerifiedProposal(), 0);
  proposal_wrapper.subscribe();

  auto block_wrapper =
      make_test_subscriber<CallExact>(simulator->on_block(), 0);
  block_wrapper.subscribe();

  simulator->processProposal(*proposal);

  ASSERT_TRUE(proposal_wrapper.validate());
  ASSERT_TRUE(block_wrapper.validate());
}

/**
 * Checks, that after failing a certain number of transactions in a proposal,
 * returned verified proposal will have only valid transactions
 *
 * @given proposal consisting of several transactions
 * @when failing some of the transactions in that proposal
 * @then verified proposal consists of txs we did not fail
 */
TEST_F(SimulatorTest, RightNumberOfFailedTxs) {
  // create a 3-height proposal, but validator returns only a 2-height verified
  // proposal
  auto tx = shared_model::proto::TransactionBuilder()
                .createdTime(iroha::time::now())
                .creatorAccountId("admin@ru")
                .addAssetQuantity("coin#coin", "1.0")
                .quorum(1)
                .build()
                .signAndAddSignature(
                    shared_model::crypto::DefaultCryptoAlgorithmType::
                        generateKeypair())
                .finish();

  std::vector<shared_model::proto::Transaction> txs = {tx, tx, tx};
  auto proposal = std::make_shared<shared_model::proto::Proposal>(
      shared_model::proto::ProposalBuilder()
          .height(3)
          .createdTime(iroha::time::now())
          .transactions(txs)
          .build());
  auto verified_proposal = std::make_shared<shared_model::proto::Proposal>(
      shared_model::proto::ProposalBuilder()
          .height(2)
          .createdTime(iroha::time::now())
          .transactions(std::vector<shared_model::proto::Transaction>{tx})
          .build());
  auto tx_errors = iroha::validation::TransactionsErrors{
      std::make_pair(validation::CommandError{"SomeCommand", "SomeError", true},
                     shared_model::crypto::Hash(std::string(32, '0'))),
      std::make_pair(validation::CommandError{"SomeCommand", "SomeError", true},
                     shared_model::crypto::Hash(std::string(32, '0')))};
  shared_model::proto::Block block = makeBlock(proposal->height() - 1);

  EXPECT_CALL(*factory, createTemporaryWsv()).Times(1);
  EXPECT_CALL(*query, getTopBlock())
      .WillOnce(Return(expected::makeValue(wBlock(clone(block)))));

  EXPECT_CALL(*query, getTopBlockHeight()).WillOnce(Return(2));

  EXPECT_CALL(*validator, validate(_, _))
      .WillOnce(Return(std::make_pair(verified_proposal, tx_errors)));

  EXPECT_CALL(*ordering_gate, onProposal())
      .WillOnce(Return(rxcpp::observable<>::empty<OrderingEvent>()));

  EXPECT_CALL(*shared_model::crypto::crypto_signer_expecter,
              sign(A<shared_model::interface::Block &>()))
      .Times(1);

  init();

  auto proposal_wrapper =
      make_test_subscriber<CallExact>(simulator->onVerifiedProposal(), 1);
  proposal_wrapper.subscribe([&verified_proposal,
                              &tx_errors](auto verified_proposal_) {
    // assure that txs in verified proposal do not include failed ones
    ASSERT_EQ(verified_proposal_->first->height(), verified_proposal->height());
    ASSERT_EQ(verified_proposal_->first->transactions(),
              verified_proposal->transactions());
    ASSERT_TRUE(verified_proposal_->second.size() == tx_errors.size());
  });

  simulator->processProposal(*proposal);

  ASSERT_TRUE(proposal_wrapper.validate());
}
