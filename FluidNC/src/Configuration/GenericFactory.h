// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <vector>

#include "HandlerBase.h"

namespace Configuration {
    template <typename BaseType>
    class BuilderBase {
        const char* name_;

    public:
        BuilderBase(const char* name) : name_(name) {}

        BuilderBase(const BuilderBase& o) = delete;
        BuilderBase& operator=(const BuilderBase& o) = delete;

        virtual BaseType* create() const = 0;
        const char*       name() const { return name_; }

        virtual ~BuilderBase() = default;
    };

    template <typename BaseType>
    class GenericFactory {
        static GenericFactory& instance() {
            static GenericFactory instance_;
            return instance_;
        }

        GenericFactory() = default;

        GenericFactory(const GenericFactory&) = delete;
        GenericFactory& operator=(const GenericFactory&) = delete;

        std::vector<BuilderBase<BaseType>*> builders_;

        inline static void registerBuilder(BuilderBase<BaseType>* builder) { instance().builders_.push_back(builder); }

    public:
        template <typename DerivedType>
        class InstanceBuilder : public BuilderBase<BaseType> {
        public:
            InstanceBuilder(const char* name) : BuilderBase<BaseType>(name) { instance().registerBuilder(this); }

            BaseType* create() const override { return new DerivedType(); }
        };

        static void factory(Configuration::HandlerBase& handler, BaseType*& inst) {
            if (inst == nullptr) {
                for (auto it : instance().builders_) {
                    if (handler.matchesUninitialized(it->name())) {
                        inst = it->create();
                        handler.enterFactory(it->name(), *inst);

                        return;
                    }
                }
            } else {
                handler.enterSection(inst->name(), inst);
            }
        }
        static void factory(Configuration::HandlerBase& handler, std::vector<BaseType*>& inst) {
            if (handler.handlerType() == HandlerType::Parser) {
                for (auto it : instance().builders_) {
                    if (handler.matchesUninitialized(it->name())) {
                        std::vector<Configurable*> instlocal;
                        handler.enterFactoryList(it->name(), it, instlocal);
                        for (const auto& value : instlocal) {
                            inst.push_back((BaseType*)value);
                        }

                        return;
                    }
                }
            } else {
                for (auto it : inst) {
                    handler.enterSection(it->name(), it);
                }
            }
        }
    };
}
