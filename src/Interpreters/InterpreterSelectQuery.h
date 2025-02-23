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

#include <memory>

#include <Core/QueryProcessingStage.h>
#include <DataStreams/IBlockStream_fwd.h>
#include <Interpreters/ExpressionActions.h>
#include <Interpreters/ExpressionAnalyzer.h>
#include <Interpreters/IInterpreterUnionOrSelectQuery.h>
#include <Interpreters/StorageID.h>
#include <Parsers/ASTSelectQuery.h>
#include <Storages/ReadInOrderOptimizer.h>
#include <Storages/SelectQueryInfo.h>
#include <Storages/TableLockHolder.h>
#include <Parsers/ASTOrderByElement.h>

#include <Columns/FilterDescription.h>

namespace Poco
{
class Logger;
}

namespace DB
{
struct SubqueryForSet;
class InterpreterSelectWithUnionQuery;
class Context;
class QueryPlan;

struct GroupingSetsParams;
using GroupingSetsParamsList = std::vector<GroupingSetsParams>;

struct TreeRewriterResult;
using TreeRewriterResultPtr = std::shared_ptr<const TreeRewriterResult>;

StreamLocalLimits getLimitsForStorage(const Settings & settings, const SelectQueryOptions & options);

/** Interprets the SELECT query. Returns the stream of blocks with the results of the query before `to_stage` stage.
  */
class InterpreterSelectQuery : public IInterpreterUnionOrSelectQuery
{
public:
    /**
     * query_ptr
     * - A query AST to interpret.
     *
     * required_result_column_names
     * - don't calculate all columns except the specified ones from the query
     *  - it is used to remove calculation (and reading) of unnecessary columns from subqueries.
     *   empty means - use all columns.
     */

    InterpreterSelectQuery(
        const ASTPtr & query_ptr_,
        ContextPtr context_,
        const SelectQueryOptions &,
        const Names & required_result_column_names_ = Names{});

    /// Read data not from the table specified in the query, but from the prepared source `input`.
    InterpreterSelectQuery(
        const ASTPtr & query_ptr_,
        ContextPtr context_,
        const BlockInputStreamPtr & input_,
        const SelectQueryOptions & = {});

    /// Read data not from the table specified in the query, but from the prepared pipe `input`.
    InterpreterSelectQuery(
        const ASTPtr & query_ptr_,
        ContextPtr context_,
        Pipe input_pipe_,
        const SelectQueryOptions & = {});

    /// Read data not from the table specified in the query, but from the specified `storage_`.
    InterpreterSelectQuery(
        const ASTPtr & query_ptr_,
        ContextPtr context_,
        const StoragePtr & storage_,
        const StorageMetadataPtr & metadata_snapshot_ = nullptr,
        const SelectQueryOptions & = {});

    ~InterpreterSelectQuery() override;

    /// Execute a query. Get the stream of blocks to read.
    BlockIO execute() override;

    /// Builds QueryPlan for current query.
    virtual void buildQueryPlan(QueryPlan & query_plan) override;

    bool ignoreLimits() const override { return options.ignore_limits; }
    bool ignoreQuota() const override { return options.ignore_quota; }

    virtual void ignoreWithTotals() override;

    ASTPtr getQuery() const { return query_ptr; }

    const SelectQueryInfo & getQueryInfo() const { return query_info; }

    SelectQueryInfo getQueryInfo() { return query_info; }

    const SelectQueryExpressionAnalyzer * getQueryAnalyzer() const { return query_analyzer.get(); }

    const ExpressionAnalysisResult & getAnalysisResult() const { return analysis_result; }

    const Names & getRequiredColumns() const { return required_columns; }

    bool hasAggregation() const { return query_analyzer->hasAggregation(); }

    static Pipe generateNullSourcePipe(
        const Block & source_header, const SelectQueryInfo & query_info);

    static void addEmptySourceToQueryPlan(
        QueryPlan & query_plan, const Block & source_header, const SelectQueryInfo & query_info, ContextPtr context_);

    Names getRequiredColumns() { return required_columns; }

    static FillColumnDescription getWithFillDescription(const ASTOrderByElement & order_by_elem, ContextPtr context);

    TreeRewriterResultPtr getSyntaxAnalyzerResult() { return syntax_analyzer_result; }

private:
    friend class InterpreterPerfectShard;
    friend struct SelectQueryInfo;

    InterpreterSelectQuery(
        const ASTPtr & query_ptr_,
        ContextPtr context_,
        const BlockInputStreamPtr & input_,
        std::optional<Pipe> input_pipe,
        const StoragePtr & storage_,
        const SelectQueryOptions &,
        const Names & required_result_column_names = {},
        const StorageMetadataPtr & metadata_snapshot_ = nullptr);

    ASTSelectQuery & getSelectQuery() { return query_ptr->as<ASTSelectQuery &>(); }

    void addPrewhereAliasActions();
    bool shouldMoveToPrewhere();

    Block getSampleBlockImpl();

    void executeImpl(QueryPlan & query_plan, const BlockInputStreamPtr & prepared_input, std::optional<Pipe> prepared_pipe);

    /// Different stages of query execution.

    void executeFetchColumns(QueryProcessingStage::Enum processing_stage, QueryPlan & query_plan);
    void executeWhere(QueryPlan & query_plan, const ActionsDAGPtr & expression, bool remove_filter);
    void executeAggregation(
        QueryPlan & query_plan, const ActionsDAGPtr & expression, bool overflow_row, bool final, InputOrderInfoPtr group_by_info);
    void executeMergeAggregated(QueryPlan & query_plan, bool overflow_row, bool final, bool has_grouping_sets = false);
    void executeTotalsAndHaving(QueryPlan & query_plan, bool has_having, const ActionsDAGPtr & expression, bool overflow_row, bool final);
    void executeHaving(QueryPlan & query_plan, const ActionsDAGPtr & expression);
    static void executeExpression(QueryPlan & query_plan, const ActionsDAGPtr & expression, const std::string & description);
    /// FIXME should go through ActionsDAG to behave as a proper function
    void executeWindow(QueryPlan & query_plan);
    void executeOrder(QueryPlan & query_plan, InputOrderInfoPtr sorting_info);
    void executeOrderOptimized(QueryPlan & query_plan, InputOrderInfoPtr sorting_info, UInt64 limit, SortDescription & output_order_descr);
    void executeWithFill(QueryPlan & query_plan);
    void executeMergeSorted(QueryPlan & query_plan, const std::string & description);
    void executePreLimit(QueryPlan & query_plan, bool do_not_skip_offset);
    void executeLimitBy(QueryPlan & query_plan);
    void executeLimit(QueryPlan & query_plan);
    void executeOffset(QueryPlan & query_plan);
    static void executeProjection(QueryPlan & query_plan, const ActionsDAGPtr & expression);
    void executeDistinct(QueryPlan & query_plan, bool before_order, Names columns, bool pre_distinct);
    void executeExtremes(QueryPlan & query_plan);
    void executeSubqueriesInSetsAndJoins(QueryPlan & query_plan, std::unordered_map<String, SubqueryForSet> & subqueries_for_sets);
    void
    executeMergeSorted(QueryPlan & query_plan, const SortDescription & sort_description, UInt64 limit, const std::string & description);

    String generateFilterActions(ActionsDAGPtr & actions, const Names & prerequisite_columns = {}) const;

    enum class Modificator
    {
        ROLLUP = 0,
        CUBE = 1
    };

    void executeRollupOrCube(QueryPlan & query_plan, Modificator modificator);

    /** If there is a SETTINGS section in the SELECT query, then apply settings from it.
      *
      * Section SETTINGS - settings for a specific query.
      * Normally, the settings can be passed in other ways, not inside the query.
      * But the use of this section is justified if you need to set the settings for one subquery.
      */
    void initSettings();

    void rewriteQueryBaseOnView();

    TreeRewriterResultPtr syntax_analyzer_result;
    std::unique_ptr<SelectQueryExpressionAnalyzer> query_analyzer;
    SelectQueryInfo query_info;
    ASTPtr query_for_perfect_shard;

    /// Is calculated in getSampleBlock. Is used later in readImpl.
    ExpressionAnalysisResult analysis_result;
    /// For row-level security.
    ASTPtr row_policy_filter;
    FilterDAGInfoPtr filter_info;

    QueryProcessingStage::Enum from_stage = QueryProcessingStage::FetchColumns;

    /// List of columns to read to execute the query.
    Names required_columns;
    /// Structure of query source (table, subquery, etc).
    Block source_header;

    /// Actions to calculate ALIAS if required.
    ActionsDAGPtr alias_actions;

    /// The subquery interpreter, if the subquery
    std::unique_ptr<InterpreterSelectWithUnionQuery> interpreter_subquery;

    /// Table from where to read data, if not subquery.
    StoragePtr storage;
    StorageID table_id = StorageID::createEmpty(); /// Will be initialized if storage is not nullptr
    TableLockHolder table_lock;

    /// Used when we read from prepared input, not table or subquery.
    BlockInputStreamPtr input;
    std::optional<Pipe> input_pipe;

    Poco::Logger * log;
    StorageMetadataPtr metadata_snapshot;
    bool has_join = false;
};

}
