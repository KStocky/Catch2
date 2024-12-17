
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

#include <memory>
#include <tuple>

template<typename T>
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
            m_Cache.emplace_back(
                CallRecord{ std::move( args ),
                            m_Function( std::move( InArgs ) ) } );
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


static int NumObjectsCreated = 0;

class ExpensiveObject {
public:
    ExpensiveObject( const int InValue ): Value( InValue ) {
        ++NumObjectsCreated;
    }

    int GetValue() const { return Value; }

private:
    int Value = 0;
};

static auto CreateObject(const std::tuple<int> InValue)
{
    return std::tuple<ExpensiveObject>( ExpensiveObject{ std::get<0>(InValue) } );
}

template<typename Ret, typename... ParamTypes>
static auto
CreateCachedFunction( Ret ( *InFunc )( std::tuple<ParamTypes...> ) ) {
    return CachedFunction<Ret(std::tuple<ParamTypes...>)>( InFunc );
}

#define DYNAMIC_SECTION_OBJECT_CACHE( Var, CreationFunc, Params, ... ) \
    INTERNAL_CATCH_DYNAMIC_SECTION( __VA_ARGS__ ) \
    { \
    auto& Var##CreationFunc = Get( CreationFunc ); \
    auto Var = Var##CreationFunc( Params ); \

class Fixture
{
protected:
    
    template <typename Ret, typename... ParamTypes>
    CachedFunction<std::tuple<ExpensiveObject>(std::tuple<int>)>& Get(Ret ( *InFunc )( std::tuple<ParamTypes...> )) const
    {
        if (!m_Cached)
        {
            m_Cached = std::make_unique<CachedFunction<std::tuple<ExpensiveObject>( std::tuple<int> )>>(InFunc);
        }

        return *m_Cached;
    }
    
private:
    mutable std::unique_ptr<CachedFunction<std::tuple<ExpensiveObject>( std::tuple<int> )>> m_Cached;
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

    SECTION("With Object Cache") {
        for ( int i = 0; i < 5; ++i ) {
    
            DYNAMIC_SECTION_OBJECT_CACHE( objects, CreateObject, std::make_tuple(i), "With value = " << i ) {
    
                const auto& object = std::get<0>(objects);
                DYNAMIC_SECTION( "Not less than zero" ) {
                    REQUIRE( object.GetValue() >= 0 );
                }
    
                DYNAMIC_SECTION( "is i" ) { REQUIRE( object.GetValue() == i ); }
    
                DYNAMIC_SECTION( "Less than 5" ) {
                    REQUIRE( object.GetValue() < 5 );
                }
            }}
        }
    }
    
    SECTION( "Check num creation again" ) { REQUIRE( NumObjectsCreated == 15 ); }
}
