#include "LocalScopeStateNode.h"
#include "../Utilities.h"
#include "../RuntimeState.h"

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
		// if (!m_stackFrame->owningFunction->GetIsStatic())
		{
			names.push_back("self");
		}
		// m_stackFrame->Func
		auto * scriptFunction = dynamic_cast<VMScriptFunction*>(m_stackFrame->Func);
		// if (scriptFunction)
		// {
		// 	names.push_back("self");
		// }
		// scriptFunction->StackSize
		// for (uint32_t i = 0; i < m_stackFrame->Func->GetStackFrameSize(); i++)
		// {
		// 	RE::BSFixedString varName;
		// 	m_stackFrame->owningFunction->GetVarNameForStackIndex(i, varName);

		// 	if (varName.empty() || varName.front() == ':')
		// 	{
		// 		continue;
		// 	}

		// 	names.push_back(varName.c_str());
		// }

		return true;
	}

	bool LocalScopeStateNode::GetChildNode(std::string name, std::shared_ptr<StateNodeBase>& node)
	{
		// if (CaseInsensitiveEquals(name, "self"))
		// {
		// 	node = RuntimeState::CreateNodeForVariable("self", &m_stackFrame->Func->Proto);
			
		// 	return true;
		// }

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
