#include "FunctionsMultiStringSearch.h"
#include "FunctionFactory.h"
#include "MultiSearchFirstPositionImpl.h"
#include "PositionImpl.h"


namespace DB
{
namespace
{

struct NameMultiSearchFirstPositionCaseInsensitiveUTF8
{
    static constexpr auto name = "multiSearchFirstPositionCaseInsensitiveUTF8";
};

using FunctionMultiSearchFirstPositionCaseInsensitiveUTF8 = FunctionsMultiStringSearch<
    MultiSearchFirstPositionImpl<PositionCaseInsensitiveUTF8>,
    NameMultiSearchFirstPositionCaseInsensitiveUTF8>;

}

REGISTER_FUNCTION(MultiSearchFirstPositionCaseInsensitiveUTF8)
{
    factory.registerFunction<FunctionMultiSearchFirstPositionCaseInsensitiveUTF8>();
}

}
