#pragma once

#include <common/scripting/dap/GameInterfaces.h>
#include <dap/protocol.h>

#include "StateNodeBase.h"

namespace DebugServer {


class RegistersNode : public StateNodeBase, public IProtocolVariableSerializable, public IStructuredState {
protected:
  VMFrame *m_stackFrame;
  caseless_path_map<std::shared_ptr<StateNodeBase>> m_children;
public:
  RegistersNode(VMFrame *stackFrame);

  virtual int GetNumberOfRegisters() = 0;

  virtual std::string GetName() = 0;

  virtual VMValue GetRegisterValue(int index)  = 0;

  virtual PType * GetRegisterType([[maybe_unused]] int index)  = 0;


  bool SerializeToProtocol(dap::Variable &variable) override;

  bool GetChildNames(std::vector<std::string> &names) override;

  bool GetChildNode(std::string name, std::shared_ptr<StateNodeBase> &node) override;

};

class PointerRegistersNode : public RegistersNode {
public:
  int GetNumberOfRegisters() override;

  std::string GetName() override;

  VMValue GetRegisterValue(int index) override;

  PType * GetRegisterType([[maybe_unused]] int index) override;

  PointerRegistersNode(VMFrame *stackFrame) : RegistersNode(stackFrame) {};

  bool GetChildNode(std::string name, std::shared_ptr<StateNodeBase> &node);
};

class StringRegistersNode : public RegistersNode {
public:
  int GetNumberOfRegisters() override;

  std::string GetName() override;

  VMValue GetRegisterValue(int index) override;

  PType * GetRegisterType([[maybe_unused]] int index) override;

  StringRegistersNode(VMFrame *stackFrame) : RegistersNode(stackFrame) {};
};

class FloatRegistersNode : public RegistersNode {
public:
  int GetNumberOfRegisters() override;

  std::string GetName() override;

  VMValue GetRegisterValue(int index) override;

  PType * GetRegisterType([[maybe_unused]] int index) override;

  FloatRegistersNode(VMFrame *stackFrame) : RegistersNode(stackFrame) {};
};

class IntRegistersNode : public RegistersNode {
public:
  int GetNumberOfRegisters() override;

  std::string GetName() override;

  VMValue GetRegisterValue(int index) override;

  PType * GetRegisterType([[maybe_unused]] int index) override;

  IntRegistersNode(VMFrame *stackFrame) : RegistersNode(stackFrame) {};
};

class ParamsRegistersNode : public RegistersNode {
public:
  int GetNumberOfRegisters() override;

  std::string GetName() override;

  VMValue GetRegisterValue(int index) override;

  PType * GetRegisterType([[maybe_unused]] int index) override;

  ParamsRegistersNode(VMFrame *stackFrame) : RegistersNode(stackFrame) {};

  bool SerializeToProtocol(dap::Variable &variable) override;
};

class RegistersScopeStateNode : public StateNodeBase, public IProtocolScopeSerializable, public IStructuredState {
  VMFrame *m_stackFrame;
  caseless_path_map<std::shared_ptr<StateNodeBase>> m_children;

public:
  RegistersScopeStateNode(VMFrame *stackFrame);

  bool SerializeToProtocol(dap::Scope &scope) override;

  bool GetChildNames(std::vector<std::string> &names) override;

  bool GetChildNode(std::string name, std::shared_ptr<StateNodeBase> &node) override;
};
}
