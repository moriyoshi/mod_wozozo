/* 
 * Copyright (c) 2016 Moriyoshi Koizumi
 *           (c) 2012 Université catholique de Louvain
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_main.h"
#include "http_protocol.h"
#include "http_request.h"
#include "util_script.h"
#include "http_connection.h"

#include "apr_strings.h"

#include <mozart.hh>
#include <boostenv.hh>

#include <boost/optional.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/scope_exit.hpp>

#include <stdio.h>

#ifdef MOZART_WINDOWS
#  include <windows.h>
#endif

// Path literal, using the system native encoding
#ifdef MOZART_WINDOWS
#  define PATH_LIT(s) boost::filesystem::path(L##s)
#else
#  define PATH_LIT(s) boost::filesystem::path(s)
#endif

namespace fs = boost::filesystem;

typedef struct mozart_vm_args_t {
    char const*  oz_home;
    char const*  init_functor_path;
    char const*  base_functor_path;
    char const*  oz_search_path;
    char const*  oz_search_load;
    size_t min_memory;
    size_t max_memory;

    mozart_vm_args_t();
} mozart_vm_args_t;

inline mozart_vm_args_t::mozart_vm_args_t()
    : oz_home(0),
      init_functor_path(0),
      base_functor_path(0),
      oz_search_path(0),
      oz_search_load(0),
      min_memory(32 * mozart::MegaBytes),
#if defined(_WIN32) && !defined(_WIN64)
      max_memory(512 * mozart::MegaBytes)
#else
      max_memory(768 * mozart::MegaBytes)
#endif
{
}

typedef struct wozozo_server_conf_t {
    mozart_vm_args_t vm_args;
    std::shared_ptr<mozart::boostenv::BoostEnvironment> env;
    std::unique_ptr<boost::thread> io_thread;
    std::unique_ptr<boost::asio::io_service::work> work;

    wozozo_server_conf_t() {}
} wozozo_server_conf_t;


static mozart::boostenv::BoostEnvironment* init_mozart_vm_env(server_rec *s, mozart_vm_args_t const& args);
static int check_mozart_vm_args(server_rec *s, mozart_vm_args_t const& args);

extern "C" {
extern module AP_MODULE_DECLARE_DATA wozozo_module;

static const char* register_oz_root(cmd_parms* cmd, void* dummy, const char* path)
{
    server_rec* s = cmd->server;
    wozozo_server_conf_t* conf = static_cast<wozozo_server_conf_t*>(ap_get_module_config(s->module_config, &wozozo_module));
    conf->vm_args.oz_home = path;
    return NULL;
}

static const char* register_oz_search_path(cmd_parms* cmd, void* dummy, const char* path)
{
    server_rec* s = cmd->server;
    wozozo_server_conf_t* conf = static_cast<wozozo_server_conf_t*>(ap_get_module_config(s->module_config, &wozozo_module));
    conf->vm_args.oz_search_path = path;
    return NULL;
}

static const char* register_oz_search_load(cmd_parms* cmd, void* dummy, const char* path)
{
    server_rec* s = cmd->server;
    wozozo_server_conf_t* conf = static_cast<wozozo_server_conf_t*>(ap_get_module_config(s->module_config, &wozozo_module));
    conf->vm_args.oz_search_load = path;
    return NULL;
}

static const char* register_init_functor_path(cmd_parms* cmd, void* dummy, const char* path)
{
    server_rec* s = cmd->server;
    wozozo_server_conf_t* conf = static_cast<wozozo_server_conf_t*>(ap_get_module_config(s->module_config, &wozozo_module));
    conf->vm_args.init_functor_path = path;
    return NULL;
}

static const char* register_base_functor_path(cmd_parms* cmd, void* dummy, const char* path)
{
    server_rec* s = cmd->server;
    wozozo_server_conf_t* conf = static_cast<wozozo_server_conf_t*>(ap_get_module_config(s->module_config, &wozozo_module));
    conf->vm_args.base_functor_path = path;
    return NULL;
}


static const char* register_max_memory(cmd_parms* cmd, void* dummy, const char* value)
{
    server_rec* s = cmd->server;
    wozozo_server_conf_t* conf = static_cast<wozozo_server_conf_t*>(ap_get_module_config(s->module_config, &wozozo_module));
    apr_off_t _value;
    if (apr_strtoff(&_value, value, NULL, 10)) {
        return "Invalid value for OzMaxMemory.";
    }
    conf->vm_args.max_memory = static_cast<size_t>(_value);
    return NULL;
}


static const char* register_min_memory(cmd_parms* cmd, void* dummy, const char* value)
{
    server_rec* s = cmd->server;
    wozozo_server_conf_t* conf = static_cast<wozozo_server_conf_t*>(ap_get_module_config(s->module_config, &wozozo_module));
    apr_off_t _value;
    if (apr_strtoff(&_value, value, NULL, 10)) {
        return "Invalid value for OzMinMemory.";
    }
    conf->vm_args.min_memory = static_cast<size_t>(_value);
    return NULL;
}


static command_rec wozozo_commands[] = {

    AP_INIT_TAKE1("OzHome", reinterpret_cast<char const*(*)()>(register_oz_root), NULL, RSRC_CONF,
                  "Specify Mozart installation directory."),

    AP_INIT_TAKE1("OzSearchPath", reinterpret_cast<char const*(*)()>(register_oz_search_path), NULL, RSRC_CONF,
                  "Specify the directory for Mozart's search path."),

    AP_INIT_TAKE1("OzSearchLoad", reinterpret_cast<char const*(*)()>(register_oz_search_load), NULL, RSRC_CONF,
                  "Specify the directory for Mozart's search load path."),

    AP_INIT_TAKE1("OzInitFunctorPath", reinterpret_cast<char const*(*)()>(register_init_functor_path), NULL, RSRC_CONF,
                  "Specify the path to Init.ozf functor."),

    AP_INIT_TAKE1("OzBaseFunctorPath", reinterpret_cast<char const*(*)()>(register_base_functor_path), NULL, RSRC_CONF,
                  "Specify the path to Base.ozf functor."),

    AP_INIT_TAKE1("OzMaxMemory", reinterpret_cast<char const*(*)()>(register_max_memory), NULL, RSRC_CONF,
                  "Specify the maximum heap size."),

    AP_INIT_TAKE1("OzMinMemory", reinterpret_cast<char const*(*)()>(register_min_memory), NULL, RSRC_CONF,
                  "Specify the minimum heap size."),
    {NULL}
};


static void *wozozo_create_server_config(apr_pool_t *p, server_rec *s)
{
    wozozo_server_conf_t* conf = static_cast<wozozo_server_conf_t*>(apr_pcalloc(p, sizeof(wozozo_server_conf_t)));
    new (conf) wozozo_server_conf_t(); 
    return conf;
}


static int wozozo_post_config(apr_pool_t *pconf, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s);
static void wozozo_child_init(apr_pool_t *pool, server_rec *s);
static int wozozo_handler(request_rec *r);

static void wozozo_register_hooks(apr_pool_t *p)
{
    ap_hook_post_config(wozozo_post_config, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_child_init(wozozo_child_init, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(wozozo_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

AP_DECLARE_MODULE(wozozo) =
{
    STANDARD20_MODULE_STUFF,
    NULL,                        /* per-directory config creator */
    NULL,                        /* dir config merger */
    wozozo_create_server_config, /* server config creator */
    NULL,                        /* server config merger */
    wozozo_commands,             /* command table */
    wozozo_register_hooks        /* set up other request processing hooks */
};

} // extern "C"

static int wozozo_post_config(apr_pool_t *pconf, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s)
{
    wozozo_server_conf_t* conf = static_cast<wozozo_server_conf_t*>(ap_get_module_config(s->module_config, &wozozo_module));

    if (conf->vm_args.oz_home) {
        conf->vm_args.oz_home = ap_resolve_env(pconf, conf->vm_args.oz_home);
    }

    if (conf->vm_args.init_functor_path) {
        conf->vm_args.init_functor_path = ap_resolve_env(pconf, conf->vm_args.init_functor_path);
    }

    if (conf->vm_args.base_functor_path) {
        conf->vm_args.base_functor_path = ap_resolve_env(pconf, conf->vm_args.base_functor_path);
    }

    if (conf->vm_args.oz_search_path) {
        conf->vm_args.oz_search_path = ap_resolve_env(pconf, conf->vm_args.oz_search_path);
    }

    if (conf->vm_args.oz_search_load) {
        conf->vm_args.oz_search_load = ap_resolve_env(pconf, conf->vm_args.oz_search_load);
    }

    return check_mozart_vm_args(s, conf->vm_args);
}

static apr_status_t wozozo_child_cleanup(void *_conf)
{
    wozozo_server_conf_t* conf = static_cast<wozozo_server_conf_t*>(_conf); 
    if (conf->work) {
        conf->work.reset();
    }
}

static void wozozo_child_init(apr_pool_t *pool, server_rec *s)
{
    wozozo_server_conf_t* conf = static_cast<wozozo_server_conf_t*>(ap_get_module_config(s->module_config, &wozozo_module));
    std::shared_ptr<mozart::boostenv::BoostEnvironment> env(init_mozart_vm_env(s, conf->vm_args));
    conf->env = env;
    conf->work = std::move(std::unique_ptr<boost::asio::io_service::work>(new boost::asio::io_service::work(env->io_service)));
    conf->io_thread = std::move(std::unique_ptr<boost::thread>(new boost::thread([env, conf, s]() {
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, s, "Mozart IO thread started");
        env->runIO();
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, s, "Mozart IO thread ended");
    })));
    apr_pool_cleanup_register(pool, conf, wozozo_child_cleanup, wozozo_child_cleanup);
}

struct condvar {
    boost::condition_variable cond;
    boost::mutex mtx;

    inline void wait() {
        boost::unique_lock<boost::mutex> lock(mtx);
        cond.wait(lock);
    }

    inline void notify() {
        cond.notify_one(); 
    }
};

struct _string: public std::string {
    request_rec *r;
    condvar* c;
    inline _string(request_rec* r, condvar* c, char const* s): std::string(s), r(r), c(c) {}
};

static int wozozo_handler(request_rec *r)
{
    int i;
    wozozo_server_conf_t* server_conf = static_cast<wozozo_server_conf_t*>(ap_get_module_config(r->server->module_config, &wozozo_module));
    if (strcmp(r->handler, "wozozo-handler")) {
        return DECLINED;
    }

    if (!r->filename) {
        return DECLINED;
    }

    mozart::VirtualMachineOptions vmOptions;
    vmOptions.minimalHeapSize = server_conf->vm_args.min_memory;
    vmOptions.maximalHeapSize = server_conf->vm_args.max_memory;

    if (r->header_only) return OK;

    std::unique_ptr<condvar> c(new condvar);

    server_conf->env->addVM(1, std::move(std::unique_ptr<std::string>(new _string(r, c.get(), r->filename))), true, vmOptions);
    c->wait();
    return OK;
}


static int check_mozart_vm_args(server_rec *s, mozart_vm_args_t const& args)
{
    if (!args.oz_home) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, "OzHome must be specified");
        return DECLINED;
    }

    if (!(args.min_memory >= 1 * mozart::MegaBytes && args.min_memory < args.max_memory)) {
        ap_log_error(APLOG_MARK, APLOG_ERR, APR_EINVAL, s,
                     "Invalid heap sizes given: min_memory=%zd max_memory=%zd",
                     args.min_memory, args.max_memory);
        return DECLINED;
    }

    return OK;
}

namespace std {

template<typename T>
struct iterator_traits<reference_wrapper<T>> {
    typedef ptrdiff_t difference_type;
    typedef T value_type;
    typedef const T* pointer;
    typedef const T& reference;
    typedef random_access_iterator_tag iterator_category;
};

}

static bool ozVSGet(mozart::VM vm, mozart::RichNode vs, char*& p)
{
    using namespace mozart::patternmatching;

    size_t partCount;
    mozart::StaticArray<mozart::StableNode> parts;

    mozart::atom_t atomValue;
    mozart::nativeint intValue;
    double floatValue;

    if (matchesVariadicSharp(vm, vs, partCount, parts)) {
        for (size_t i = 0; i < partCount; ++i) {
            if (!ozVSGet(vm, parts[i], std::ref(p)))
                return false;
        }
        return true;
    } else if (matches(vm, vs, capture(atomValue))) {
        if (atomValue != vm->coreatoms.nil) {
            std::copy_n(atomValue.contents(), atomValue.length(), std::ref(p));
            p += atomValue.length();
        }
        return true;
    } else if (matchesCons(vm, vs, wildcard(), wildcard())) {
        mozart::internal::ozListForEachNoRaise(vm, vs,
            [vm, &p] (char32_t c) {
                char buffer[4];
                mozart::nativeint length = mozart::toUTF(c, buffer);
                std::copy_n(buffer, length, std::ref(p));
            }
        );
        return true;
    } else if (vs.is<mozart::String>()) {
        auto& value = vs.as<mozart::String>().value();
        std::copy(value.begin(), value.end(), std::ref(p));
        return true;
    } else if (matches(vm, vs, capture(intValue))) {
        mozart::internal::IntToStrBuffer buffer;
        auto length = mozart::internal::intToStrBuffer(buffer, intValue);
        std::copy_n(buffer, length, std::ref(p));
        return true;
    } else if (vs.is<mozart::BigInt>()) {
        std::string buffer = vs.as<mozart::BigInt>().str();
        std::copy(buffer.begin(), buffer.end(), std::ref(p));
        return true;
    } else if (matches(vm, vs, capture(floatValue))) {
        mozart::internal::FloatToStrBuffer buffer;
        auto length = mozart::internal::floatToStrBuffer(buffer, floatValue);
        std::copy_n(buffer, length, std::ref(p));
        return true;
    } else {
        return false;
    } 
}


static char* ozVSGet(mozart::VM vm, mozart::RichNode n, apr_pool_t *pool, boost::optional<size_t&> len = boost::optional<size_t&>())
{
    mozart::nativeint l(mozart::ozVSLengthForBufferNoRaise(vm, n));
    if (l < 0) {
        return NULL;
    }
    char* buf = static_cast<char*>(apr_pcalloc(pool, static_cast<size_t>(l) + 1));
    {
        char* p(buf);
        if (!ozVSGet(vm, n, p)) {
            mozart::raiseTypeError(vm, "VirtualString", n);
            return NULL;
        }
        *p = 0;
        if (len) {
            *len = p - buf;
        }
    }
    return buf;
}


class ApacheModule: public mozart::BuiltinModule {

public:
    class SetContentType: public mozart::builtins::Builtin<SetContentType> {
    protected:
        request_rec* r;
    public:
        SetContentType(request_rec* r): Builtin("setContentType"), r(r) {}

        static void call(mozart::VM vm, mozart::builtins::In str) {
            auto builtinNode = vm->findBuiltin("Apache", "setContentType");
            auto builtin = reinterpret_cast<SetContentType*>(mozart::RichNode(builtinNode).as<mozart::BuiltinProcedure>().value());
            char* strVal(ozVSGet(vm, str, builtin->r->pool));
            ap_set_content_type(builtin->r, strVal);
        }
    };

    class Rputs: public mozart::builtins::Builtin<Rputs> {
    protected:
        request_rec* r;
    public:
        Rputs(request_rec* r): Builtin("rputs"), r(r) {}

        static void call(mozart::VM vm, mozart::builtins::In str) {
            auto builtinNode = vm->findBuiltin("Apache", "rputs");
            auto builtin = reinterpret_cast<Rputs*>(mozart::RichNode(builtinNode).as<mozart::BuiltinProcedure>().value());
            size_t strLen;
            char* strVal(ozVSGet(vm, str, builtin->r->pool, strLen));
            ap_rwrite(strVal, strLen, builtin->r);
        }

    };

    class Rflush: public mozart::builtins::Builtin<Rflush> {
    protected:
        request_rec* r;
    public:
        Rflush(request_rec* r): Builtin("rflush"), r(r) {}

        static void call(mozart::VM vm) {
            auto builtinNode = vm->findBuiltin("Apache", "rflush");
            auto builtin = reinterpret_cast<Rflush*>(mozart::RichNode(builtinNode).as<mozart::BuiltinProcedure>().value());
            ap_rflush(builtin->r);
        }

    };

protected:
    SetContentType instanceSetContentType;
    Rputs instanceRputs;
    Rflush instanceRflush;

public:
    inline ApacheModule(mozart::VM vm, request_rec* r)
        : BuiltinModule(vm, "Apache"),
          instanceSetContentType(r),
          instanceRputs(r),
          instanceRflush(r) {
        instanceRputs.setModuleName("Apache");
        mozart::UnstableField fields[3];
        fields[0].feature = mozart::build(vm, "setContentType");
        fields[0].value = mozart::build(vm, instanceSetContentType);
        fields[1].feature = mozart::build(vm, "rputs");
        fields[1].value = mozart::build(vm, instanceRputs);
        fields[2].feature = mozart::build(vm, "rflush");
        fields[2].value = mozart::build(vm, instanceRflush);
        auto label = build(vm, "export");
        auto module = buildRecordDynamic(vm, label, sizeof(fields) / sizeof(*fields), fields);
        initModule(vm, std::move(module));
    }
};

static mozart::boostenv::BoostEnvironment* init_mozart_vm_env(server_rec *s, mozart_vm_args_t const& args)
{
    if (OK != check_mozart_vm_args(s, args)) {
        return 0;
    }

    fs::path ozHome(args.oz_home), initFunctorPath, baseFunctorPath;
    if (args.init_functor_path) {
        initFunctorPath = fs::path(args.init_functor_path);
    }
    if (args.base_functor_path) {
        baseFunctorPath = fs::path(args.base_functor_path);
    }

    if (initFunctorPath.empty()) {
        initFunctorPath = ozHome / PATH_LIT("share") / PATH_LIT("mozart") / PATH_LIT("Init.ozf");
    }

    // SET UP THE VM AND RUN
    return new mozart::boostenv::BoostEnvironment([=] (mozart::VM vm, std::unique_ptr<std::string> app, bool isURL) {
        mozart::boostenv::BoostVM& boostVM = mozart::boostenv::BoostVM::forVM(vm);
        _string* _s(reinterpret_cast<_string*>(app.get()));
        request_rec* r = _s->r;
        condvar* c = _s->c;
        vm->registerBuiltinModule(std::make_shared<ApacheModule>(vm, r));
        BOOST_SCOPE_EXIT((c)) {
            c->notify();
        } BOOST_SCOPE_EXIT_END;

        // Set some properties
        {
            auto& properties = vm->getPropertyRegistry();

            mozart::atom_t ozHomeAtom = vm->getAtom(ozHome.string());
            properties.registerValueProp(vm, "oz.home", ozHomeAtom);
            properties.registerValueProp(vm, "oz.emulator.home", ozHomeAtom);
            properties.registerValueProp(vm, "oz.configure.home", ozHomeAtom);
            if (args.oz_search_path) {
                properties.registerValueProp(vm, "oz.search.path", vm->getAtom(std::string(args.oz_search_path)));
            }
            if (args.oz_search_load) {
                properties.registerValueProp(vm, "oz.search.load", vm->getAtom(std::string(args.oz_search_load)));
            }

            if (isURL) {
                auto decodedURL = mozart::toUTF<char>(mozart::makeLString(app->c_str()));
                auto appURL = vm->getAtom(decodedURL.length, decodedURL.string);
                properties.registerValueProp(vm, "application.url", appURL);
            } else {
                // Set application.url for compatibility
                auto fakeURL = vm->getAtom("<VM.new functor>");
                properties.registerValueProp(vm, "application.url", fakeURL);

                std::istringstream input(*app);
                mozart::UnstableNode functor = mozart::unpickle(vm, input);
                properties.registerValueProp(vm, "application.functor", functor);
            }
            app.reset(); // Release the memory hold by the potentially big app string

            properties.registerValueProp(vm, "application.gui", false);
        }

        mozart::boostenv::BoostEnvironment& boostEnv = mozart::boostenv::BoostEnvironment::forVM(vm);

        // Some protected nodes
        mozart::ProtectedNode baseEnv, initFunctor;

        // Register apache builtins

        // Load the Base environment if required
        if (args.base_functor_path) {
            baseEnv = vm->protect(mozart::OptVar::build(vm));

            mozart::UnstableNode baseValue;

            if (!boostEnv.bootLoader(vm, baseFunctorPath.string(), baseValue)) {
                ap_log_error(APLOG_MARK, APLOG_ERR, APR_EINVAL, s,
                             "could not load Base functor at %s",
                             baseFunctorPath.string().c_str());
                return false;
            }

            // Create the thread that loads the Base environment
            if (mozart::Callable(baseValue).isProcedure(vm)) {
                mozart::ozcalls::asyncOzCall(vm, baseValue, *baseEnv);
            } else {
                // Assume it is a functor that does not import anything
                mozart::UnstableNode applyAtom = build(vm, "apply");
                mozart::UnstableNode applyProc = mozart::Dottable(baseValue).dot(vm, applyAtom);
                mozart::UnstableNode importParam = build(vm, "import");
                mozart::ozcalls::asyncOzCall(vm, applyProc, importParam, *baseEnv);
            }

            boostVM.run();
        }

        // Load the Init functor
        {
            initFunctor = vm->protect(mozart::OptVar::build(vm));

            mozart::UnstableNode initValue;

            if (!boostEnv.bootLoader(vm, initFunctorPath.string(), initValue)) {
                ap_log_error(APLOG_MARK, APLOG_ERR, APR_EINVAL, s,
                             "could not load Init functor at %s",
                             initFunctorPath.string().c_str());
                return false;
            }

            // Create the thread that loads the Init functor
            if (mozart::Callable(initValue).isProcedure(vm)) {
                if (!args.base_functor_path) {
                    ap_log_error(APLOG_MARK, APLOG_ERR, APR_EINVAL, s,
                                 "Init.ozf is a procedure, but I have no Base to give to it");
                    return false;
                }

                mozart::ozcalls::asyncOzCall(vm, initValue, *baseEnv, *initFunctor);
                boostVM.run();
            } else {
                // Assume it is already the Init functor
                mozart::DataflowVariable(*initFunctor).bind(vm, initValue);
            }
        }

        // Apply the Init functor
        {
            auto ApplyAtom = mozart::build(vm, "apply");
            auto ApplyProc = mozart::Dottable(*initFunctor).dot(vm, ApplyAtom);

            auto BootModule = vm->findBuiltinModule("Boot");
            auto ImportRecord = mozart::buildRecord(
                vm, buildArity(vm, "import", "Boot"),
                BootModule);

            mozart::ozcalls::asyncOzCall(vm, ApplyProc, ImportRecord, mozart::OptVar::build(vm));

            baseEnv.reset();
            initFunctor.reset();

            boostVM.run();
        }

        return true;
    });
}
