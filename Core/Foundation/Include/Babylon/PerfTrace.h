#pragma once
#include <memory>
#include <napi/env.h>


namespace Babylon
{
    namespace PerfTrace
    {
        enum class Level
        {
            None,
            Mark,
            Log,
        };

        class Handle
        {
        public:
            ~Handle();
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;
            Handle(Handle&&) noexcept;
            Handle& operator=(Handle&&) noexcept;

            static Napi::Value ToNapi(Napi::Env env, Handle handle);
            static Handle FromNapi(Napi::Value napiValue);

        private:
            friend Handle Trace(const char* name);
            Handle(const char* name);
            struct Impl;
            std::unique_ptr<Impl> m_impl;
        };

        void SetLevel(Level level);
        Handle Trace(const char* name);
    }
}
