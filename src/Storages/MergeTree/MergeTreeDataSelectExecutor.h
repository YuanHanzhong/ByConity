/*
 * Copyright 2016-2023 ClickHouse, Inc.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


/*
 * This file may have been modified by Bytedance Ltd. and/or its affiliates (“ Bytedance's Modifications”).
 * All Bytedance's Modifications are Copyright (2023) Bytedance Ltd. and/or its affiliates.
 */

#pragma once

#include <Core/QueryProcessingStage.h>
#include <Storages/SelectQueryInfo.h>
#include <MergeTreeCommon/MergeTreeMetaBase.h>
#include <Storages/MergeTree/RangesInDataPart.h>
#include <Storages/MergeTree/PartitionPruner.h>
#include <QueryPlan/ReadFromMergeTree.h>

namespace DB
{

class KeyCondition;

using PartitionIdToMaxBlock = std::unordered_map<String, Int64>;

/** Executes SELECT queries on data from the merge tree.
  */
class MergeTreeDataSelectExecutor
{
public:
    explicit MergeTreeDataSelectExecutor(const MergeTreeMetaBase & data_);

    /** When reading, selects a set of parts that covers the desired range of the index.
      * max_blocks_number_to_read - if not nullptr, do not read all the parts whose right border is greater than max_block in partition.
      */

    QueryPlanPtr read(
        const Names & column_names,
        const StorageMetadataPtr & metadata_snapshot,
        const SelectQueryInfo & query_info,
        ContextPtr context,
        UInt64 max_block_size,
        unsigned num_streams,
        QueryProcessingStage::Enum processed_stage,
        std::shared_ptr<PartitionIdToMaxBlock> max_block_numbers_to_read = nullptr) const;

    /// The same as read, but with specified set of parts.
    QueryPlanPtr readFromParts(
        MergeTreeMetaBase::DataPartsVector parts,
        MergeTreeMetaBase::DeleteBitmapGetter delete_bitmap_getter,
        const Names & column_names,
        const StorageMetadataPtr & metadata_snapshot_base,
        const StorageMetadataPtr & metadata_snapshot,
        const SelectQueryInfo & query_info,
        ContextPtr context,
        UInt64 max_block_size,
        unsigned num_streams,
        std::shared_ptr<PartitionIdToMaxBlock> max_block_numbers_to_read = nullptr,
        MergeTreeDataSelectAnalysisResultPtr merge_tree_select_result_ptr = nullptr) const;

    /// Get an estimation for the number of marks we are going to read.
    /// Reads nothing. Secondary indexes are not used.
    /// This method is used to select best projection for table.
    MergeTreeDataSelectAnalysisResultPtr estimateNumMarksToRead(
        MergeTreeMetaBase::DataPartsVector parts,
        const Names & column_names,
        const StorageMetadataPtr & metadata_snapshot_base,
        const StorageMetadataPtr & metadata_snapshot,
        const SelectQueryInfo & query_info,
        ContextPtr context,
        unsigned num_streams,
        std::shared_ptr<PartitionIdToMaxBlock> max_block_numbers_to_read = nullptr) const;

private:
    const MergeTreeMetaBase & data;
    Poco::Logger * log;

    /// Get the approximate value (bottom estimate - only by full marks) of the number of rows falling under the index.
    static size_t getApproximateTotalRowsToRead(
        const MergeTreeMetaBase::DataPartsVector & parts,
        const StorageMetadataPtr & metadata_snapshot,
        const KeyCondition & key_condition,
        const Settings & settings,
        Poco::Logger * log);

    static MarkRanges markRangesFromPKRange(
        const MergeTreeMetaBase::DataPartPtr & part,
        const StorageMetadataPtr & metadata_snapshot,
        const KeyCondition & key_condition,
        const Settings & settings,
        Poco::Logger * log);

    static MarkRanges filterMarksUsingIndex(
        MergeTreeIndexPtr index_helper,
        MergeTreeIndexConditionPtr condition,
        MergeTreeMetaBase::DataPartPtr part,
        const MarkRanges & ranges,
        ContextPtr context,
        const MergeTreeReaderSettings & reader_settings,
        size_t & total_granules,
        size_t & granules_dropped,
        Poco::Logger * log);

    struct PartFilterCounters
    {
        size_t num_initial_selected_parts = 0;
        size_t num_initial_selected_granules = 0;
        size_t num_parts_after_minmax = 0;
        size_t num_granules_after_minmax = 0;
        size_t num_parts_after_partition_pruner = 0;
        size_t num_granules_after_partition_pruner = 0;
    };

    /// Select the parts in which there can be data that satisfy `minmax_idx_condition` and that match the condition on `_part`,
    ///  as well as `max_block_number_to_read`.
    static void selectPartsToRead(
        MergeTreeMetaBase::DataPartsVector & parts,
        const std::optional<std::unordered_set<String>> & part_values,
        const std::optional<KeyCondition> & minmax_idx_condition,
        const DataTypes & minmax_columns_types,
        std::optional<PartitionPruner> & partition_pruner,
        const PartitionIdToMaxBlock * max_block_numbers_to_read,
        PartFilterCounters & counters);

    /// Same as previous but also skip parts uuids if any to the query context, or skip parts which uuids marked as excluded.
    static void selectPartsToReadWithUUIDFilter(
        MergeTreeMetaBase::DataPartsVector & parts,
        const std::optional<std::unordered_set<String>> & part_values,
        MergeTreeMetaBase::PinnedPartUUIDsPtr pinned_part_uuids,
        const std::optional<KeyCondition> & minmax_idx_condition,
        const DataTypes & minmax_columns_types,
        std::optional<PartitionPruner> & partition_pruner,
        const PartitionIdToMaxBlock * max_block_numbers_to_read,
        ContextPtr query_context,
        PartFilterCounters & counters,
        Poco::Logger * log);

public:
    /// For given number rows and bytes, get the number of marks to read.
    /// It is a minimal number of marks which contain so many rows and bytes.
    static size_t roundRowsOrBytesToMarks(
        size_t rows_setting,
        size_t bytes_setting,
        size_t rows_granularity,
        size_t bytes_granularity);

    /// The same as roundRowsOrBytesToMarks, but return no more than max_marks.
    static size_t minMarksForConcurrentRead(
        size_t rows_setting,
        size_t bytes_setting,
        size_t rows_granularity,
        size_t bytes_granularity,
        size_t max_marks);

    /// If possible, filter using expression on virtual columns.
    /// Example: SELECT count() FROM table WHERE _part = 'part_name'
    /// If expression found, return a set with allowed part names (std::nullopt otherwise).
    static std::optional<std::unordered_set<String>> filterPartsByVirtualColumns(
        const MergeTreeMetaBase & data,
        const MergeTreeMetaBase::DataPartsVector & parts,
        const ASTPtr & query,
        ContextPtr context);

    /// Filter parts using minmax index and partition key.
    static void filterPartsByPartition(
        MergeTreeMetaBase::DataPartsVector & parts,
        const std::optional<std::unordered_set<String>> & part_values,
        const StorageMetadataPtr & metadata_snapshot,
        const MergeTreeMetaBase & data,
        const SelectQueryInfo & query_info,
        const ContextPtr & context,
        const PartitionIdToMaxBlock * max_block_numbers_to_read,
        Poco::Logger * log,
        ReadFromMergeTree::IndexStats & index_stats);

    static DataTypes get_set_element_types(const NamesAndTypesList & source_columns, const String & column_name);

    static bool extractBitmapIndexImpl(
        const SelectQueryInfo&    queryInfo,
        const ASTPtr&             expr,
        const MergeTreeMetaBase&      data,
        std::map<String, SetPtr>& bitmap_index_map);

    static bool extractBitmapIndex(
        const SelectQueryInfo &    queryInfo,
        const MergeTreeMetaBase &      data,
        std::map<String, SetPtr> & bitmap_index_map);

    /// Filter parts using primary key and secondary indexes.
    /// For every part, select mark ranges to read.
    static RangesInDataParts filterPartsByPrimaryKeyAndSkipIndexes(
        MergeTreeMetaBase::DataPartsVector && parts,
        StorageMetadataPtr metadata_snapshot,
        const SelectQueryInfo & query_info,
        const ContextPtr & context,
        const KeyCondition & key_condition,
        const MergeTreeReaderSettings & reader_settings,
        Poco::Logger * log,
        size_t num_streams,
        ReadFromMergeTree::IndexStats & index_stats,
        bool use_skip_indexes,
        const MergeTreeMetaBase & data_,
        bool use_sampling,
        RelativeSize relative_sample_size);

    /// Create expression for sampling.
    /// Also, calculate _sample_factor if needed.
    /// Also, update key condition with selected sampling range.
    static MergeTreeDataSelectSamplingData getSampling(
        const ASTSelectQuery & select,
        NamesAndTypesList available_real_columns,
        const MergeTreeMetaBase::DataPartsVector & parts,
        KeyCondition & key_condition,
        const MergeTreeMetaBase & data,
        const StorageMetadataPtr & metadata_snapshot,
        ContextPtr context,
        bool sample_factor_column_queried,
        Poco::Logger * log);

    static MarkRanges sampleByRange(
        const MergeTreeMetaBase::DataPartPtr & part,
        const MarkRanges & ranges,
        const RelativeSize & relative_sample_size,
        bool deterministic,
        bool uniform);

    static MarkRanges sliceRange(const MarkRange & range, const UInt64 & sample_size);

    /// Check query limits: max_partitions_to_read, max_concurrent_queries.
    /// Also, return QueryIdHolder. If not null, we should keep it until query finishes.
    static std::shared_ptr<QueryIdHolder> checkLimits(
        const MergeTreeMetaBase & data,
        const RangesInDataParts & parts_with_ranges,
        const ContextPtr & context);
};

}
