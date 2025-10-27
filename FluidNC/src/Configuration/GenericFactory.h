// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <vector>
#include "Logging.h"

#include "HandlerBase.h"
#include <algorithm>

namespace Configuration {
    template <typename BaseType>
    class GenericFactory {
        static GenericFactory& instance() {
            static GenericFactory instance_;
            return instance_;
        }

        GenericFactory() = default;

        GenericFactory(const GenericFactory&)            = delete;
        GenericFactory& operator=(const GenericFactory&) = delete;

        class BuilderBase {
            const char* name_;

        public:
            BuilderBase(const char* name) : name_(name) {}

            BuilderBase(const BuilderBase& o)            = delete;
            BuilderBase& operator=(const BuilderBase& o) = delete;

            virtual BaseType* create(const char* name) const = 0;
            const char*       name() const { return name_; }

            virtual ~BuilderBase() = default;
        };

        std::vector<BuilderBase*> builders_;
        std::vector<BaseType*>    objects_;

        inline static void registerBuilder(BuilderBase* builder) { instance().builders_.push_back(builder); }

    public:
        static std::vector<BaseType*>& objects() { return instance().objects_; }

        static void add(BaseType* object) { objects().push_back(object); }

        template <typename DerivedType>
        class InstanceBuilder : public BuilderBase {
        public:
            explicit InstanceBuilder(const char* name, bool autocreate) : BuilderBase(name) {
                instance().registerBuilder(this);
                add(create(name));
            }
            explicit InstanceBuilder(const char* name) : BuilderBase(name) { instance().registerBuilder(this); }

            BaseType* create(const char* name) const override { return new DerivedType(name); }
        };

        template <typename DerivedType, typename DependencyType>
        class DependentInstanceBuilder : public BuilderBase {
        public:
            explicit DependentInstanceBuilder(const char* name, bool autocreate = false) : BuilderBase(name) {
                instance().registerBuilder(this);
                if (autocreate) {
                    auto& objects = instance().objects_;
                    auto  object  = create(name);
                    objects.push_back(object);
                }
            }

            DerivedType* create(const char* name) const override {
                auto dependency = new DependencyType();
                return new DerivedType(name, dependency);
            }
        };

        // This factory() method is used when there can be only one
        // instance of the type, at a given level of the tree, as with
        // a kinematics system or a motor driver.  The variable that
        // points to the instance must be created externally and
        // passed as an argument.
        static void factory(Configuration::HandlerBase& handler, BaseType*& inst) {
            if (inst == nullptr) {
                auto& builders = instance().builders_;
                auto  it       = std::find_if(
                    builders.begin(), builders.end(), [&](auto& builder) { return handler.matchesUninitialized(builder->name()); });
                if (it != builders.end()) {
                    auto name = (*it)->name();
                    inst      = (*it)->create(name);
                    handler.enterFactory(name, *inst);
                }
            } else {
                handler.enterSection(inst->name(), inst);
            }
        }

        // This factory() method is used when there can be multiple instances,
        // as with spindles and modules.  A vector in the GenericFactory<BaseType>
        // singleton holds the derived type instances, so there is no need to
        // declare and define it separately.  That vector can be accessed with
        // Configuration::GenericFactory<BaseType>::objects() - which is
        // often abbreviated to, e.g. ModuleFactory::objects() via a
        // "using" declaration.
        static void factory(Configuration::HandlerBase& handler) {
            auto& objects = instance().objects_;
            if (handler.handlerType() == HandlerType::Parser) {
                auto& builders = instance().builders_;
                auto  it       = std::find_if(
                    builders.begin(), builders.end(), [&](auto& builder) { return handler.matchesUninitialized(builder->name()); });
                if (it != builders.end()) {
                    auto name = (*it)->name();
                    // If the config file contains multiple factory sections with the same name,
                    // for example two laser: sections or oled: sections, create a new node
                    // for each repetition.  FluidNC can thus support multiple lasers with
                    // different tool numbers and output pins, multiple OLED displays, etc
                    auto object = (*it)->create(name);
                    add(object);
                    handler.enterFactory(name, *object);
                }
            } else {
                for (auto it : objects) {
                    handler.enterSection(it->name(), it);
                }
            }
        }
    };
}
