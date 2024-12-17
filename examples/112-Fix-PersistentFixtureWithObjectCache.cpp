//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0

// 101-Fix-SectionWithObjectCache.cpp

// Catch has two ways to express fixtures:
// - Sections (this file)
// - Traditional class-based fixtures

// main() provided by linkage to Catch2WithMain

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <unordered_map>

template <typename T>
static constexpr bool AlwaysFalse = false;

template <typename InFunc>
class CachedFunction {
    static_assert( AlwaysFalse<InFunc>,
                   "The function to be cached must be either a free function, "
                   "or a lambda without captures" );
};

template <typename RetType, typename... InParamTypes>
class CachedFunction<RetType( std::tuple<InParamTypes...> )> {
public:
    using FunctionType = RetType( std::tuple<InParamTypes...> );
    using ArgsType = std::tuple<InParamTypes...>;

    CachedFunction( FunctionType* InFunc ): m_Function( InFunc ) {}

    RetType operator()( ArgsType&& InArgs ) {

        auto iter = std::find_if( m_Cache.cbegin(),
                                  m_Cache.cend(),
                                  [&InArgs]( const CallRecord& InRecord ) {
                                      return InArgs == InRecord.Args;
                                  } );
        if ( iter != m_Cache.cend() ) {
            return iter->Value;
        } else {
            auto args = InArgs;
            m_Cache.emplace_back( CallRecord{
                std::move( args ), m_Function( std::move( InArgs ) ) } );
            return m_Cache.back().Value;
        }
    }

private:
    struct CallRecord {
        std::tuple<InParamTypes...> Args;
        RetType Value;
    };

    FunctionType* m_Function;
    std::vector<CallRecord> m_Cache;
};

template <typename InFunc>
struct CachedFunctionDeductionGuideHelper;

template <typename Ret, typename... InArgs>
struct CachedFunctionDeductionGuideHelper<Ret( InArgs... )> {
    using Type = Ret( InArgs... );
};

template<typename InFuncType>
auto MakeCachedFunction(InFuncType* InFunc)
{
    return CachedFunction<
        typename CachedFunctionDeductionGuideHelper<InFuncType>::Type>(
        InFunc );
}

static int NumObjectsCreated = 0;

class ObjectCaches {

public:

    using FuncType = std::function<void(void*, void*)>;

    template <typename InFuncType, typename... InParamTypes>
    auto Invoke( InFuncType* InFunc, InParamTypes&&... InParams ) {

        //using ResultType = std::result_of_t<InFuncType(std::tuple<InParamTypes...>)>;

        using ResultType = decltype(InFunc(InParams...));
        alignas( ResultType ) unsigned char returnBytes[sizeof(ResultType)];

        auto& cache = m_Caches.try_emplace(
            InFunc, 
            [cachedFunc = MakeCachedFunction(InFunc)]( void* OutResult,
                                                 void* InArg ) mutable {

                    new ( OutResult ) ResultType( cachedFunc(*static_cast<std::tuple<InParamTypes...>*>( InArg ) ) );
            } );

        auto params = std::forward_as_tuple<InParamTypes...>( InParams... );
        auto& func = *cache.first;
        func.second( &returnBytes, &params );

        return *reinterpret_cast<ResultType*>( &returnBytes );
    }

private:

    std::unordered_map<void*, FuncType> m_Caches;
};

class ExpensiveObject {
public:
    ExpensiveObject( const int InValue ): Value( InValue ) {
        ++NumObjectsCreated;
    }

    int GetValue() const { return Value; }

private:
    int Value = 0;
};

class Fixture {
protected:
    mutable ObjectCaches m_Caches;
};

TEST_CASE_PERSISTENT_FIXTURE( Fixture, "ObjectCache" ) {

    SECTION( "Without ObjectCache" ) {
        for ( int i = 0; i < 5; ++i ) {
            DYNAMIC_SECTION( "With value = " << i ) {
                const ExpensiveObject object{ i };

                DYNAMIC_SECTION( "Not less than zero" ) {
                    REQUIRE( object.GetValue() >= 0 );
                }

                DYNAMIC_SECTION( "is i" ) { REQUIRE( object.GetValue() == i ); }

                DYNAMIC_SECTION( "Less than 5" ) {
                    REQUIRE( object.GetValue() < 5 );
                }
            }
        }
    }

    SECTION( "Check num creation" ) { REQUIRE( NumObjectsCreated == 15 ); }

    SECTION( "Reset counter" ) { NumObjectsCreated = 0; }

    SECTION( "With Object Cache" ) {
        for ( int i = 0; i < 5; ++i ) {

            DYNAMIC_SECTION("With value = " << i ) {

                auto callable = [](const std::tuple<int> InParams)
                    {
                        return ExpensiveObject{ std::get<0>( InParams ) };
                    };
                
                using Type = std::result_of_t<decltype(callable)(std::tuple<int>)>; 
                const auto object = m_Caches.Invoke(
                    +[](const std::tuple<int> InParams)
                    {
                        return ExpensiveObject{ std::get<0>( InParams ) };
                    }, i);
                DYNAMIC_SECTION( "Not less than zero" ) {
                    REQUIRE( object.GetValue() >= 0 );
                }
                
                DYNAMIC_SECTION( "is i" ) { REQUIRE( object.GetValue() == i ); }
                
                DYNAMIC_SECTION( "Less than 5" ) {
                    REQUIRE( object.GetValue() < 5 );
                }
            }
        }
    }

    SECTION( "Check num creation again" ) {
        REQUIRE( NumObjectsCreated == 5 );
    }
}
