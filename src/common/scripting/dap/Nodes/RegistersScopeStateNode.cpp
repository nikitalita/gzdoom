#include "RegistersScopeStateNode.h"
#include <common/scripting/dap/Utilities.h>
#include <common/scripting/dap/RuntimeState.h>
#include "ValueStateNode.h"
namespace DebugServer
{
	RegistersScopeStateNode::RegistersScopeStateNode(VMFrame* stackFrame) : m_stackFrame(stackFrame)
	{
	}

	bool RegistersScopeStateNode::SerializeToProtocol(dap::Scope& scope)
	{
		scope.name = "Registers";
		scope.expensive = false;
    scope.presentationHint = "registers";
		scope.variablesReference = GetId();

		std::vector<std::string> childNames;
		GetChildNames(childNames);

		scope.namedVariables = childNames.size();
		scope.indexedVariables = 0;

		return true;
	}

	bool RegistersScopeStateNode::GetChildNames(std::vector<std::string>& names)
	{
		names.emplace_back("Params");
		names.emplace_back("IntReg");
		names.emplace_back("FloatReg");
		names.emplace_back("StringReg");
		names.emplace_back("PointerReg");
		return true;
	}

	bool RegistersScopeStateNode::GetChildNode(std::string name, std::shared_ptr<StateNodeBase>& node)
	{
    if (CaseInsensitiveEquals(name, "Params"))
    {
        node = std::make_shared<ParamsRegistersNode>(m_stackFrame);
        return true;
    }
    else if (CaseInsensitiveEquals(name, "IntReg"))
    {
        node = std::make_shared<IntRegistersNode>(m_stackFrame);
        return true;
    }
    else if (CaseInsensitiveEquals(name, "FloatReg"))
    {
        node = std::make_shared<FloatRegistersNode>(m_stackFrame);
        return true;
    }
    else if (CaseInsensitiveEquals(name, "StringReg"))
    {
        node = std::make_shared<StringRegistersNode>(m_stackFrame);
        return true;
    }
    else if (CaseInsensitiveEquals(name, "PointerReg"))
    {
        node = std::make_shared<PointerRegistersNode>(m_stackFrame);
        return true;
    }

		return false;
	}


bool RegistersNode::SerializeToProtocol(dap::Variable &variable) {

    variable.name = GetName() + "sReg";
    variable.type = GetName() + " Registers";
    // value will be the max number of registers
    auto max_num_reg = GetNumberOfRegisters();
    variable.value = GetName() + "[" + std::to_string(max_num_reg) + "]";
    variable.indexedVariables = max_num_reg;
    variable.variablesReference = GetId();
    return true;
}

bool RegistersNode::GetChildNames(std::vector<std::string> &names) {
  for (int i = 0; i < GetNumberOfRegisters(); i++) {
    names.push_back(std::to_string(i));
  }
  return true;
}

bool RegistersNode::GetChildNode(std::string name, std::shared_ptr<StateNodeBase> &node) {
  int index = std::stoi(name);
  if (index < 0 || index >= GetNumberOfRegisters()) {
    return false;
  }
  VMValue val = GetRegisterValue(index);
  node = RuntimeState::CreateNodeForVariable(name, val, GetRegisterType(index));
  return true;
}

std::string PointerRegistersNode::GetName() {
  return "Pointer";
}

int PointerRegistersNode::GetNumberOfRegisters() {
  return m_stackFrame->NumRegA;
}

VMValue PointerRegistersNode::GetRegisterValue(int index) {
  return m_stackFrame->GetRegA()[index];
}

PType *PointerRegistersNode::GetRegisterType(int index) {
  return TypeVoidPtr;
}

std::string StringRegistersNode::GetName() {
  return "String";
}

int StringRegistersNode::GetNumberOfRegisters() {
  return m_stackFrame->NumRegS;
}

VMValue StringRegistersNode::GetRegisterValue(int index) {
  return {&m_stackFrame->GetRegS()[index]};
}

PType *StringRegistersNode::GetRegisterType(int index) {
  return TypeString;
}

std::string FloatRegistersNode::GetName() {
  return "Float";
}

int FloatRegistersNode::GetNumberOfRegisters() {
  return m_stackFrame->NumRegF;
}

VMValue FloatRegistersNode::GetRegisterValue(int index) {
  return m_stackFrame->GetRegF()[index];
}

PType *FloatRegistersNode::GetRegisterType(int index) {
  return TypeFloat64;
}

std::string IntRegistersNode::GetName() {
  return "Int";
}

int IntRegistersNode::GetNumberOfRegisters() {
  return m_stackFrame->NumRegD;
}

VMValue IntRegistersNode::GetRegisterValue(int index) {
  return m_stackFrame->GetRegD()[index];
}

PType *IntRegistersNode::GetRegisterType(int index) {
  return TypeSInt32;
}

std::string ParamsRegistersNode::GetName() {
  return "Params";
}

int ParamsRegistersNode::GetNumberOfRegisters() {
  return m_stackFrame->MaxParam;
}

VMValue ParamsRegistersNode::GetRegisterValue(int index) {
  return m_stackFrame->GetParam()[index];
}

PType *ParamsRegistersNode::GetRegisterType(int index) {
//    auto a_types = m_stackFrame->Func->Proto->ArgumentTypes;
//  if (index < m_stackFrame->Func->Proto->ArgumentTypes.size()) {
//      return m_stackFrame->Func->Proto->ArgumentTypes[index];
//  }
  return TypeVoidPtr; // Replace with the actual type of P
}

bool PointerRegistersNode::GetChildNode(std::string name, std::shared_ptr<StateNodeBase> &node) {
  int index = std::stoi(name);
  if (index < 0 || index >= GetNumberOfRegisters()) {
    return false;
  }
  VMValue val = GetRegisterValue(index);
  node = RuntimeState::CreateNodeForVariable(name, val, GetRegisterType(index));
  return true;
}


bool ParamsRegistersNode::SerializeToProtocol(dap::Variable &variable) {

  variable.name = "ParamsReg";
  variable.type = "Parameter Registers";
  // value will be the max number of registers
  auto max_num_reg = GetNumberOfRegisters();
  variable.value = "Params - Max: " + std::to_string(max_num_reg) + ", In Use: " + std::to_string(m_stackFrame->NumParam);
  variable.indexedVariables = max_num_reg;
  variable.variablesReference = GetId();
  return true;
}

RegistersNode::RegistersNode(VMFrame *stackFrame)
: m_stackFrame(stackFrame) {

}
}
