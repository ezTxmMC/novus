#include "interpreter.hpp"
#include "../util/file.hpp"
#include <cmath>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>

void Interpreter::interpret(const unique_ptr<Node> &ast) {
  if (!ast)
    return;

  if (auto program = dynamic_cast<const ProgramNode *>(ast.get())) {
    visitProgram(program);
  }
}

void Interpreter::visitProgram(const ProgramNode *node) {
  if (!node)
    return;

  for (const auto &method : node->methods) {
    if (auto methodNode = dynamic_cast<const MethodNode *>(method.get())) {
      methods[methodNode->name].push_back(const_cast<MethodNode *>(methodNode));
    } else if (auto classNode = dynamic_cast<const ClassNode *>(method.get())) {
      classes[classNode->name] = const_cast<ClassNode *>(classNode);
    } else if (auto enumNode = dynamic_cast<const EnumNode *>(method.get())) {
      enums[enumNode->name] = const_cast<EnumNode *>(enumNode);
    } else if (auto ifaceNode =
                   dynamic_cast<const InterfaceNode *>(method.get())) {
      interfaces[ifaceNode->name] = const_cast<InterfaceNode *>(ifaceNode);
    } else if (auto annoNode =
                   dynamic_cast<const AnnotationDefNode *>(method.get())) {
      annotationDefs[annoNode->name] = const_cast<AnnotationDefNode *>(annoNode);
    }
  }

  // Interface implementation checks
  for (auto &entry : classes) {
    ClassNode *cls = entry.second;
    if (cls->isAbstract) {
      continue; // subclasses are checked via the abstract-method rules
    }
    auto ifaceIt = interfaces.find(cls->baseName);
    if (ifaceIt == interfaces.end()) {
      continue;
    }
    for (const auto &required : ifaceIt->second->methodNames) {
      bool found = false;
      int guard = 0;
      for (ClassNode *c = cls; c && guard < 32; guard++) {
        for (const auto &m : c->methods) {
          if (m->name == required && !m->isAbstract) {
            found = true;
            break;
          }
        }
        if (found) {
          break;
        }
        auto baseIt = classes.find(c->baseName);
        c = (c->baseName.empty() || baseIt == classes.end()) ? nullptr
                                                             : baseIt->second;
      }
      if (!found) {
        throw runtime_error("Class '" + cls->name + "' does not implement '" +
                            required + "()' from interface " + cls->baseName);
      }
    }
  }

  // Top-level constants become globals
  for (const auto &method : node->methods) {
    if (auto varNode = dynamic_cast<const VariableNode *>(method.get())) {
      Value v;
      v.type = varNode->type;
      if (varNode->value) {
        v = evalExpression(varNode->value);
      }
      globals[varNode->name] = v;
    }
  }

  // Build enum constant values before running main
  for (auto &entry : enums) {
    EnumNode *en = entry.second;
    for (auto &constant : en->constants) {
      vector<Value> args;
      for (const auto &arg : constant.args) {
        args.push_back(evalExpression(arg));
      }
      Value v = constructInstance(
          en->name, en->fields,
          en->constructor ? en->constructor.get() : nullptr, args);
      v.data = constant.name; // identity for display and ==
      enumValues[en->name][constant.name] = v;
    }
  }

  auto mainIt = methods.find("main");
  if (mainIt != methods.end()) {
    MethodNode *chosen = nullptr;
    if (!programArgs.empty()) {
      for (auto *candidate : mainIt->second) {
        if (candidate->parameters.size() == 1) {
          chosen = candidate;
          break;
        }
      }
    }
    if (!chosen) {
      for (auto *candidate : mainIt->second) {
        if (candidate->parameters.empty()) {
          chosen = candidate;
          break;
        }
      }
    }
    if (!chosen) {
      chosen = mainIt->second.front();
    }

    if (chosen->parameters.empty()) {
      visitMethod(chosen);
    } else {
      Value argsArray(Type("array", true), "");
      for (const auto &arg : programArgs) {
        argsArray.elements.push_back(Value(Type("string"), arg));
      }
      vector<Value> callArgs;
      callArgs.push_back(argsArray);
      callMethod("main", callArgs);
    }
  }
}

void Interpreter::visitMethod(const MethodNode *node) {
  if (!node)
    return;

  hasReturned = false;
  hasBroken = false;
  hasContinued = false;
  scopes.clear();
  executeBlock(node->body);
  hasReturned = false;
}

void Interpreter::executeBlock(const vector<unique_ptr<Node>> &statements) {
  pushScope();
  for (const auto &stmt : statements) {
    if (!stmt)
      continue;

    executeStatement(stmt.get());

    if (hasReturned || hasBroken || hasContinued)
      break;
  }
  popScope();
}

void Interpreter::executeStatement(const Node *stmt) {
  if (!stmt)
    return;

  if (auto varNode = dynamic_cast<const VariableNode *>(stmt)) {
    visitVariable(varNode);
    return;
  }

  if (auto assignNode = dynamic_cast<const AssignmentNode *>(stmt)) {
    visitAssignment(assignNode);
    return;
  }

  if (auto indexAssignNode = dynamic_cast<const IndexAssignmentNode *>(stmt)) {
    visitIndexAssignment(indexAssignNode);
    return;
  }

  if (auto propAssignNode =
          dynamic_cast<const PropertyAssignmentNode *>(stmt)) {
    visitPropertyAssignment(propAssignNode);
    return;
  }

  if (auto ifNode = dynamic_cast<const IfNode *>(stmt)) {
    visitIf(ifNode);
    return;
  }

  if (auto whileNode = dynamic_cast<const WhileNode *>(stmt)) {
    visitWhile(whileNode);
    return;
  }

  if (auto forNode = dynamic_cast<const ForInNode *>(stmt)) {
    visitForIn(forNode);
    return;
  }

  if (dynamic_cast<const BreakNode *>(stmt)) {
    hasBroken = true;
    return;
  }

  if (dynamic_cast<const ContinueNode *>(stmt)) {
    hasContinued = true;
    return;
  }

  if (auto returnNode = dynamic_cast<const ReturnNode *>(stmt)) {
    returnValue = visitReturn(returnNode);
    hasReturned = true;
    return;
  }

  // println statements and bare expression statements
  visitPrintNode(stmt);
}

void Interpreter::visitVariable(const VariableNode *node) {
  if (!node)
    return;

  Value value;
  value.type = node->type;

  if (node->value) {
    Value evalResult = evalExpression(node->value);

    if (node->type.name == "inferred" || node->type.name == "unknown") {
      value = evalResult; // adopt the initializer's type
    } else {
      value = coerceType(evalResult, node->type);
    }
  } else {
    value.data = "";
  }

  defineVariable(node->name, value);
}

void Interpreter::visitAssignment(const AssignmentNode *node) {
  if (!node)
    return;

  // Evaluate first - nested calls may swap the scope stack.
  Value value = evalExpression(node->value);

  Value *target = findVariable(node->name);
  if (!target) {
    throw runtime_error("Unknown variable '" + node->name + "'");
  }

  if (target->type.name != "unknown" && target->type.name != "inferred") {
    value = coerceType(value, target->type);
  }

  *target = value;
}

void Interpreter::visitIf(const IfNode *node) {
  if (!node)
    return;

  if (isTruthy(evalExpression(node->condition))) {
    executeBlock(node->thenBranch);
  } else {
    executeBlock(node->elseBranch);
  }
}

void Interpreter::visitWhile(const WhileNode *node) {
  if (!node)
    return;

  while (!hasReturned && isTruthy(evalExpression(node->condition))) {
    executeBlock(node->body);
    hasContinued = false;
    if (hasBroken) {
      hasBroken = false;
      break;
    }
  }
}

void Interpreter::visitForIn(const ForInNode *node) {
  if (!node)
    return;

  Value iterable = evalExpression(node->iterable);

  vector<Value> items;
  if (iterable.type.isArray) {
    items = iterable.elements;
  } else if (iterable.type.name == "map") {
    for (const auto &entry : iterable.entries) {
      items.push_back(Value(Type("string"), entry.first));
    }
  } else if (iterable.type.name == "string") {
    for (char c : iterable.data) {
      items.push_back(Value(Type("string"), string(1, c)));
    }
  } else {
    throw runtime_error("Cannot iterate over this value");
  }

  for (const auto &item : items) {
    if (hasReturned)
      break;
    pushScope();
    defineVariable(node->varName, item);
    executeBlock(node->body);
    popScope();
    hasContinued = false;
    if (hasBroken) {
      hasBroken = false;
      break;
    }
  }
}

void Interpreter::visitPrintNode(const Node *node) {
  if (!node)
    return;

  Value val = evalNode(node);
  if ((val.type.name == "unknown" || val.type.name == "void") &&
      val.data.empty() && !val.type.isArray) {
    return; // void result (e.g. a bare call statement) - nothing to print
  }
  std::cout << display(val) << std::endl;
}

void Interpreter::visitIdentifier(const IdentifierNode *node) {
  if (!node)
    return;

  Value val = getValue(node->name);
  std::cout << display(val) << std::endl;
}

Value Interpreter::visitMethodCall(const MethodCallNode *node) {
  if (!node)
    return Value();

  vector<Value> args;
  for (const auto &arg : node->arguments) {
    args.push_back(evalExpression(arg));
  }

  return callMethod(node->methodName, args);
}

Value Interpreter::callMemberOn(Value &target, const string &methodName,
                                vector<Value> &args) {

    // Class or enum instance: field accessors and methods with 'this'
    ClassNode *cls = nullptr;
    EnumNode *en = nullptr;
    {
      auto cIt = classes.find(target.type.name);
      if (cIt != classes.end())
        cls = cIt->second;
      auto eIt = enums.find(target.type.name);
      if (eIt != enums.end())
        en = eIt->second;
    }

    if (cls || en) {
      vector<FieldNode> allFields = cls ? collectFields(cls) : en->fields;

      for (const auto &field : allFields) {
        if (field.name == methodName) {
          if (args.empty()) {
            auto entry = target.entries.find(field.name);
            return entry != target.entries.end() ? entry->second : Value();
          }
          target.entries[field.name] = coerceType(args[0], field.type);
          return Value();
        }
      }

      MethodNode *member = nullptr;
      if (cls) {
        int guard = 0;
        for (ClassNode *c = cls; c && guard < 32; guard++) {
          for (const auto &m : c->methods) {
            if (m->name == methodName) {
              member = m.get();
              break;
            }
          }
          if (member)
            break;
          auto baseIt = classes.find(c->baseName);
          c = (c->baseName.empty() || baseIt == classes.end())
                  ? nullptr
                  : baseIt->second;
        }
      } else {
        for (const auto &m : en->methods) {
          if (m->name == methodName) {
            member = m.get();
            break;
          }
        }
      }

      if (member) {
        warnIfDeprecated(member);
        MethodNode *m = member;
        {
          vector<map<string, Value>> savedScopes = std::move(scopes);
          bool savedHasReturned = hasReturned;
          bool savedHasBroken = hasBroken;
          bool savedHasContinued = hasContinued;
          Value savedReturnValue = returnValue;

          scopes.clear();
          pushScope();
          for (size_t i = 0; i < m->parameters.size() && i < args.size();
               i++) {
            defineVariable(m->parameters[i].name,
                           coerceType(args[i], m->parameters[i].type));
          }
          defineVariable("this", target);

          hasReturned = false;
          hasBroken = false;
          hasContinued = false;
          returnValue = Value(m->returnType, "");

          executeBlock(m->body);

          Value result = coerceType(returnValue, m->returnType);
          Value updatedThis = target;
          if (Value *self = findVariable("this")) {
            updatedThis = *self;
          }

          hasReturned = savedHasReturned;
          hasBroken = savedHasBroken;
          hasContinued = savedHasContinued;
          returnValue = savedReturnValue;
          scopes = std::move(savedScopes);

          target = updatedThis;
          return result;
        }
      }

      throw runtime_error("Unknown member '" + methodName + "' on " +
                          target.type.name);
    }

    if (methodName == "length") {
      if (target.type.isArray) {
        return Value(Type("integer"), to_string(target.elements.size()));
      }
      if (target.type.name == "map") {
        return Value(Type("integer"), to_string(target.entries.size()));
      }
      return Value(Type("integer"), to_string(target.data.size()));
    }

    if (methodName == "has") {
      if (target.type.name != "map") {
        throw runtime_error("'" + target.type.name + "' is not a map");
      }
      string key = args.size() > 0 ? args[0].data : "";
      return boolValue(target.entries.find(key) != target.entries.end());
    }

    if (methodName == "keys") {
      if (target.type.name != "map") {
        throw runtime_error("'" + target.type.name + "' is not a map");
      }
      Value keys(Type("array", true), "");
      for (const auto &entry : target.entries) {
        keys.elements.push_back(Value(Type("string"), entry.first));
      }
      return keys;
    }

    if (methodName == "remove") {
      if (target.type.name != "map") {
        throw runtime_error("'" + target.type.name + "' is not a map");
      }
      string key = args.size() > 0 ? args[0].data : "";
      target.entries.erase(key);
      return Value();
    }

    if (methodName == "append") {
      if (!target.type.isArray) {
        throw runtime_error("'" + target.type.name + "' is not an array");
      }
      for (auto &arg : args) {
        target.elements.push_back(std::move(arg));
      }
      return Value();
    }

    if (methodName == "substring") {
      const string &s = target.data;
      int start = args.size() > 0 ? stoi(args[0].data) : 0;
      int end = args.size() > 1 ? stoi(args[1].data) : (int)s.size();
      if (start < 0)
        start = 0;
      if (end > (int)s.size())
        end = (int)s.size();
      if (start > end)
        start = end;
      return Value(Type("string"), s.substr(start, end - start));
    }

    if (methodName == "charAt") {
      const string &s = target.data;
      int index = args.size() > 0 ? stoi(args[0].data) : 0;
      if (index < 0 || index >= (int)s.size()) {
        throw runtime_error("charAt index " + to_string(index) +
                            " out of bounds (length " + to_string(s.size()) +
                            ")");
      }
      return Value(Type("string"), string(1, s[index]));
    }

    throw runtime_error("Unknown method '" + methodName + "()' on '" +
                        target.type.name + "'");
}

void Interpreter::visitPropertyAccess(const PropertyAccessNode *node) {
  if (!node)
    return;

  std::cout << node->objectName << "." << node->propertyName << std::endl;
}

void Interpreter::visitPropertyAssignment(const PropertyAssignmentNode *node) {
  if (!node)
    return;

  // Evaluate first - nested calls may swap the scope stack.
  Value value = evalExpression(node->value);

  Value *obj = findVariable(node->objectName);
  if (!obj) {
    throw runtime_error("Unknown variable '" + node->objectName + "'");
  }

  obj->entries[node->propertyName] = value;
}

vector<FieldNode> Interpreter::collectFields(ClassNode *cls) {
  vector<ClassNode *> chain;
  int guard = 0;
  for (ClassNode *c = cls; c && guard < 32; guard++) {
    chain.push_back(c);
    auto baseIt = classes.find(c->baseName);
    c = (c->baseName.empty() || baseIt == classes.end()) ? nullptr
                                                         : baseIt->second;
  }

  vector<FieldNode> out;
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    for (const auto &field : (*it)->fields) {
      out.push_back(field);
    }
  }
  return out;
}

MethodNode *Interpreter::findConstructorFor(ClassNode *cls) {
  int guard = 0;
  for (ClassNode *c = cls; c && guard < 32; guard++) {
    if (c->constructor) {
      return c->constructor.get();
    }
    auto baseIt = classes.find(c->baseName);
    c = (c->baseName.empty() || baseIt == classes.end()) ? nullptr
                                                         : baseIt->second;
  }
  return nullptr;
}

Value Interpreter::constructObject(ClassNode *cls, const vector<Value> &args) {
  if (cls->isAbstract) {
    throw runtime_error("Cannot instantiate abstract class '" + cls->name +
                        "'");
  }

  // Every abstract method in the chain must resolve to a concrete override
  int guard = 0;
  for (ClassNode *c = cls; c && guard < 32; guard++) {
    for (const auto &m : c->methods) {
      if (!m->isAbstract) {
        continue;
      }
      MethodNode *resolved = nullptr;
      int innerGuard = 0;
      for (ClassNode *r = cls; r && innerGuard < 32; innerGuard++) {
        for (const auto &candidate : r->methods) {
          if (candidate->name == m->name) {
            resolved = candidate.get();
            break;
          }
        }
        if (resolved) {
          break;
        }
        auto baseIt = classes.find(r->baseName);
        r = (r->baseName.empty() || baseIt == classes.end()) ? nullptr
                                                             : baseIt->second;
      }
      if (!resolved || resolved->isAbstract) {
        throw runtime_error("Class '" + cls->name +
                            "' must implement abstract method '" + m->name +
                            "()'");
      }
    }
    auto baseIt = classes.find(c->baseName);
    c = (c->baseName.empty() || baseIt == classes.end()) ? nullptr
                                                         : baseIt->second;
  }

  return constructInstance(cls->name, collectFields(cls),
                           findConstructorFor(cls), args);
}

Value Interpreter::constructInstance(const string &typeName,
                                     const vector<FieldNode> &fields,
                                     MethodNode *ctor,
                                     const vector<Value> &args) {
  Value obj(Type(typeName), "");
  for (const auto &field : fields) {
    obj.entries[field.name] = Value(field.type, "");
  }

  if (ctor) {

    vector<map<string, Value>> savedScopes = std::move(scopes);
    bool savedHasReturned = hasReturned;
    bool savedHasBroken = hasBroken;
    bool savedHasContinued = hasContinued;
    Value savedReturnValue = returnValue;

    scopes.clear();
    pushScope();
    for (size_t i = 0; i < ctor->parameters.size() && i < args.size(); i++) {
      defineVariable(ctor->parameters[i].name,
                     coerceType(args[i], ctor->parameters[i].type));
    }
    defineVariable("this", obj);

    hasReturned = false;
    hasBroken = false;
    hasContinued = false;

    executeBlock(ctor->body);

    if (Value *self = findVariable("this")) {
      obj = *self;
    }

    hasReturned = savedHasReturned;
    hasBroken = savedHasBroken;
    hasContinued = savedHasContinued;
    returnValue = savedReturnValue;
    scopes = std::move(savedScopes);
  } else {
    // No constructor: positional field initialization
    size_t i = 0;
    for (const auto &field : fields) {
      if (i >= args.size())
        break;
      obj.entries[field.name] = coerceType(args[i], field.type);
      i++;
    }
  }

  return obj;
}

Value Interpreter::boolValue(bool b) {
  return Value(Type("bool"), b ? "true" : "false");
}

bool Interpreter::isTruthy(const Value &v) { return v.data == "true"; }

string Interpreter::display(const Value &v) {
  if (!v.type.isArray && v.type.name != "map" && !v.entries.empty() &&
      v.data.empty()) {
    // class instance (enums carry their constant name in data instead)
    string out = v.type.name + "{";
    bool first = true;
    for (const auto &entry : v.entries) {
      if (!first) {
        out += ", ";
      }
      first = false;
      out += entry.first + ": " + display(entry.second);
    }
    return out + "}";
  }

  if (v.type.name == "map") {
    string out = "{";
    bool first = true;
    for (const auto &entry : v.entries) {
      if (!first) {
        out += ", ";
      }
      first = false;
      out += entry.first + ": " + display(entry.second);
    }
    return out + "}";
  }

  if (!v.type.isArray) {
    return v.data;
  }

  string out = "[";
  for (size_t i = 0; i < v.elements.size(); i++) {
    if (i > 0) {
      out += ", ";
    }
    out += display(v.elements[i]);
  }
  return out + "]";
}

void Interpreter::visitIndexAssignment(const IndexAssignmentNode *node) {
  if (!node)
    return;

  // Evaluate index and value first - nested calls may reallocate `variables`.
  Value indexValue = evalExpression(node->index);
  Value value = evalExpression(node->value);

  Value *found = findVariable(node->name);
  if (!found) {
    throw runtime_error("Unknown variable '" + node->name + "'");
  }

  Value &target = *found;

  if (target.type.name == "map") {
    target.entries[indexValue.data] = value; // insert or update
    return;
  }

  if (!target.type.isArray) {
    throw runtime_error("'" + node->name + "' is not an array or map");
  }

  int index = stoi(indexValue.data);
  if (index < 0 || (size_t)index >= target.elements.size()) {
    throw runtime_error("Array index " + to_string(index) +
                        " out of bounds for '" + node->name + "' (size " +
                        to_string(target.elements.size()) + ")");
  }

  target.elements[index] = value;
}

Value Interpreter::evalNode(const Node *node) {
  if (!node)
    return Value();

  if (auto idNode = dynamic_cast<const IdentifierNode *>(node)) {
    return getValue(idNode->name);
  }

  if (auto memberCall = dynamic_cast<const MemberCallNode *>(node)) {
    vector<Value> args;
    for (const auto &arg : memberCall->arguments) {
      args.push_back(evalExpression(arg));
    }

    // Calls on a plain variable mutate it in place (setters, append, ...)
    if (auto id =
            dynamic_cast<const IdentifierNode *>(memberCall->target.get())) {
      if (Value *found = findVariable(id->name)) {
        return callMemberOn(*found, memberCall->methodName, args);
      }
      if (id->name == "path" || id->name == "json") {
        return callModuleFunction(id->name, memberCall->methodName, args);
      }
      throw runtime_error("Unknown variable '" + id->name + "'");
    }

    Value temp = evalExpression(memberCall->target);
    return callMemberOn(temp, memberCall->methodName, args);
  }

  if (auto memberAccess = dynamic_cast<const MemberAccessNode *>(node)) {
    // Enum constant: Gender.MALE
    if (auto id =
            dynamic_cast<const IdentifierNode *>(memberAccess->target.get())) {
      auto enumIt = enumValues.find(id->name);
      if (enumIt != enumValues.end()) {
        auto constant = enumIt->second.find(memberAccess->propertyName);
        if (constant == enumIt->second.end()) {
          throw runtime_error("No constant '" + memberAccess->propertyName +
                              "' in enum " + id->name);
        }
        return constant->second;
      }
    }

    if (auto id =
            dynamic_cast<const IdentifierNode *>(memberAccess->target.get())) {
      if (!findVariable(id->name) && id->name == "path" &&
          memberAccess->propertyName == "absolute") {
        return Value(Type("string"), filesystem::current_path().string());
      }
    }

    Value target = evalExpression(memberAccess->target);
    auto entry = target.entries.find(memberAccess->propertyName);
    if (entry == target.entries.end()) {
      throw runtime_error("No property '" + memberAccess->propertyName +
                          "' on value of type " + target.type.name);
    }
    return entry->second;
  }

  if (auto methodCallNode = dynamic_cast<const MethodCallNode *>(node)) {
    return visitMethodCall(methodCallNode);
  }

  if (auto binOpNode = dynamic_cast<const BinaryOpNode *>(node)) {
    return visitBinaryOp(binOpNode);
  }

  if (auto intLitNode = dynamic_cast<const IntegerLiteralNode *>(node)) {
    return visitIntegerLiteral(intLitNode);
  }

  if (auto floatLitNode = dynamic_cast<const FloatLiteralNode *>(node)) {
    return visitFloatLiteral(floatLitNode);
  }

  if (auto strLitNode = dynamic_cast<const StringLiteralNode *>(node)) {
    return visitStringLiteral(strLitNode);
  }

  if (auto arrayNode = dynamic_cast<const ArrayLiteralNode *>(node)) {
    Value val(Type("array", true), "");
    for (const auto &element : arrayNode->elements) {
      val.elements.push_back(evalExpression(element));
    }
    return val;
  }

  if (auto objNode = dynamic_cast<const ObjectLiteralNode *>(node)) {
    auto classIt = classes.find(objNode->className);
    if (classIt == classes.end()) {
      throw runtime_error("Unknown class '" + objNode->className + "'");
    }
    if (classIt->second->isAbstract) {
      throw runtime_error("Cannot instantiate abstract class '" +
                          objNode->className + "'");
    }
    Value obj(Type(objNode->className), "");
    for (const auto &field : collectFields(classIt->second)) {
      obj.entries[field.name] = Value(field.type, "");
    }
    for (const auto &field : objNode->fields) {
      obj.entries[field.first] = evalExpression(field.second);
    }
    return obj;
  }

  if (auto propNode = dynamic_cast<const PropertyAccessNode *>(node)) {
    // Enum constant: Gender.MALE
    auto enumIt = enumValues.find(propNode->objectName);
    if (enumIt != enumValues.end()) {
      auto constant = enumIt->second.find(propNode->propertyName);
      if (constant == enumIt->second.end()) {
        throw runtime_error("No constant '" + propNode->propertyName +
                            "' in enum " + propNode->objectName);
      }
      return constant->second;
    }

    Value *obj = findVariable(propNode->objectName);
    if (!obj) {
      throw runtime_error("Unknown variable '" + propNode->objectName + "'");
    }
    auto entry = obj->entries.find(propNode->propertyName);
    if (entry == obj->entries.end()) {
      throw runtime_error("No property '" + propNode->propertyName +
                          "' on '" + propNode->objectName + "'");
    }
    return entry->second;
  }

  if (auto mapNode = dynamic_cast<const MapLiteralNode *>(node)) {
    Value val(Type("map"), "");
    for (const auto &entry : mapNode->entries) {
      string key = evalExpression(entry.first).data;
      val.entries[key] = evalExpression(entry.second);
    }
    return val;
  }

  if (auto indexNode = dynamic_cast<const IndexAccessNode *>(node)) {
    Value target = evalExpression(indexNode->target);

    if (target.type.name == "map") {
      string key = evalExpression(indexNode->index).data;
      auto entry = target.entries.find(key);
      if (entry == target.entries.end()) {
        throw runtime_error("Key '" + key + "' not found in map");
      }
      return entry->second;
    }

    if (!target.type.isArray) {
      throw runtime_error("Cannot index into a non-array value");
    }

    int index = stoi(evalExpression(indexNode->index).data);
    if (index < 0 || (size_t)index >= target.elements.size()) {
      throw runtime_error("Array index " + to_string(index) +
                          " out of bounds (size " +
                          to_string(target.elements.size()) + ")");
    }
    return target.elements[index];
  }

  if (auto boolLitNode = dynamic_cast<const BoolLiteralNode *>(node)) {
    return boolValue(boolLitNode->value);
  }

  if (auto unaryNode = dynamic_cast<const UnaryOpNode *>(node)) {
    Value operand = evalExpression(unaryNode->operand);
    return boolValue(!isTruthy(operand));
  }

  if (auto retNode = dynamic_cast<const ReturnNode *>(node)) {
    return visitReturn(retNode);
  }

  return Value();
}

Value Interpreter::evalExpression(const unique_ptr<Node> &node) {
  return evalNode(node.get());
}

void Interpreter::pushScope() { scopes.push_back({}); }

void Interpreter::popScope() {
  if (!scopes.empty()) {
    scopes.pop_back();
  }
}

Value *Interpreter::findVariable(const string &name) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      return &found->second;
    }
  }
  auto global = globals.find(name);
  if (global != globals.end()) {
    return &global->second;
  }
  return nullptr;
}

void Interpreter::defineVariable(const string &name, const Value &value) {
  if (scopes.empty()) {
    pushScope();
  }
  scopes.back()[name] = value;
}

Value Interpreter::getValue(const string &varName) {
  if (Value *found = findVariable(varName)) {
    return *found;
  }
  return Value(Type("unknown"), "");
}

Value Interpreter::coerceType(const Value &val, const Type &targetType) {
  if (val.type == targetType) {
    return val;
  }

  if (targetType.name == "integer" && val.type.name == "float") {
    double floatVal = stod(val.data);
    int intVal = static_cast<int>(floatVal);
    return Value(Type("integer"), to_string(intVal));
  }

  if (targetType.name == "float" && val.type.name == "integer") {
    int intVal = stoi(val.data);
    double floatVal = static_cast<double>(intVal);
    return Value(Type("float"), to_string(floatVal));
  }

  if (targetType.name == "string") {
    return Value(Type("string"), val.data);
  }

  return val;
}

static nlohmann::json valueToJson(const Value &v) {
  if (v.type.isArray) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &element : v.elements) {
      arr.push_back(valueToJson(element));
    }
    return arr;
  }
  if (v.type.name == "map" || !v.entries.empty()) {
    nlohmann::json obj = nlohmann::json::object();
    for (const auto &entry : v.entries) {
      obj[entry.first] = valueToJson(entry.second);
    }
    return obj;
  }
  if (v.type.name == "integer") {
    return stoi(v.data.empty() ? "0" : v.data);
  }
  if (v.type.name == "float") {
    return stod(v.data.empty() ? "0" : v.data);
  }
  if (v.type.name == "bool") {
    return v.data == "true";
  }
  return v.data;
}

static Value jsonToValue(const nlohmann::json &j) {
  if (j.is_array()) {
    Value arr(Type("array", true), "");
    for (const auto &element : j) {
      arr.elements.push_back(jsonToValue(element));
    }
    return arr;
  }
  if (j.is_object()) {
    Value obj(Type("map"), "");
    for (auto it = j.begin(); it != j.end(); ++it) {
      obj.entries[it.key()] = jsonToValue(it.value());
    }
    return obj;
  }
  if (j.is_boolean()) {
    return Value(Type("bool"), j.get<bool>() ? "true" : "false");
  }
  if (j.is_number_integer()) {
    return Value(Type("integer"), to_string(j.get<long long>()));
  }
  if (j.is_number()) {
    return Value(Type("float"), to_string(j.get<double>()));
  }
  if (j.is_string()) {
    return Value(Type("string"), j.get<string>());
  }
  return Value();
}

Value Interpreter::callModuleFunction(const string &module, const string &fn,
                                      vector<Value> &args) {
  if (module == "path") {
    if (fn == "join") {
      filesystem::path joined;
      for (const auto &arg : args) {
        joined /= arg.data;
      }
      return Value(Type("string"), joined.string());
    }
    if (fn == "absolute") {
      return Value(Type("string"), filesystem::current_path().string());
    }
    if (fn == "exists") {
      return boolValue(!args.empty() && filesystem::exists(args[0].data));
    }
  }

  if (module == "json") {
    if (fn == "stringify") {
      if (args.empty()) {
        return Value(Type("string"), "null");
      }
      return Value(Type("string"), valueToJson(args[0]).dump());
    }
    if (fn == "parse") {
      if (args.empty()) {
        return Value();
      }
      return jsonToValue(nlohmann::json::parse(args[0].data));
    }
    if (fn == "save") {
      if (args.size() < 3) {
        throw runtime_error("json.save(value, dir, filename) needs 3 args");
      }
      filesystem::path target = filesystem::path(args[1].data) / args[2].data;
      writeFile(target.string(), valueToJson(args[0]).dump(2));
      return Value();
    }
  }

  throw runtime_error("Unknown module function " + module + "." + fn + "()");
}

void Interpreter::warnIfDeprecated(const MethodNode *m) {
  for (const auto &use : m->annotations) {
    if (use.name != "Deprecated") {
      continue;
    }
    if (deprecatedWarned.count(m)) {
      return;
    }
    deprecatedWarned.insert(m);

    string text;
    string since;
    for (const auto &kv : use.args) {
      if (kv.first == "text") {
        text = kv.second;
      }
      if (kv.first == "since") {
        since = kv.second;
      }
    }

    string message = "[warning] Method '" + m->name + "' is deprecated";
    if (!since.empty()) {
      message += " (since " + since + ")";
    }
    if (!text.empty()) {
      message += ": " + text;
    }
    std::cout << message << std::endl;
  }
}

MethodNode *Interpreter::selectOverload(const vector<MethodNode *> &candidates,
                                        const vector<Value> &args) {
  MethodNode *sizeMatch = nullptr;
  for (auto *candidate : candidates) {
    if (candidate->parameters.size() != args.size()) {
      continue;
    }
    if (!sizeMatch) {
      sizeMatch = candidate;
    }
    bool exact = true;
    for (size_t i = 0; i < args.size(); i++) {
      if (!(candidate->parameters[i].type == args[i].type)) {
        exact = false;
        break;
      }
    }
    if (exact) {
      return candidate;
    }
  }
  return sizeMatch ? sizeMatch : candidates.front();
}

Value Interpreter::callMethod(const string &methodName,
                              const vector<Value> &args) {
  // Builtin free functions (take precedence over user methods)
  if (methodName == "readFile") {
    string path = args.size() > 0 ? args[0].data : "";
    return Value(Type("string"), readFile(path));
  }

  if (methodName == "writeFile") {
    string path = args.size() > 0 ? args[0].data : "";
    string content = args.size() > 1 ? args[1].data : "";
    writeFile(path, content);
    return Value();
  }

  if (methodName == "fileExists") {
    string path = args.size() > 0 ? args[0].data : "";
    return boolValue(fileExists(path));
  }

  if (methodName == "args") {
    Value arr(Type("array", true), "");
    for (const auto &arg : programArgs) {
      arr.elements.push_back(Value(Type("string"), arg));
    }
    return arr;
  }

  if (methodName == "parseInt") {
    string s = args.size() > 0 ? args[0].data : "0";
    return Value(Type("integer"), to_string(stoi(s)));
  }

  if (methodName == "chr") {
    int code = args.empty() ? 0 : stoi(args[0].data);
    return Value(Type("string"), string(1, (char)code));
  }

  if (methodName == "ord") {
    if (args.empty() || args[0].data.empty()) {
      return Value(Type("integer"), "0");
    }
    return Value(Type("integer"),
                 to_string((int)(unsigned char)args[0].data[0]));
  }

  if (methodName == "typeOf") {
    if (args.empty()) {
      return Value(Type("string"), "unknown");
    }
    const Value &v = args[0];
    if (v.type.isArray) {
      return Value(Type("string"), "array");
    }
    return Value(Type("string"), v.type.name);
  }

  auto classIt = classes.find(methodName);
  if (classIt != classes.end()) {
    return constructObject(classIt->second, args);
  }

  auto it = methods.find(methodName);
  if (it == methods.end()) {
    return Value();
  }

  MethodNode *method = selectOverload(it->second, args);
  warnIfDeprecated(method);

  vector<map<string, Value>> savedScopes = std::move(scopes);
  bool savedHasReturned = hasReturned;
  bool savedHasBroken = hasBroken;
  bool savedHasContinued = hasContinued;
  Value savedReturnValue = returnValue;

  scopes.clear();
  pushScope(); // parameter scope
  for (size_t i = 0; i < method->parameters.size() && i < args.size(); i++) {
    defineVariable(method->parameters[i].name,
                   coerceType(args[i], method->parameters[i].type));
  }

  hasReturned = false;
  hasBroken = false;
  hasContinued = false;
  returnValue = Value(method->returnType, "");

  executeBlock(method->body);

  Value result = coerceType(returnValue, method->returnType);

  // restore caller state
  hasReturned = savedHasReturned;
  hasBroken = savedHasBroken;
  hasContinued = savedHasContinued;
  returnValue = savedReturnValue;
  scopes = std::move(savedScopes);

  return result;
}

Value Interpreter::visitBinaryOp(const BinaryOpNode *node) {
  if (!node)
    return Value();

  const string &op = node->op;

  // 1. Logical operators (short-circuit: evaluate right side only if needed)
  if (op == "&&") {
    if (!isTruthy(evalExpression(node->left)))
      return boolValue(false);
    return boolValue(isTruthy(evalExpression(node->right)));
  }
  if (op == "||") {
    if (isTruthy(evalExpression(node->left)))
      return boolValue(true);
    return boolValue(isTruthy(evalExpression(node->right)));
  }

  Value left = evalExpression(node->left);
  Value right = evalExpression(node->right);

  // 2. Strings: concatenation and comparison
  if (left.type.name == "string" || right.type.name == "string") {
    if (op == "+")
      return Value(Type("string"), display(left) + display(right));
    if (op == "==")
      return boolValue(left.data == right.data);
    if (op == "!=")
      return boolValue(left.data != right.data);
    if (op == "<")
      return boolValue(left.data < right.data);
    if (op == ">")
      return boolValue(left.data > right.data);
    if (op == "<=")
      return boolValue(left.data <= right.data);
    if (op == ">=")
      return boolValue(left.data >= right.data);
    return Value();
  }

  // 3. Bool equality
  if (left.type.name == "bool" || right.type.name == "bool") {
    if (op == "==")
      return boolValue(left.data == right.data);
    if (op == "!=")
      return boolValue(left.data != right.data);
    return Value();
  }

  // 4a. Non-numeric equality (enum constants, objects): identity via data
  if (op == "==" || op == "!=") {
    bool leftNumeric = left.type.name == "integer" || left.type.name == "float";
    bool rightNumeric =
        right.type.name == "integer" || right.type.name == "float";
    if (!leftNumeric || !rightNumeric) {
      if (op == "==")
        return boolValue(left.data == right.data);
      return boolValue(left.data != right.data);
    }
  }

  // 4b. Numeric comparisons (double covers integer as well)
  if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" ||
      op == ">=") {
    double l = stod(left.data);
    double r = stod(right.data);

    if (op == "==")
      return boolValue(l == r);
    if (op == "!=")
      return boolValue(l != r);
    if (op == "<")
      return boolValue(l < r);
    if (op == ">")
      return boolValue(l > r);
    if (op == "<=")
      return boolValue(l <= r);
    return boolValue(l >= r);
  }

  // 5. Arithmetic
  bool isFloat = left.type.name == "float" || right.type.name == "float";

  if (isFloat) {
    double leftNum = stod(left.data);
    double rightNum = stod(right.data);
    double result = 0;

    if (op == "+")
      result = leftNum + rightNum;
    else if (op == "-")
      result = leftNum - rightNum;
    else if (op == "*")
      result = leftNum * rightNum;
    else if (op == "/")
      result = rightNum != 0 ? leftNum / rightNum : 0;
    else if (op == "%")
      result = fmod(leftNum, rightNum);
    else
      return Value();

    return Value(Type("float"), to_string(result));
  }

  int leftNum = stoi(left.data);
  int rightNum = stoi(right.data);
  int result = 0;

  if (op == "+")
    result = leftNum + rightNum;
  else if (op == "-")
    result = leftNum - rightNum;
  else if (op == "*")
    result = leftNum * rightNum;
  else if (op == "/")
    result = rightNum != 0 ? leftNum / rightNum : 0;
  else if (op == "%")
    result = rightNum != 0 ? leftNum % rightNum : 0;
  else
    return Value();

  return Value(Type("integer"), to_string(result));
}

Value Interpreter::visitIntegerLiteral(const IntegerLiteralNode *node) {
  if (!node)
    return Value();
  return Value(Type("integer"), to_string(node->value));
}

Value Interpreter::visitFloatLiteral(const FloatLiteralNode *node) {
  if (!node)
    return Value();
  return Value(Type("float"), to_string(node->value));
}

Value Interpreter::visitStringLiteral(const StringLiteralNode *node) {
  if (!node)
    return Value();
  return Value(Type("string"), node->value);
}

Value Interpreter::visitReturn(const ReturnNode *node) {
  if (!node || !node->value) {
    return Value();
  }

  return evalExpression(node->value);
}
