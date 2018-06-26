#pragma once
#include <appbase/plugin.hpp>
#include <appbase/channel.hpp>
#include <appbase/method.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/core/demangle.hpp>
#include <typeindex>

namespace appbase {
   namespace bpo = boost::program_options;
   namespace bfs = boost::filesystem;

   class application
   {
      public:
         ~application();


         /** @brief Set version
          *
          * @param version Version output with -v/--version
          */
         void set_version(uint64_t version);
         /** @brief Get version
          *
          * @return Version output with -v/--version
          */
         uint64_t version() const;
         /** @brief Set default data directory
          *
          * @param data_dir Default data directory to use if not specified
          *                 on the command line.
          */
         void set_default_data_dir(const bfs::path& data_dir = "data-dir");
         /** @brief Get data directory
          *
          * @return Data directory, possibly from command line
          */
         bfs::path data_dir() const;
         /** @brief Set default config directory
          *
          * @param config_dir Default configuration directory to use if not
          *                   specified on the command line.
          */
         void set_default_config_dir(const bfs::path& config_dir = "etc");
         /** @brief Get config directory
          *
          * @return Config directory, possibly from command line
          */
         bfs::path config_dir() const;
         /** @brief Get logging configuration path.
          *
          * @return Logging configuration location from command line
          */
         bfs::path get_logging_conf() const;
         /**
          * @brief Looks for the --plugin commandline / config option and calls initialize on those plugins
          *
          * @tparam Plugin List of plugins to initalize even if not mentioned by configuration. For plugins started by
          * configuration settings or dependency resolution, this template has no effect.
          * @return true if the application and plugins were initialized, false or exception on error
          */
         template<typename... Plugin>
         bool                 initialize(int argc, char** argv) {
            return initialize_impl(argc, argv, {find_plugin<Plugin>()...});
         }

         void                  startup();
         void                  shutdown();

         /**
          *  Wait until quit(), SIGINT or SIGTERM and then shutdown
          */
         void                 exec();
         void                 quit();

         static application&  instance();

         abstract_plugin* find_plugin(const string& name)const;
         abstract_plugin& get_plugin(const string& name)const;

         // 注册插件
         template<typename Plugin>
         auto& register_plugin() {
            // 查找是否已经注册过该插件
            auto existing = find_plugin<Plugin>();
            // 如果已经注册过，则直接返回
            if(existing)
               return *existing;
            // 构建新的插件对象
            auto plug = new Plugin();
            // 将插件加入已注册的map列表中
            plugins[plug->name()].reset(plug);
            // 注册插件自身的依赖(插件自身的依赖其实也是插件)
            plug->register_dependencies();
            return *plug;
         }

         template<typename Plugin>
         Plugin* find_plugin()const {
            string name = boost::core::demangle(typeid(Plugin).name());
            return dynamic_cast<Plugin*>(find_plugin(name));
         }

         template<typename Plugin>
         Plugin& get_plugin()const {
            auto ptr = find_plugin<Plugin>();
            return *ptr;
         }

         /**
          * Fetch a reference to the method declared by the passed in type.  This will construct the method
          * on first access.  This allows loose and deferred binding between plugins
          *
          * @tparam MethodDecl - @ref appbase::method_decl
          * @return reference to the method described by the declaration
          */
         template<typename MethodDecl>
         auto get_method() -> std::enable_if_t<is_method_decl<MethodDecl>::value, typename MethodDecl::method_type&>
         {
            using method_type = typename MethodDecl::method_type;
            auto key = std::type_index(typeid(MethodDecl));
            auto itr = methods.find(key);
            if(itr != methods.end()) {
               return *method_type::get_method(itr->second);
            } else {
               methods.emplace(std::make_pair(key, method_type::make_unique()));
               return  *method_type::get_method(methods.at(key));
            }
         }

         /**
          * Fetch a reference to the channel declared by the passed in type.  This will construct the channel
          * on first access.  This allows loose and deferred binding between plugins
          *
          * @tparam ChannelDecl - @ref appbase::channel_decl
          * @return reference to the channel described by the declaration
          */
         template<typename ChannelDecl>
         auto get_channel() -> std::enable_if_t<is_channel_decl<ChannelDecl>::value, typename ChannelDecl::channel_type&>
         {
            using channel_type = typename ChannelDecl::channel_type;
            auto key = std::type_index(typeid(ChannelDecl));
            auto itr = channels.find(key);
            if(itr != channels.end()) {
               return *channel_type::get_channel(itr->second);
            } else {
               channels.emplace(std::make_pair(key, channel_type::make_unique(io_serv)));
               return  *channel_type::get_channel(channels.at(key));
            }
         }

         boost::asio::io_service& get_io_service() { return *io_serv; }
      protected:
         template<typename Impl>
         friend class plugin;

         bool initialize_impl(int argc, char** argv, vector<abstract_plugin*> autostart_plugins);

         /** these notifications get called from the plugin when their state changes so that
          * the application can call shutdown in the reverse order.
          */
         ///@{
         void plugin_initialized(abstract_plugin& plug){ initialized_plugins.push_back(&plug); }
         void plugin_started(abstract_plugin& plug){ running_plugins.push_back(&plug); }
         ///@}

      private:
         // 单例模式，将构造函数设为私有使得外部无法直接通过构造函数实例化
         application(); ///< private because application is a singleton that should be accessed via instance()
         // 所有已注册的插件
         map<string, std::unique_ptr<abstract_plugin>> plugins; ///< all registered plugins
         // 已初始化的插件列表
         vector<abstract_plugin*>                  initialized_plugins; ///< stored in the order they were started running
         // 正在运行的插件列表
         vector<abstract_plugin*>                  running_plugins; ///< stored in the order they were started running

         map<std::type_index, erased_method_ptr>   methods;
         map<std::type_index, erased_channel_ptr>  channels;

         std::shared_ptr<boost::asio::io_service>  io_serv;

         void set_program_options();
         void write_default_config(const bfs::path& cfg_file);
         void print_default_config(std::ostream& os);
         std::unique_ptr<class application_impl> my;

   };

   application& app();

   /*
      plugin是纯虚类abstract_plugin的一个派生类，并且是一个模板类，他封装了一个插件中较为通用的操作，
      从这个类开始构建一个插件比直接从abstract_plugin纯虚类派生可以减少很多重复的工作，当然如果你需要
      一些和plugin提供的基本操作不同的操作的话，也可以直接从abstract_plugin构建插件。
   */
   template<typename Impl>
   class plugin : public abstract_plugin {
      public:
         // 根据Impl类来生成一个字符串的名字并赋值给_name作为插件的名字
         plugin():_name(boost::core::demangle(typeid(Impl).name())){}
         virtual ~plugin(){}
         
         // 获取插件的状态 registered/initialized/started/stopped
         virtual state get_state()const override         { return _state; }

         // 获取插件名称，名称也被application类用来作为区分不同插件的依据
         virtual const std::string& name()const override { return _name; }
         
         // 注册插件的依赖，插件必须有一个函数plugin_requires函数定义自己的依赖，
         // 依赖其实也是插件，我们查看示例代码的插件式发现插件定义中并没有明显的
         // 调用plugin_requires函数，实际上是通过APPBASE_PLUGIN_REQUIRES宏
         // 来定义的。
         virtual void register_dependencies() {
            static_cast<Impl*>(this)->plugin_requires([&](auto& plug){});
         }
         
         // 插件初始化
         virtual void initialize(const variables_map& options) override {
            // _state默认是registered，初始化之后变成initialized
            if(_state == registered) {
               _state = initialized;
               // 调用本插件依赖项的initialize
               static_cast<Impl*>(this)->plugin_requires([&](auto& plug){ plug.initialize(options); });
               // 自定义的插件必须实现plugin_initialize方法，这个方法给予插件的构建者一个机会
               // 做一些本插件特有的初始化工作
               static_cast<Impl*>(this)->plugin_initialize(options);
               //ilog( "initializing plugin ${name}", ("name",name()) );
               // 通知app插件初始化完毕，app中会更新对应插件的状态
               app().plugin_initialized(*this);
            }
            assert(_state == initialized); /// if initial state was not registered, final state cannot be initiaized
         }

         // 启动插件，app的startup会首先调用插件的startup
         virtual void startup() override {
            // _state此时是initialized，startup之后变成started
            if(_state == initialized) {
               _state = started;
               // 调用本插件依赖项的startup
               static_cast<Impl*>(this)->plugin_requires([&](auto& plug){ plug.startup(); });
               // 自定义的插件必须实现plugin_startup方法，这个方法给予插件的构建者一个机会
               // 做一些本插件特有的启动工作               
               static_cast<Impl*>(this)->plugin_startup();
               // 通知app插件启动完毕，app中会更新对应插件的状态
               app().plugin_started(*this);
            }
            assert(_state == started); // if initial state was not initialized, final state cannot be started
         }

         // 关闭插件
         virtual void shutdown() override {
            if(_state == started) {
               _state = stopped;
               //ilog( "shutting down plugin ${name}", ("name",name()) );
               // 自定义的插件必须实现plugin_shutdown方法，这个方法给予插件的构建者一个机会
               // 做一些本插件特有的关闭工作                   
               static_cast<Impl*>(this)->plugin_shutdown();
            }
         }

      protected:
         plugin(const string& name) : _name(name){}

      private:
         state _state = abstract_plugin::registered;
         std::string _name;
   };
}
