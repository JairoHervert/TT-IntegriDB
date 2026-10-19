#pragma once

#include <array>
#include <string>
#include <vector>

#include "tt/aso/intersection.hpp"
#include "tt/query/selection.hpp"

namespace tt::query {
struct MultiRangeResponse {
    crypto::Digest state_id{};
    MultiRangeQuery query;
    std::array<ait::RangeProof, 2> ranges;
    std::array<std::vector<aso::SplitWitness>, 2> splits; // un testigo por nodo de cobertura
    std::vector<aso::DisjointWitness> disjoint;            // orden i*|B|+j
    std::vector<RowProof> rows;                            // solo la interseccion
};
struct MultiRangeAnswer {
    MultiRangeResponse response;
    std::array<scheme::TreeSpec, 2> trees;
    std::array<std::uint64_t, 2> input_counts{};
    std::size_t stale_rows = 0;
};
struct MultiRangeVerification {
    bool accepted = false;
    std::string reason;
    std::vector<db::Row> rows;
    std::array<std::size_t, 2> cover_nodes{};
    double range_seconds = 0;
    double rows_seconds = 0;
    double aso_seconds = 0;
};

MultiRangeAnswer answer_multirange(const scheme::AuthenticatedDatabase& adb,
                                  const scheme::DatabaseDigest& digest,
                                  const aso::PublicKey& pk, const MultiRangeQuery& query,
                                  db::RowStore& store);
MultiRangeVerification verify_multirange(const scheme::DatabaseDigest& digest,
                                         const aso::PublicKey& pk,
                                         const MultiRangeQuery& asked,
                                         const MultiRangeResponse& response);
}  // namespace tt::query
