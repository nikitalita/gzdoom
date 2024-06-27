#include "LocalScopeStateNode.h"
#include "common/scripting/dap/GameInterfaces.h"
#include "types.h"
#include <common/scripting/dap/Utilities.h>
#include <common/scripting/dap/RuntimeState.h>

namespace DebugServer
{
	LocalScopeStateNode::LocalScopeStateNode(VMFrame* stackFrame) : m_stackFrame(stackFrame)
	{
	}

	bool LocalScopeStateNode::SerializeToProtocol(dap::Scope& scope)
	{
		scope.name = "Local";
		scope.expensive = false;

		scope.variablesReference = GetId();

		std::vector<std::string> childNames;
		GetChildNames(childNames);

		scope.namedVariables = childNames.size();
		scope.indexedVariables = 0;

		return true;
	}

	bool LocalScopeStateNode::GetChildNames(std::vector<std::string>& names)
	{
		// TODO: how does the VM indicate whether a function is static?
		// apparently, it's indicated by absence of VARF_Method on VMFunction::VarFlags
		if (m_stackFrame->Func->VarFlags & VARF_Action)
		{
			names.push_back("self");
			names.push_back("invoker");
      names.push_back("state_pointer");
		}
		else if (m_stackFrame->Func->VarFlags & VARF_Method)
		{
			names.push_back("self");
      // TODO: Figure out if there is a state_pointer in non-action methods?
      // names.push_back("state_pointer");
		}
		if (IsFunctionNative(m_stackFrame->Func))
		{
			// Can't introspect locals in native functions
			return true;
		}
		// TODO: waiting for PR to add local/register mapping to VMFrame

		return true;
	}

	bool LocalScopeStateNode::GetChildNode(std::string name, std::shared_ptr<StateNodeBase>& node)
	{
		bool has_self = false;
		bool has_invoker = false;
    bool has_state_pointer = false;
		if (m_children.find(name) != m_children.end())
		{
			node = m_children[name];
			return true;
		}
    int paramidx = -1;
		if (CaseInsensitiveEquals(name, "self")){
			has_self = true;
      paramidx = 0;
		} else if (CaseInsensitiveEquals(name, "invoker")){
			has_invoker = true;
      paramidx = 1;
		} else if (CaseInsensitiveEquals(name, "state_pointer")){
      paramidx = 1;
      if (m_stackFrame->Func->VarFlags & VARF_Action){
        paramidx = 2;
      }
    }
		if (paramidx >= 0)
		{
			if (m_stackFrame->NumRegA == 0) {
				LogError("Function {} has '{}' but no parameters", m_stackFrame->Func->QualifiedName, name);
				return false;
			} else if (m_stackFrame->NumRegA <= paramidx) {
				LogError("Function {} has '{}' but <= {} parameters", m_stackFrame->Func->QualifiedName, name, paramidx);
				return false;
			}
			if (m_stackFrame->Func->Proto->ArgumentTypes.size() == 0) {
				LogError("Function {} has '{}' but no argument types", m_stackFrame->Func->QualifiedName, name);
				return false;
			} else if (m_stackFrame->Func->Proto->ArgumentTypes.size() <= paramidx) {
				LogError("Function {} has '{}' but <= {} argument types", m_stackFrame->Func->QualifiedName, name, paramidx);
				return false;
			}
			auto type = m_stackFrame->Func->Proto->ArgumentTypes[paramidx];
			auto val = m_stackFrame->GetRegA()[paramidx];
			m_children[name] = RuntimeState::CreateNodeForVariable(ToLowerCopy(name), val, type);
			node = m_children[name];
			return true;
		}
		// TODO: no locals yet; waiting on PR to add local/register mapping to VMFrame
		return false;

		return false;
	}
}
