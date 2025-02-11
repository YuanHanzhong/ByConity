#include <Functions/IFunction.h>
#include <Functions/FunctionFactory.h>
#include <Functions/FunctionHelpers.h>
#include <DataTypes/DataTypeString.h>
#include <Columns/ColumnString.h>
#include <Interpreters/Context.h>
#include <Common/Macros.h>
#include <Core/Field.h>


namespace DB
{
namespace ErrorCodes
{
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
}

namespace
{

/** Get scalar value of sub queries from query context via IAST::Hash.
  */
class FunctionGetScalar : public IFunction, WithContext
{
public:
    static constexpr auto name = "__getScalar";
    static FunctionPtr create(ContextPtr context_)
    {
        return std::make_shared<FunctionGetScalar>(context_);
    }

    explicit FunctionGetScalar(ContextPtr context_) : WithContext(context_) {}

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override
    {
        return 1;
    }

    DataTypePtr getReturnTypeImpl(const ColumnsWithTypeAndName & arguments) const override
    {
        if (arguments.size() != 1 || !isString(arguments[0].type) || !arguments[0].column || !isColumnConst(*arguments[0].column))
            throw Exception("Function " + getName() + " accepts one const string argument", ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);
        auto scalar_name = assert_cast<const ColumnConst &>(*arguments[0].column).getValue<String>();
        ContextPtr query_context = getContext()->hasQueryContext() ? getContext()->getQueryContext() : getContext();
        scalar = query_context->getScalar(scalar_name).getByPosition(0);
        return scalar.type;
    }

    ColumnPtr executeImpl(const ColumnsWithTypeAndName &, const DataTypePtr &, size_t input_rows_count) const override
    {
        return ColumnConst::create(scalar.column, input_rows_count);
    }

private:
    mutable ColumnWithTypeAndName scalar;
};

}

REGISTER_FUNCTION(GetScalar)
{
    factory.registerFunction<FunctionGetScalar>();
}

}
