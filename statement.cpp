#include "statement.h"

#include <iostream>
#include <sstream>
#include <numeric>

using namespace std;

namespace ast {

    using runtime::Closure;
    using runtime::Context;
    using runtime::ObjectHolder;

    namespace {
        const string ADD_METHOD = "__add__"s;
        const string INIT_METHOD = "__init__"s;
    }  // namespace

    ObjectHolder Assignment::Execute(Closure& closure, Context& context) {
        const auto& new_value = rv_->Execute(closure, context);
        closure[var_] = new_value;
        return new_value;
    }

    Assignment::Assignment(std::string var, std::unique_ptr<Statement> rv) : var_(std::move(var)), rv_(std::move(rv)) {
    }

    VariableValue::VariableValue(const std::string& var_name) {
        dotted_ids_.push_back(std::move(var_name));
    }

    VariableValue::VariableValue(std::vector<std::string> dotted_ids) : dotted_ids_(std::move(dotted_ids)) {
    }

    ObjectHolder VariableValue::Execute(Closure& closure, [[maybe_unused]] Context& context) {
        const auto& var_name = dotted_ids_.front();
        if (!closure.count(var_name)) {
            throw std::runtime_error("unknown variable " + var_name);
        }

        return accumulate(
                next(begin(dotted_ids_)), end(dotted_ids_),
                closure.at(var_name),
                [](ObjectHolder parent,
                   const string& name) { return parent.TryAs<runtime::ClassInstance>()->Fields().at(name); }
        );
    }

    unique_ptr<Print> Print::Variable(const std::string& name) {
        return std::make_unique<Print>(
                std::make_unique<VariableValue>(std::move(name))
        );
    }

    Print::Print(unique_ptr<Statement> argument) {
        args_.push_back(std::move(argument));
    }

    Print::Print(vector<unique_ptr<Statement>> args) : args_(std::move(args)) {
    }

    ObjectHolder Print::Execute(Closure& closure, Context& context) {
        auto& output = context.GetOutputStream();
        if (output) {
            bool first_ = true;
            for (auto& statement : args_) {
                if (first_) {
                    ObjectHolder object = statement->Execute(closure, context);
                    if (object) {
                        object->Print(output, context);
                    } else {
                        output << "None";
                    }
                    first_ = false;
                } else {
                    output << ' ';
                    ObjectHolder object = statement->Execute(closure, context);
                    if (object) {
                        object->Print(output, context);
                    } else {
                        output << "None";
                    }
                }
            }
            output << '\n';

        }
        return ObjectHolder::None();
    }

    MethodCall::MethodCall(std::unique_ptr<Statement> object, std::string method,
                           std::vector<std::unique_ptr<Statement>> args) : object_(std::move(object)),
                                                                           method_(std::move(method)),
                                                                           args_(std::move(args)) {
    }

    ObjectHolder MethodCall::Execute(Closure& closure, Context& context) {
        std::vector<ObjectHolder> actual_args;
        for (const auto& arg : args_) {
            actual_args.push_back(arg->Execute(closure, context));
        }

        auto* class_instance = object_->Execute(closure, context).TryAs<runtime::ClassInstance>();

        if (class_instance) {
            if (class_instance->HasMethod(method_, actual_args.size())) {
                return class_instance->Call(method_, actual_args, context);
            }
        }

        throw std::runtime_error("Bad Method call: " + method_);
    }

    ObjectHolder Stringify::Execute(Closure& closure, Context& context) {
        std::ostringstream out;
        ObjectHolder str = argument_->Execute(closure, context);
        if (str.Get() == NULL) {
            return ObjectHolder::Own(runtime::String("None"));
        } else {
            str->Print(out, context);
            return ObjectHolder::Own(runtime::String(out.str()));
        }
    }

    std::optional<ObjectHolder>
    executeBinaryOperationOnClasses(runtime::ClassInstance* lhs, runtime::ObjectHolder& rhs,
                                    const std::string& operation, Context& context) {
        if (lhs->HasMethod(operation, 1)) {
            return lhs->Call(operation, {rhs}, context);
        }
        return nullopt;
    }

    ObjectHolder Add::Execute(Closure& closure, Context& context) {
        ObjectHolder lhs_h = lhs_->Execute(closure, context);
        ObjectHolder rhs_h = rhs_->Execute(closure, context);

        if ((lhs_h.TryAs<runtime::Number>()) && (rhs_h.TryAs<runtime::Number>())) {
            return ObjectHolder::Own(runtime::Number(
                    lhs_h.TryAs<runtime::Number>()->GetValue() +
                    rhs_h.TryAs<runtime::Number>()->GetValue()
            ));
        }

        if ((lhs_h.TryAs<runtime::String>()) && (rhs_h.TryAs<runtime::String>())) {
            return runtime::ObjectHolder::Own(runtime::String(
                    lhs_h.TryAs<runtime::String>()->GetValue() +
                    rhs_h.TryAs<runtime::String>()->GetValue()
            ));
        }

        if (lhs_h.TryAs<runtime::ClassInstance>()) {
            const auto result = executeBinaryOperationOnClasses(
                    lhs_h.TryAs<runtime::ClassInstance>(),
                    rhs_h,
                    "__add__",
                    context
            );
            if (result) {
                return *result;
            }
        }

        throw runtime_error("Bad Addition!");
    }


    ObjectHolder Sub::Execute(Closure& closure, Context& context) {
        ObjectHolder lhs_h = lhs_->Execute(closure, context);
        ObjectHolder rhs_h = rhs_->Execute(closure, context);

        if ((lhs_h.TryAs<runtime::Number>()) && (rhs_h.TryAs<runtime::Number>())) {
            return runtime::ObjectHolder::Own(runtime::Number(
                    lhs_h.TryAs<runtime::Number>()->GetValue() -
                    rhs_h.TryAs<runtime::Number>()->GetValue()
            ));
        }

        if (lhs_h.TryAs<runtime::ClassInstance>()) {
            const auto result = executeBinaryOperationOnClasses(
                    lhs_h.TryAs<runtime::ClassInstance>(),
                    rhs_h,
                    "__sub__",
                    context
            );
            if (result) {
                return *result;
            }
        }

        throw runtime_error("Bad Subtraction!");
    }

    ObjectHolder Mult::Execute(Closure& closure, Context& context) {
        ObjectHolder lhs_h = lhs_->Execute(closure, context);
        ObjectHolder rhs_h = rhs_->Execute(closure, context);

        if ((lhs_h.TryAs<runtime::Number>()) && (rhs_h.TryAs<runtime::Number>())) {
            return runtime::ObjectHolder::Own(runtime::Number(
                    lhs_h.TryAs<runtime::Number>()->GetValue() *
                    rhs_h.TryAs<runtime::Number>()->GetValue()
            ));
        }

        if (lhs_h.TryAs<runtime::ClassInstance>()) {
            const auto result = executeBinaryOperationOnClasses(
                    lhs_h.TryAs<runtime::ClassInstance>(),
                    rhs_h,
                    "__mul__",
                    context
            );
            if (result) {
                return *result;
            }
        }

        throw runtime_error("Bad Multiplication!");

    }

    ObjectHolder Div::Execute(Closure& closure, Context& context) {
        ObjectHolder lhs_h = lhs_->Execute(closure, context);
        ObjectHolder rhs_h = rhs_->Execute(closure, context);

        if ((lhs_h.TryAs<runtime::Number>()) && (rhs_h.TryAs<runtime::Number>())) {
            if (rhs_h.TryAs<runtime::Number>()->GetValue() == 0) {
                throw runtime_error("Zero Division!");
            }

            return runtime::ObjectHolder::Own(runtime::Number(
                    lhs_h.TryAs<runtime::Number>()->GetValue() /
                    rhs_h.TryAs<runtime::Number>()->GetValue()
            ));
        }

        if (lhs_h.TryAs<runtime::ClassInstance>()) {
            const auto result = executeBinaryOperationOnClasses(
                    lhs_h.TryAs<runtime::ClassInstance>(),
                    rhs_h,
                    "__div__",
                    context
            );
            if (result) {
                return *result;
            }
        }

        throw runtime_error("Bad Division!");
    }

    ObjectHolder Compound::Execute(Closure& closure, Context& context) {
        for (auto& statement : statements_) {
            if (dynamic_cast<Return*>(statement.get()))
                return statement->Execute(closure, context);

            if (
                    dynamic_cast<IfElse*>(statement.get()) ||
                    dynamic_cast<MethodCall*>(statement.get())
                    ) {
                ObjectHolder result = statement->Execute(closure, context);
                if (result) {
                    return result;
                }
            } else {
                statement->Execute(closure, context);
            }
        }

        return runtime::ObjectHolder::None();
    }

    ObjectHolder Return::Execute(Closure& closure, Context& context) {
        throw Exception(statement_->Execute(closure, context));
    }

    ClassDefinition::ClassDefinition(ObjectHolder cls) : cls_(std::move(cls)),
                                                         class_name_(cls_.TryAs<runtime::Class>()->GetName()) {
    }

    ObjectHolder ClassDefinition::Execute(Closure& closure, [[maybe_unused]] Context& context) {
        closure[class_name_] = cls_;
        return runtime::ObjectHolder::None();
    }

    FieldAssignment::FieldAssignment(VariableValue object, std::string field_name,
                                     std::unique_ptr<Statement> rv) : object_(object),
                                                                      field_name_(std::move(field_name)),
                                                                      rv_(std::move(rv)) {

    }

    ObjectHolder FieldAssignment::Execute(Closure& closure, Context& context) {
        ObjectHolder& field = object_.Execute(closure, context).TryAs<runtime::ClassInstance>()->Fields()[field_name_];
        field = rv_->Execute(closure, context);
        return field;
    }

    IfElse::IfElse(std::unique_ptr<Statement> condition, std::unique_ptr<Statement> if_body,
                   std::unique_ptr<Statement> else_body) : condition_(std::move(condition)),
                                                           if_body_(std::move(if_body)),
                                                           else_body_(std::move(else_body)) {
    }

    ObjectHolder IfElse::Execute(Closure& closure, Context& context) {
        ObjectHolder condition = condition_->Execute(closure, context);

        if (condition) {
            if (IsTrue(condition)) {
                if (if_body_)
                    return if_body_->Execute(closure, context);
            } else {
                if (else_body_)
                    return else_body_->Execute(closure, context);
            }
        } else {
            if (else_body_)
                return else_body_->Execute(closure, context);
        }
        return ObjectHolder::None();
    }

    ObjectHolder Or::Execute(Closure& closure, Context& context) {
        ObjectHolder lhs_h = lhs_->Execute(closure, context);
        ObjectHolder rhs_h = rhs_->Execute(closure, context);
        if (!lhs_h) {
            lhs_h = ObjectHolder::Own(runtime::Bool(false));
        }
        if (!rhs_h) {
            rhs_h = ObjectHolder::Own(runtime::Bool(false));
        }
        return ObjectHolder::Own(
                runtime::Bool(
                        IsTrue(lhs_h) ||
                        IsTrue(rhs_h)
                )
        );
    }

    ObjectHolder And::Execute(Closure& closure, Context& context) {
        ObjectHolder lhs_h = lhs_->Execute(closure, context);
        ObjectHolder rhs_h = rhs_->Execute(closure, context);
        if (!lhs_h) {
            lhs_h = ObjectHolder::Own(runtime::Bool(false));
        }
        if (!rhs_h) {
            rhs_h = ObjectHolder::Own(runtime::Bool(false));
        }
        return ObjectHolder::Own(
                runtime::Bool(
                        IsTrue(lhs_h) &&
                        IsTrue(rhs_h)
                )
        );
    }

    ObjectHolder Not::Execute(Closure& closure, Context& context) {
        ObjectHolder argument_h = argument_->Execute(closure, context);
        if (!argument_h) {
            argument_h = ObjectHolder::Own(runtime::Bool(false));
        }
        return ObjectHolder::Own(
                runtime::Bool(
                        !IsTrue(argument_h)
                )
        );
    }

    Comparison::Comparison(Comparator cmp, unique_ptr<Statement> lhs, unique_ptr<Statement> rhs)
            : BinaryOperation(std::move(lhs), std::move(rhs)), cmp_(cmp) {
    }

    ObjectHolder Comparison::Execute(Closure& closure, Context& context) {
        return ObjectHolder::Own(
                runtime::Bool(
                        cmp_(
                                lhs_->Execute(closure, context),
                                rhs_->Execute(closure, context),
                                context
                        )
                )
        );
    }

    NewInstance::NewInstance(const runtime::Class& class_, std::vector<std::unique_ptr<Statement>> args) : class__(
            class_), args_(std::move(args)) {
    }

    NewInstance::NewInstance(const runtime::Class& class_) : class__(class_) {
    }

    ObjectHolder NewInstance::Execute(Closure& closure, Context& context) {
        auto* new_instance = new runtime::ClassInstance(class__);
        if (new_instance->HasMethod("__init__", args_.size())) {
            std::vector<ObjectHolder> actual_args;
            for (const auto& statement : args_) {
                actual_args.push_back(statement->Execute(closure, context));
            }
            new_instance->Call("__init__", actual_args, context);
        }

        return ObjectHolder::Share(*new_instance);
    }

    MethodBody::MethodBody(std::unique_ptr<Statement>&& body) {
        body_.AddStatement(std::move(body));
    }

    ObjectHolder MethodBody::Execute(Closure& closure, Context& context) {
        try {
            return body_.Execute(closure, context);
        } catch (Exception& ex) {
            return ex.getValue();
        }
    }

    Exception::Exception(const ObjectHolder& value) : value_(value) {}

    const ObjectHolder& Exception::getValue() const {
        return value_;
    }
}  // namespace ast