/*
    Part of Grbl_ESP32
    2021 -  Stefan de Bruijn

    Grbl_ESP32 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Grbl_ESP32 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Grbl_ESP32.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <vector>

#include "HandlerBase.h"

namespace Configuration {
    template <typename BaseType>
    class GenericFactory {
        static GenericFactory& instance() {
            static GenericFactory instance_;
            return instance_;
        }

        GenericFactory() = default;

        GenericFactory(const GenericFactory&) = delete;
        GenericFactory& operator=(const GenericFactory&) = delete;

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

        std::vector<BuilderBase*> builders_;

        inline static void registerBuilder(BuilderBase* builder) { instance().builders_.push_back(builder); }

    public:
        template <typename DerivedType>
        class InstanceBuilder : public BuilderBase {
        public:
            InstanceBuilder(const char* name) : BuilderBase(name) { instance().registerBuilder(this); }

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
                        auto product = it->create();
                        inst.push_back(product);
                        handler.enterFactory(it->name(), *product);

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
