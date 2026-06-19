#pragma once

#include "AppRuntime.h"

#include <napi/napi.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace Babylon::Internal
{
    // Wraps an unhandled-promise rejection reason as a Napi::Error: an Error-like object passes
    // through (preserving message/stack/cause); any other value is stringified so the host handler
    // always receives a Napi::Error. Lives here rather than in shared AppRuntime.cpp because the
    // napi_value -> Napi::Value bridge is unavailable on the JSI Node-API shim, and only the engines
    // that support rejection tracking (V8, JavaScriptCore) include this header.
    inline Napi::Error ToError(Napi::Env env, napi_value reason)
    {
        const Napi::Value value{env, reason};
        return value.IsObject()
            ? Napi::Error{env, reason}
            : Napi::Error::New(env, value.ToString().Utf8Value());
    }

    // Engine-agnostic bookkeeping for engines that report an unhandled rejection immediately and a
    // later handler-added event separately (V8): collect candidates as promises reject without a
    // handler, drop them when a handler is attached, and report the survivors to the host handler at
    // the end of the current turn -- so a rejection handled synchronously within the same turn is
    // never reported. Engines whose host hook already fires only for still-unhandled rejections at
    // the microtask checkpoint (JavaScriptCore) report directly and do not need this.
    //
    // CandidateT is engine-specific and must provide:
    //   void Report(AppRuntime& runtime, Napi::Env env) const;
    //       // convert its stored reason to a Napi::Error (via ToError) and call
    //       // runtime.OnUnhandledPromiseRejection
    template<typename CandidateT>
    class PromiseRejectionTracker
    {
    public:
        explicit PromiseRejectionTracker(AppRuntime& runtime)
            : m_runtime{runtime}
        {
        }

        void Add(CandidateT candidate)
        {
            m_candidates.push_back(std::move(candidate));

            if (!m_flushScheduled)
            {
                m_flushScheduled = true;
                m_runtime.Dispatch([this](Napi::Env env) { Flush(env); });
            }
        }

        template<typename PredicateT>
        void Remove(PredicateT predicate)
        {
            m_candidates.erase(
                std::remove_if(m_candidates.begin(), m_candidates.end(), std::move(predicate)),
                m_candidates.end());
        }

    private:
        void Flush(Napi::Env env)
        {
            m_flushScheduled = false;

            // Move the candidates out before reporting: a host handler could synchronously reject
            // another promise, re-entering Add() and mutating m_candidates mid-iteration.
            const std::vector<CandidateT> candidates = std::move(m_candidates);
            m_candidates.clear();

            for (const CandidateT& candidate : candidates)
            {
                candidate.Report(m_runtime, env);
            }
        }

        AppRuntime& m_runtime;
        std::vector<CandidateT> m_candidates;
        bool m_flushScheduled{false};
    };
}
