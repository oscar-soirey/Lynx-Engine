#pragma once

#include "Common.h"
#include "../gameplay/Object.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#define LYNX_MODULE_FUNCTION extern "C" __declspec(dllexport)

// Utilise la factory locale créée dans LYNX_LINK_MODULE
#define LYNX_MODULE_REGISTER(__class__) \
    __factory__.emplace( \
        #__class__, \
        []() -> lynx::Object* { return new __class__(); } \
    )

#define LYNX_LINK_MODULE(__module_content__) \
    static void LynxFillFactory(lynx::Factory& __factory__) \
    { \
        __module_content__; \
    } \
    \
    extern "C" __declspec(dllexport) void FactoryRegisterClasses() \
    { \
        lynx::Factory f; \
        LynxFillFactory(f); \
        lynx::Engine::Get()->GetFactory().InsertFactory(f); \
    } \
    \
    extern "C" __declspec(dllexport) void FactoryUnregisterClasses() \
    { \
        lynx::Factory f; \
        LynxFillFactory(f); \
        lynx::Engine::Get()->GetFactory().RemoveFactory(f); \
    }

namespace lynx
{
    using ObjectConstructor = std::function<Object*()>;
    using Factory = std::unordered_map<std::string, ObjectConstructor>;

    class LYNX_API FactoryObject
    {
    public:
        FactoryObject() = default;
        ~FactoryObject() = default;

        void RegisterObject(const char* name, ObjectConstructor constr)
        {
            auto [it, inserted] = factory_.emplace(
                name,
                std::move(constr)
            );

            if (!inserted)
            {
                printf(
                    "RegisterObject error: object %s already exists\n",
                    name
                );
            }
        }

        std::optional<ObjectConstructor> GetObjectConstr(
            const char* name
        ) const
        {
            auto it = factory_.find(name);

            if (it == factory_.end())
                return std::nullopt;

            return it->second;
        }

        void InsertFactory(const Factory& factory)
        {
            for (const auto& [name, constructor] : factory)
            {
                RegisterObject(name.c_str(), constructor);
            }
        }

        // Retire de la factory toutes les classes listees dans `factory`.
        // Appele au unload d'un module pour ne pas garder de constructeurs
        // qui pointent dans une dll dechargee.
        void RemoveFactory(const Factory& factory)
        {
            for (const auto& [name, constructor] : factory)
            {
                // A game class with the name of an engine class was refused
                // by RegisterObject : the engine one stays.
                if (builtin_.count(name))
                    continue;
                factory_.erase(name);
            }
        }

        // Classes of the engine itself (EngineActors.h) : kept when a game
        // module is unloaded.
        void MarkBuiltin(const char* name)
        {
            builtin_.emplace(name);
        }

        const Factory* GetInternalFactory() const
        {
            return &factory_;
        }

    private:
        Factory factory_;
        std::unordered_set<std::string> builtin_;
    };
}