#include "FunctionsMultiStringSearch.h"
#include "FunctionFactory.h"
#include "MultiSearchImpl.h"
#include "PositionImpl.h"


namespace DB
{
namespace
{

struct NameMultiSearchAnyUTF8
{
    static constexpr auto name = "multiSearchAnyUTF8";
};
using FunctionMultiSearchUTF8 = FunctionsMultiStringSearch<MultiSearchImpl<PositionCaseSensitiveUTF8>, NameMultiSearchAnyUTF8>;

}

REGISTER_FUNCTION(MultiSearchAnyUTF8)
{
    factory.registerFunction<FunctionMultiSearchUTF8>();
}

}
