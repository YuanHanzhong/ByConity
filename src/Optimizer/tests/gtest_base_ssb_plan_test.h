/*
 * Copyright (2022) Bytedance Ltd. and/or its affiliates
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <Optimizer/tests/test_config.h>
#include <Optimizer/tests/gtest_base_plan_test.h>

namespace DB
{

/**
 * change resource path in test.config.h.in.
 */
class BaseSsbPlanTest : public AbstractPlanTestSuite
{
public:
    explicit BaseSsbPlanTest(const std::unordered_map<String, Field> & settings = {}, int sf_ = 100) : AbstractPlanTestSuite("ssb" + std::to_string(sf_), settings), sf(sf_)
    {
        createTables();
        dropTableStatistics();
        loadTableStatistics();
    }

    std::vector<std::filesystem::path> getTableDDLFiles() override { return {SSB_TABLE_DDL_FILE}; }
    std::filesystem::path getStatisticsFile() override { return SSB100_TABLE_STATISTICS_FILE; }
    std::filesystem::path getQueriesDir() override { return SSB_QUERIES_DIR; }
    std::filesystem::path getExpectedExplainDir() override { 
        return std::filesystem::path(SSB_EXPECTED_EXPLAIN_RESULT) / ("ssb" + std::to_string(sf));
    }

    void setLabel(const std::string & label_) { this->label = "_" + label_; }

    int sf;
    std::string label;
};

}
