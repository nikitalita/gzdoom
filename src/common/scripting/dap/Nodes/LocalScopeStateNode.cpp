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
		bool has_self = false;
		bool has_invoker = false;
		
		if (m_stackFrame->Func->VarFlags & VARF_Action)
		{
			names.push_back("self");
			names.push_back("invoker");
			has_self = true;
			has_invoker = true;
		} 
		else if (m_stackFrame->Func->VarFlags & VARF_Method)
		{
			names.push_back("self");
			has_self = true;
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
		if (m_children.find(name) != m_children.end())
		{
			node = m_children[name];
			return true;
		}
		if (CaseInsensitiveEquals(name, "self")){
			has_self = true;
		} else if (CaseInsensitiveEquals(name, "invoker")){
			has_invoker = true;
		}
		if (has_self || has_invoker)
		{
			if (has_self && m_stackFrame->MaxParam == 0) {
				LogError("Function {} has 'self' but no parameters", m_stackFrame->Func->QualifiedName);
				return false;
			} else if (has_invoker && m_stackFrame->MaxParam <= 1) {
				LogError("Function {} has 'invoker' but <= 1 parameter", m_stackFrame->Func->QualifiedName);
				return false;
			}
			if (has_self && m_stackFrame->Func->Proto->ArgumentTypes.size() == 0) {
				LogError("Function {} has 'self' but no argument types", m_stackFrame->Func->QualifiedName);
				return false;
			} else if (has_invoker && m_stackFrame->Func->Proto->ArgumentTypes.size() <= 1) {
				LogError("Function {} has 'invoker' but <= 1 argument types", m_stackFrame->Func->QualifiedName);
				return false;
			}
			int idx = has_self ? 0 : 1;
			auto params = m_stackFrame->GetParam();
			auto & param = params[idx];
			auto selfType = m_stackFrame->Func->Proto->ArgumentTypes[idx];
			auto val = m_stackFrame->GetParam()[idx];
			m_children[name] = RuntimeState::CreateNodeForVariable(ToLowerCopy(name), val, selfType);
			node = m_children[name];
			return true;
		}
		// TODO: no locals yet; waiting on PR to add local/register mapping to VMFrame
		return false;

		// for (uint32_t i = 0; i < m_stackFrame->owningFunction->GetStackFrameSize(); i++)
		// {
		// 	RE::BSFixedString varName;
		// 	m_stackFrame->owningFunction->GetVarNameForStackIndex(i, varName);

		// 	if (varName.empty() || varName.front() == ':')
		// 	{
		// 		continue;
		// 	}

		// 	if (CaseInsensitiveEquals(name, varName.c_str()))
		// 	{
		// 		const uint32_t pageHint = m_stackFrame->parent->GetPageForFrame(m_stackFrame);
		// 		VMValue& variable = m_stackFrame->GetStackFrameVariable(i, pageHint);
		// 		if (&variable)
		// 		{
		// 			node = RuntimeState::CreateNodeForVariable(varName.c_str(), &variable);

		// 			return true;
		// 		}
		// 	}
		// }

		return false;
	}
}
