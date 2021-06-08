#include "runtime.h"

#include <cassert>
#include <optional>
#include <sstream>

using namespace std;

namespace runtime {

    namespace {

        template<typename T, typename Cmp>
        std::optional<bool> TryCompare(const ObjectHolder& lhs, const ObjectHolder& rhs, Cmp&& cmp) {
            const auto* l = lhs.TryAs<T>();
            const auto* r = rhs.TryAs<T>();
            if (l && r) {
                return cmp(l->GetValue(), r->GetValue());
            }
            return nullopt;
        }

        const string EQ_METHOD = "__eq__"s;
        const string LT_METHOD = "__lt__"s;
        const string STR_METHOD = "__str__"s;
    }  // namespace

    ObjectHolder::ObjectHolder(std::shared_ptr<Object> data)
            : data_(std::move(data)) {
    }

    void ObjectHolder::AssertIsValid() const {
        assert(data_ != nullptr);
    }

    ObjectHolder ObjectHolder::Share(Object& object) {
        // Возвращаем невладеющий shared_ptr (его deleter ничего не делает)
        return ObjectHolder(std::shared_ptr<Object>(&object, [](auto* /*p*/) { /* do nothing */ }));
    }

    ObjectHolder ObjectHolder::None() {
        return ObjectHolder();
    }

    Object& ObjectHolder::operator*() const {
        AssertIsValid();
        return *Get();
    }

    Object* ObjectHolder::operator->() const {
        AssertIsValid();
        return Get();
    }

    Object* ObjectHolder::Get() const {
        return data_.get();
    }

    ObjectHolder::operator bool() const {
        return Get() != nullptr;
    }

    bool IsTrue(const ObjectHolder& object) {
        if (!object) {
            return false;
        }
        const auto* number = object.TryAs<runtime::Number>();
        if (number != nullptr && number->GetValue() != 0) {
            return true;
        }
        const auto* str = object.TryAs<runtime::String>();
        if (str != nullptr && !str->GetValue().empty()) {
            return true;
        }
        const auto* p = object.TryAs<runtime::Bool>();
        if (p != nullptr && p->GetValue()) {
            return true;
        }
        return false;
    }

    void ClassInstance::Print(std::ostream& os, Context& context) {
        if (HasMethod(STR_METHOD, 0)) {
            Call(STR_METHOD, {}, context)->Print(os, context);
        } else {
            os << this;
        }
    }

    bool ClassInstance::HasMethod(const std::string& method, size_t argument_count) const {
        const auto* m = class_.GetMethod(method);
        return m != nullptr && m->formal_params.size() == argument_count;
    }

    Closure& ClassInstance::Fields() {
        return fields_;
    }

    const Closure& ClassInstance::Fields() const {
        return fields_;
    }

    ClassInstance::ClassInstance(const Class& cls)
            : class_(cls) {
    }

    ObjectHolder ClassInstance::Call(const std::string& method,
                                     const std::vector<ObjectHolder>& actual_args, Context& context) {
        const auto* m = class_.GetMethod(method);
        if (m == nullptr) {
            std::ostringstream msg;
            msg << "Class "sv << class_.GetName() << " doesn't have method "sv << method;
            throw std::runtime_error(msg.str());
        }
        if (m->formal_params.size() != actual_args.size()) {
            std::ostringstream msg;
            msg << "Method "sv << class_.GetName() << "::" << method << " expects "sv
                << m->formal_params.size() << " arguments, but "sv << actual_args.size() << " given"sv;
            throw std::runtime_error(msg.str());
        }
        Closure closure = {{"self"s, ObjectHolder::Share(*this)}};
        for (size_t i = 0; i < actual_args.size(); ++i) {
            closure[m->formal_params[i]] = actual_args[i];
        }
        return m->body->Execute(closure, context);
    }

    Class::Class(std::string name, std::vector<Method> methods, const Class* parent)
            : name_(std::move(name)), parent_(parent) {
        for (auto& m : methods) {
            if (vtable_.find(m.name) != vtable_.end()) {
                std::ostringstream ss;
                ss << "Class "sv << name_ << " has duplicate methods with name "sv << m.name;
                throw runtime_error(ss.str());
            }
            vtable_[m.name] = std::move(m);
        }
    }

    const Method* Class::GetMethod(const std::string& name) const {
        if (auto it = vtable_.find(name); it != vtable_.end()) {
            return &it->second;
        }
        if (parent_ != nullptr) {
            return parent_->GetMethod(name);
        }
        return nullptr;
    }

    const std::string& Class::GetName() const {
        return name_;
    }

    void Class::Print(ostream& os, [[maybe_unused]] Context& context) {
        os << "Class "sv << name_;
    }

    void Bool::Print(std::ostream& os, [[maybe_unused]] Context& context) {
        os << (GetValue() ? "True"sv : "False"sv);
    }

    bool Equal(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        auto result = TryCompare<runtime::Number>(lhs, rhs, std::equal_to<>());
        if (!result) {
            result = TryCompare<runtime::String>(lhs, rhs, std::equal_to<>());
        }
        if (!result) {
            result = TryCompare<runtime::Bool>(lhs, rhs, std::equal_to<>());
        }
        if (result) {
            return *result;
        }
        auto* p = lhs.TryAs<runtime::ClassInstance>();
        if (p != nullptr && p->HasMethod(EQ_METHOD, 1)) {
            return IsTrue(p->Call(EQ_METHOD, {rhs}, context));
        }
        if (!lhs && !rhs) {
            return true;
        }

        throw std::runtime_error("Cannot compare objects for equality"s);
    }

    bool Less(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        auto result = TryCompare<runtime::Number>(lhs, rhs, std::less<>());
        if (!result) {
            result = TryCompare<runtime::String>(lhs, rhs, std::less<>());
        }
        if (!result) {
            result = TryCompare<runtime::Bool>(lhs, rhs, std::less<>());
        }
        if (result) {
            return *result;
        }
        auto* p = lhs.TryAs<runtime::ClassInstance>();
        if (p != nullptr && p->HasMethod(LT_METHOD, 1)) {
            return IsTrue(p->Call(LT_METHOD, {rhs}, context));
        }

        throw std::runtime_error("Cannot compare objects for less"s);
    }

    bool NotEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        return !Equal(lhs, rhs, context);
    }

    bool Greater(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        return !LessOrEqual(lhs, rhs, context);
    }

    bool LessOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        return Less(lhs, rhs, context) || Equal(lhs, rhs, context);
    }

    bool GreaterOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        return !Less(lhs, rhs, context);
    }

}  // namespace runtime