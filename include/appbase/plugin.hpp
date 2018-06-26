#pragma once
#include <boost/program_options.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <string>
#include <vector>
#include <map>

#define APPBASE_PLUGIN_REQUIRES_VISIT( r, visitor, elem ) \
  visitor( appbase::app().register_plugin<elem>() ); 

/*
  APPBASE_PLUGIN_REQUIRES宏值得展开说一下，这个宏用在用户自已定义的插件中，
  用于指定插件的依赖项。
  当调用者调用如 plugin_requires([&](auto& plug){ plug.initialize(options); }); 的时候
  该宏展开后会对每一个依赖项调用参数中的lamda函数。
  具体可以参考BOOST_PP_SEQ_FOR_EACH宏的使用方法
*/

#define APPBASE_PLUGIN_REQUIRES( PLUGINS )                               \
   template<typename Lambda>                                           \
   void plugin_requires( Lambda&& l ) {                                \
      BOOST_PP_SEQ_FOR_EACH( APPBASE_PLUGIN_REQUIRES_VISIT, l, PLUGINS ) \
   }

namespace appbase {

   using boost::program_options::options_description;
   using boost::program_options::variables_map;
   using std::string;
   using std::vector;
   using std::map;

   class application;
   application& app();

   class abstract_plugin {
      public:
         enum state {
            registered, ///< the plugin is constructed but doesn't do anything
            initialized, ///< the plugin has initialized any state required but is idle
            started, ///< the plugin is actively running
            stopped ///< the plugin is no longer running
         };

         virtual ~abstract_plugin(){}
         virtual state get_state()const = 0;
         virtual const std::string& name()const  = 0;
         virtual void set_program_options( options_description& cli, options_description& cfg ) = 0;
         virtual void initialize(const variables_map& options) = 0;
         virtual void startup() = 0;
         virtual void shutdown() = 0;
   };

   template<typename Impl>
   class plugin;
}
