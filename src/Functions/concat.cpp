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

#include <Columns/ColumnString.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/getLeastSupertype.h>
#include <Functions/FunctionsConversion.h>
#include <Functions/FunctionFactory.h>
#include <Functions/FunctionHelpers.h>
#include <Functions/GatherUtils/Algorithms.h>
#include <Functions/GatherUtils/Sinks.h>
#include <Functions/GatherUtils/Slices.h>
#include <Functions/GatherUtils/Sources.h>
#include <Functions/IFunction.h>
#include <IO/WriteHelpers.h>
#include <common/map.h>
#include <common/range.h>

#include "formatString.h"

namespace DB
{
namespace ErrorCodes
{
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
    extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
    extern const int ILLEGAL_COLUMN;
}

using namespace GatherUtils;

namespace
{

template <typename Name, bool is_injective>
class ConcatImpl : public IFunction
{
public:
    static constexpr auto name = Name::name;
    static FunctionPtr create(ContextPtr /*context*/) { return std::make_shared<ConcatImpl>(); }

    String getName() const override { return name; }

    bool isVariadic() const override { return true; }

    size_t getNumberOfArguments() const override { return 0; }

    bool isInjective(const ColumnsWithTypeAndName &) const override { return is_injective; }

    bool useDefaultImplementationForConstants() const override { return true; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (arguments.size() < 2)
            throw Exception(
                "Number of arguments for function " + getName() + " doesn't match: passed " + toString(arguments.size())
                    + ", should be at least 2.",
                ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

        if (arguments.size() > FormatStringImpl::argument_threshold)
            throw Exception(
                "Number of arguments for function " + getName() + " doesn't match: passed " + toString(arguments.size())
                    + ", should be at most " + std::to_string(FormatStringImpl::argument_threshold),
                ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

        return std::make_shared<DataTypeString>();
    }

    ColumnPtr executeImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr &, size_t input_rows_count) const override
    {
        ColumnsWithTypeAndName args;

        /// convert all columns to ColumnString.
        auto convert = std::make_shared<FunctionToString>();
        auto type_string = std::make_shared<DataTypeString>();
        for (const auto & arg: arguments)
        {
            ColumnPtr col = arg.column;
            if (!isStringOrFixedString(arg.type))
                col = convert->executeImpl({arg}, type_string, input_rows_count);
            args.emplace_back(col, type_string, arg.name);
        }

        /// Format function is not proven to be faster for two arguments.
        /// Actually there is overhead of 2 to 5 extra instructions for each string for checking empty strings in FormatImpl.
        /// Though, benchmarks are really close, for most examples we saw executeBinary is slightly faster (0-3%).
        /// For 3 and more arguments FormatImpl is much faster (up to 50-60%).
        if (arguments.size() == 2)
            return executeBinary(args, input_rows_count);
        else
            return executeFormatImpl(args, input_rows_count);
    }

protected:
    ColumnPtr executeBinary(const ColumnsWithTypeAndName & arguments, size_t input_rows_count) const
    {
        const IColumn * c0 = arguments[0].column.get();
        const IColumn * c1 = arguments[1].column.get();

        const ColumnString * c0_string = checkAndGetColumn<ColumnString>(c0);
        const ColumnString * c1_string = checkAndGetColumn<ColumnString>(c1);
        const ColumnConst * c0_const_string = checkAndGetColumnConst<ColumnString>(c0);
        const ColumnConst * c1_const_string = checkAndGetColumnConst<ColumnString>(c1);

        auto c_res = ColumnString::create();

        if (c0_string && c1_string)
            concat(StringSource(*c0_string), StringSource(*c1_string), StringSink(*c_res, c0->size()));
        else if (c0_string && c1_const_string)
            concat(StringSource(*c0_string), ConstSource<StringSource>(*c1_const_string), StringSink(*c_res, c0->size()));
        else if (c0_const_string && c1_string)
            concat(ConstSource<StringSource>(*c0_const_string), StringSource(*c1_string), StringSink(*c_res, c0->size()));
        else
        {
            /// Fallback: use generic implementation for not very important cases.
            return executeFormatImpl(arguments, input_rows_count);
        }

        return c_res;
    }

    ColumnPtr executeFormatImpl(const ColumnsWithTypeAndName & arguments, size_t input_rows_count) const
    {
        const size_t num_arguments = arguments.size();
        assert(num_arguments >= 2);

        auto c_res = ColumnString::create();
        std::vector<const ColumnString::Chars *> data(num_arguments);
        std::vector<const ColumnString::Offsets *> offsets(num_arguments);
        std::vector<size_t> fixed_string_sizes(num_arguments);
        std::vector<String> constant_strings(num_arguments);
        bool has_column_string = false;
        bool has_column_fixed_string = false;
        for (size_t i = 0; i < num_arguments; ++i)
        {
            const ColumnPtr & column = arguments[i].column;
            if (const ColumnString * col = checkAndGetColumn<ColumnString>(column.get()))
            {
                has_column_string = true;
                data[i] = &col->getChars();
                offsets[i] = &col->getOffsets();
            }
            else if (const ColumnFixedString * fixed_col = checkAndGetColumn<ColumnFixedString>(column.get()))
            {
                has_column_fixed_string = true;
                data[i] = &fixed_col->getChars();
                fixed_string_sizes[i] = fixed_col->getN();
            }
            else if (const ColumnConst * const_col = checkAndGetColumnConstStringOrFixedString(column.get()))
            {
                constant_strings[i] = const_col->getValue<String>();
            }
            else
                throw Exception(
                    "Illegal column " + column->getName() + " of argument of function " + getName(), ErrorCodes::ILLEGAL_COLUMN);
        }

        String pattern;
        pattern.reserve(2 * num_arguments);

        for (size_t i = 0; i < num_arguments; ++i)
            pattern += "{}";

        FormatStringImpl::formatExecute(
            has_column_string,
            has_column_fixed_string,
            std::move(pattern),
            data,
            offsets,
            fixed_string_sizes,
            constant_strings,
            c_res->getChars(),
            c_res->getOffsets(),
            input_rows_count);

        return c_res;
    }
};

struct NameConcat
{
    static constexpr auto name = "concat";
};
struct NameConcatAssumeInjective
{
    static constexpr auto name = "concatAssumeInjective";
};

using FunctionConcat = ConcatImpl<NameConcat, false>;
using FunctionConcatAssumeInjective = ConcatImpl<NameConcatAssumeInjective, true>;


/// Also works with arrays.
class ConcatOverloadResolver : public IFunctionOverloadResolver
{
public:
    static constexpr auto name = "concat";
    static FunctionOverloadResolverPtr create(ContextPtr context) { return std::make_unique<ConcatOverloadResolver>(context); }

    explicit ConcatOverloadResolver(ContextPtr context_) : context(context_) {}

    String getName() const override { return name; }
    size_t getNumberOfArguments() const override { return 0; }
    bool isVariadic() const override { return true; }

    FunctionBasePtr buildImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr & return_type) const override
    {
        if (isArray(arguments.at(0).type))
        {
            return FunctionFactory::instance().getImpl("arrayConcat", context)->build(arguments);
        }
        else
            return std::make_unique<FunctionToFunctionBaseAdaptor>(
                FunctionConcat::create(context), collections::map<DataTypes>(arguments, [](const auto & elem) { return elem.type; }), return_type);
    }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (arguments.size() < 2)
            throw Exception(
                "Number of arguments for function " + getName() + " doesn't match: passed " + toString(arguments.size())
                    + ", should be at least 2.",
                ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

        /// We always return Strings from concat, even if arguments were fixed strings.
        return std::make_shared<DataTypeString>();
    }

private:
    ContextPtr context;
};

}

REGISTER_FUNCTION(Concat)
{
    factory.registerFunction<ConcatOverloadResolver>(FunctionFactory::CaseInsensitive);
    factory.registerFunction<FunctionConcatAssumeInjective>();
}

}
