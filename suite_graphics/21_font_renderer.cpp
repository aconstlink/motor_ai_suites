#include <motor/platform/global.h>
#include <motor/gfx/font/text_render_2d.h>
#include <motor/format/global.h>
#include <motor/format/future_items.hpp>
#include <motor/property/property_sheet.hpp>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/profiling/global.h>
#include <motor/io/global.h>
#include <motor/std/string>

#include <chrono>
#include <cmath>

namespace sample
{
    using namespace motor::core::types ;

    struct options
    {
        bool_t d3d = false ;
        bool_t smoke = false ;
        bool_t still = false ;
    } ;

    static char const * const phases[] = {
        "Sizes, faces and colors", "Only group 2", "All groups empty",
        "900 glyphs in group 1", "Restored text and fallback" } ;

    class font_app : public motor::application::app
    {
        options _options ;
        bool_t * _passed ;
        motor::io::database_t _db = motor::io::database_t(
            motor::io::path_t( MOTOR_FONT_ASSETS ), "./", "font_renderer" ) ;
        motor::gfx::text_render_2d_mtr_t _text = nullptr ;
        motor::graphics::state_object_t _state ;
        float_t _aspect = 1280.0f / 720.0f ;
        float_t _time = 0.0f ;
        size_t _phase = 0, _frames = 0, _counter = 0 ;
        bool_t _quitting = false ;
        std::chrono::steady_clock::time_point _started ;

        bool_t load_font( void_t )
        {
            motor::property::property_sheet_mtr_t properties = motor::shared( motor::property::property_sheet_t() ) ;
            {
                motor::font::code_points_t points ;
                for( uint32_t c = 33 ; c <= 126 ; ++c ) points.emplace_back( c ) ;
                properties->set_value( "code_points", points ) ;
                properties->set_value( "additional_locations", motor::vector<motor::io::location_t>{
                    motor::io::location_t( "Cousine-Regular.ttf" ) } ) ;
                properties->set_value<size_t>( "atlas_width", 512 ) ;
                properties->set_value<size_t>( "atlas_height", 512 ) ;
                properties->set_value<size_t>( "point_size", 48 ) ;
                properties->set_value<size_t>( "dpi", 72 ) ;
            }
            auto * registry = motor::format::global::register_default_modules(
                motor::shared( motor::format::module_registry_t() ) ) ;
            auto future = registry->import_from( motor::io::location_t( "Roboto-Medium.ttf" ),
                &_db, motor::move( properties ) ) ;
            auto * item = future.get() ;
            auto * atlas_item = dynamic_cast<motor::format::glyph_atlas_item_t *>( item ) ;
            bool_t valid = atlas_item != nullptr && atlas_item->obj != nullptr ;
            if( valid )
            {
                auto * atlas = atlas_item->obj ;
                valid = atlas->get_num_images() > 0 ;
                for( size_t face = 0 ; face < 2 ; ++face )
                {
                    for( uint32_t c = 33 ; c <= 126 ; ++c )
                    {
                        size_t index = 0 ;
                        motor::font::glyph_atlas_t::glyph_info_t glyph ;
                        valid = atlas->find_glyph( face, c, index, glyph ) && valid ;
                    }
                }
                motor::log::global_t::status( "[font] atlas pages: " + motor::to_string( atlas->get_num_images() ) +
                    ", glyphs: " + motor::to_string( atlas->get_num_glyphs() ) ) ;
                if( valid )
                {
                    _text = motor::shared( motor::gfx::text_render_2d_t() ) ;
                    _text->init( "ai_suites.font_renderer", motor::move( atlas_item->obj ), 3 ) ;
                }
            }
            motor::log::global_t::test( "[font] imported both ASCII faces and fallback glyphs", valid ) ;
            if( item != nullptr ) { item->release() ; motor::release( motor::move( item ) ) ; }
            motor::release( motor::move( registry ) ) ;
            return valid ;
        }

        void_t text( size_t const group, size_t const face, size_t const points,
            float_t const x, float_t const y, motor::math::vec4f_cref_t color,
            motor::string_cref_t value )
        {
            _text->draw_text( group, face, points, motor::math::vec2f_t( x * _aspect, y ), color, value ) ;
        }

    public:
        font_app( options const opts, bool_t * passed ) noexcept : _options( opts ), _passed( passed ) {}

        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            if( !load_font() ) { _quitting = true ; close() ; return ; }
            {
                motor::graphics::render_state_sets_t states ;
                states.depth_s.do_change = true ;
                states.depth_s.ss.do_activate = false ;
                states.depth_s.ss.do_depth_write = false ;
                states.polygon_s.do_change = true ;
                states.polygon_s.ss.do_activate = false ;
                states.clear_s.do_change = true ;
                states.clear_s.ss.do_activate = true ;
                states.clear_s.ss.do_color_clear = true ;
                states.clear_s.ss.do_depth_clear = true ;
                states.clear_s.ss.clear_color = motor::math::vec4f_t( 0.035f, 0.045f, 0.055f, 1.0f ) ;
                _state = motor::graphics::state_object_t( "font_renderer.background" ) ;
                _state.add_render_state_set( states ) ;
            }
            {
                motor::application::window_info_t info ;
                info.window_name = _options.d3d ? "motor | font renderer | D3D11" : "motor | font renderer | GL4" ;
                info.x = 60 ; info.y = 60 ; info.w = 1280 ; info.h = 720 ;
                info.gen = _options.d3d ? motor::application::graphics_generation::gen4_d3d11 :
                    motor::application::graphics_generation::gen4_gl4 ;
                send_window_message( create_window( info ), []( auto & window )
                {
                    window.send_message( motor::application::show_message( { true } ) ) ;
                    window.send_message( motor::application::vsync_message_t( { true } ) ) ;
                } ) ;
            }
        }

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting || _text == nullptr ) return ;
            if( !_options.still ) _time += data.sec_dt ;
            if( !_options.smoke && !_options.still ) _phase = size_t( _time / 4.0f ) % 5 ;
            ++_counter ;
            {
                auto const identity = motor::math::mat4f_t().identity() ;
                auto projection = identity ;
                projection[0] = 1.0f / _aspect ;
                _text->set_view_proj( identity, projection ) ;
                // Exercise both the all-group and individual-group matrix paths.
                _text->set_view_proj( 2, identity, projection ) ;
            }
            auto const white = motor::math::vec4f_t( 0.91f, 0.94f, 0.96f, 1.0f ) ;
            auto const teal = motor::math::vec4f_t( 0.2f, 0.85f, 0.72f, 1.0f ) ;
            auto const gold = motor::math::vec4f_t( 1.0f, 0.72f, 0.3f, 1.0f ) ;
            if( _phase != 2 )
            {
                size_t const header_group = _phase == 1 ? 2 : 0 ;
                text( header_group, 0, 72, -0.88f, 0.76f, white, "motor / typography" ) ;
                text( header_group, 1, 26, -0.88f, 0.62f, teal, phases[_phase] ) ;
            }
            if( _phase == 0 || _phase == 4 )
            {
                text( 0, 0, 48, -0.88f, 0.37f, gold, "The quick brown fox." ) ;
                text( 0, 1, 34, -0.88f, 0.20f, white, "Cousine: 0123456789 + - * / =" ) ;
                text( 1, 0, 26, -0.88f, 0.03f, white, "ABCDEFGHIJKLMNOPQRSTUVWXYZ" ) ;
                text( 1, 0, 20, -0.88f, -0.10f, teal, "abcdefghijklmnopqrstuvwxyz !?.,:; []{}()" ) ;
                text( 1, 1, 18, -0.88f, -0.21f, gold, "Small type / spacing / 0123456789" ) ;
                text( 1, 0, 32, -0.88f, -0.40f, white, "Frame " + motor::to_string( _counter ) ) ;
                float_t const x = -0.55f + 0.25f * std::sin( _time ) ;
                text( 2, 0, 42, x, -0.65f, teal, "Moving letters" ) ;
                if( _phase == 4 )
                    text( 2, 1, 22, -0.88f, -0.84f, gold, "Fallback: [\x01]  Expected: [?]" ) ;
                // Empty strings must not add glyphs or leave stale text behind.
                text( 1, 0, 24, 0.0f, 0.0f, white, "" ) ;
            }
            else if( _phase == 1 )
            {
                text( 2, 1, 36, -0.88f, 0.15f, gold, "Groups 0 and 1 are empty." ) ;
                text( 2, 0, 32, -0.88f, -0.08f, white, "Only the last group remains." ) ;
            }
            else if( _phase == 3 )
            {
                for( size_t row = 0 ; row < 30 ; ++row )
                    text( 1, row % 2, 18, -0.88f, 0.45f - float_t( row ) * 0.044f,
                        row % 2 == 0 ? teal : gold, "0123456789ABCDEFGHIJKLMNOPQRST" ) ;
            }
            _text->prepare_for_rendering() ;
        }

        void_t on_first_frame( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->configure<motor::graphics::state_object_t>( &_state ) ;
            _text->configure( fe ) ;
        }

        void_t on_render( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            if( _quitting || _text == nullptr ) return ;
            fe->push( &_state ) ;
            _text->prepare_for_rendering( fe ) ;
            for( size_t group = 0 ; group < 3 ; ++group ) _text->render( fe, group ) ;
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
            ++_frames ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_options.smoke || _quitting || _frames < 45 ) return ;
            motor::log::global_t::test( motor::string_t( "[font] submitted frames: " ) + phases[_phase], true ) ;
            _frames = 0 ;
            if( ++_phase == 5 ) { *_passed = true ; _quitting = true ; close() ; }
        }

        void_t on_last_frame( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->release<motor::graphics::state_object_t>( &_state ) ;
            // text_render_2d has no frontend release API. Its objects stay alive until shutdown.
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _options.smoke && !_quitting &&
                std::chrono::steady_clock::now() - _started > std::chrono::seconds( 30 ) )
            {
                motor::log::global_t::test( "[font] smoke timeout", false ) ;
                _quitting = true ; close() ;
            }
        }

        void_t on_event( window_id_t const,
            motor::application::window_message_listener::state_vector_cref_t state ) noexcept override
        {
            if( state.resize_changed && state.resize_msg.resize && state.resize_msg.w > 0 && state.resize_msg.h > 0 )
                _aspect = float_t( state.resize_msg.w ) / float_t( state.resize_msg.h ) ;
            if( state.close_changed ) { _quitting = true ; close() ; }
        }

        void_t on_shutdown( void_t ) noexcept override
        {
            if( !_options.smoke && _text != nullptr ) *_passed = true ;
            motor::release( motor::move( _text ) ) ;
        }
    } ;
}

int main( int argc, char ** argv )
{
    sample::options opts ;
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg( argv[i] ) ;
        if( arg == "--gl-only" ) opts.d3d = false ;
        else if( arg == "--d3d-only" ) opts.d3d = true ;
        else if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--still" ) opts.still = true ;
        else
        {
            motor::log::global_t::status( "Usage: 21_font_renderer [--gl-only|--d3d-only] [--smoke] [--still]" ) ;
            motor::log::global_t::deinit() ; return arg == "--help" ? 0 : 2 ;
        }
    }
#ifndef _WIN32
    if( opts.d3d )
    {
        motor::log::global_t::error( "D3D11 requires Windows." ) ;
        motor::log::global_t::deinit() ; return 2 ;
    }
#endif
    bool passed = false ;
    int result = 0 ;
    {
        motor::application::carrier_mtr_t carrier = motor::platform::global_t::create_carrier(
            motor::shared( sample::font_app( opts, &passed ) ) ) ;
        result = carrier->exec() ;
        motor::release( motor::move( carrier ) ) ;
    }
    motor::concurrent::global::deinit() ;
    motor::io::global::deinit() ;
    motor::profiling::global::deinit() ;
    motor::log::global::deinit() ;
    auto const remaining = motor::memory::global::dump_to_std() ;
    return result != 0 ? result : ( remaining != 0 || !passed ? 1 : 0 ) ;
}
