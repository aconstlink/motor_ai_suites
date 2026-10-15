#include "workload.hpp"

int main( reference::void_t )
{
    bool good = true ;
    {
        auto objects = reference::make_objects(1000) ;
        reference::update(objects, 0) ;
        good &= motor::log::global_t::test( "Reference object count", objects.size() == 1000 ) ;
        for( auto const & object : objects )
        {
            good &= std::abs(object.world[12] - object.position.x()) < 0.0001f ;
            good &= std::abs(object.world[13] - object.position.y()) < 0.0001f ;
            good &= object.world[15] == 1.0f ;
        }
        good &= motor::log::global_t::test( "Column-major transform preserves translation", good ) ;
        auto const before = objects.front().world ;
        reference::update(objects, 30) ;
        good &= motor::log::global_t::test( "Animation changes rotation, not placement",
            before != objects.front().world && before[12] == objects.front().world[12] && before[13] == objects.front().world[13] ) ;
        auto const gl = reference::projection(true), dx = reference::projection(false) ;
        good &= motor::log::global_t::test( "Projection near/far GL and D3D",
            std::abs(-gl[10]+gl[14]+1.0f) < 0.0001f && std::abs(-100*gl[10]+gl[14]-1.0f) < 0.0001f &&
            std::abs(-dx[10]+dx[14]) < 0.0001f && std::abs(-100*dx[10]+dx[14]-1.0f) < 0.0001f ) ;
        motor::vector<double> values {4,1,2,3} ;
        good &= motor::log::global_t::test( "Even sample median", reference::median(values) == 2.5 ) ;
        good &= motor::log::global_t::test( "Cube topology", std::size(reference::vertices) == 24 && std::size(reference::indices) == 36 ) ;
        for( auto const index : reference::indices ) good &= index < std::size(reference::vertices) ;
    }
    motor::log::global_t::deinit() ;
    return good ? 0 : 1 ;
}
