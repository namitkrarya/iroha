/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_BLOCK_LOADER_SERVICE_HPP
#define IROHA_BLOCK_LOADER_SERVICE_HPP

#include "ametsuchi/block_query_factory.hpp"
#include "consensus/consensus_block_cache.hpp"
#include "loader.grpc.pb.h"
#include "logger/logger.hpp"

namespace iroha {
  namespace network {
    class BlockLoaderService : public proto::Loader::Service {
     public:
      BlockLoaderService(
          std::shared_ptr<ametsuchi::BlockQueryFactory> block_query_factory,
          std::shared_ptr<iroha::consensus::ConsensusResultCache>
              consensus_result_cache,
          logger::Logger log = logger::log("BlockLoaderService"));

      grpc::Status retrieveBlocks(
          ::grpc::ServerContext *context,
          const proto::BlocksRequest *request,
          ::grpc::ServerWriter<protocol::Block> *writer) override;

      grpc::Status retrieveBlock(::grpc::ServerContext *context,
                                 const proto::BlockRequest *request,
                                 protocol::Block *response) override;

     private:
      std::shared_ptr<ametsuchi::BlockQueryFactory> block_query_factory_;
      std::shared_ptr<iroha::consensus::ConsensusResultCache>
          consensus_result_cache_;
      logger::Logger log_;
    };
  }  // namespace network
}  // namespace iroha

#endif  // IROHA_BLOCK_LOADER_SERVICE_HPP
