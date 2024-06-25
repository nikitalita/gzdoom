#include "StackFrameStateNode.h"

#include "../Utilities.h"
#include <string>

#include "LocalScopeStateNode.h"

namespace DebugServer
{
	StackFrameStateNode::StackFrameStateNode(VMFrame* stackFrame) : m_stackFrame(stackFrame)
	{

	}

	bool StackFrameStateNode::SerializeToProtocol(dap::StackFrame& stackFrame, PexCache* pexCache ) const
	{
		stackFrame.id = GetId();
		dap::Source source;
		std::string ScriptName = "<Native>";
		// TODO: ignoring this for now, just for debugging reference
		// check if we can cast m_stackFrame->Func to a VMScriptFunction
		auto scriptFunction = dynamic_cast<VMScriptFunction*>(m_stackFrame->Func);
		if (scriptFunction)
		{
			ScriptName = scriptFunction->SourceFileName.GetChars();
		}
		else
		{
			stackFrame.name = m_stackFrame->Func->PrintableName;
			return true;
		}
		if (pexCache->GetSourceData(ScriptName.c_str(), source))
		{
			stackFrame.source = source;
			uint32_t lineNumber =scriptFunction->PCToLine(m_stackFrame->PC);
			if (lineNumber)
			{
				stackFrame.line = lineNumber;
			}
		}

		auto name = std::string(m_stackFrame->Func->PrintableName);
		// if (strcmp(m_stackFrame->owningFunction->GetStateName().c_str(), "") != 0)
		// {
		// 	name = StringFormat("%s (%s)", name.c_str(), m_stackFrame->owningFunction->GetStateName().c_str());
		// }

		stackFrame.name = name;

		return true;
	}

	bool StackFrameStateNode::GetChildNames(std::vector<std::string>& names)
	{
		auto scriptFunction = dynamic_cast<VMScriptFunction*>(m_stackFrame->Func);
		if (scriptFunction)
		{
			names.push_back("Local");
		}

		return true;
	}

	bool StackFrameStateNode::GetChildNode(std::string name, std::shared_ptr<StateNodeBase>& node)
	{
		auto scriptFunction = dynamic_cast<VMScriptFunction*>(m_stackFrame->Func);
		if (scriptFunction && CaseInsensitiveEquals(name, "local"))
		{
			node = std::make_shared<LocalScopeStateNode>(m_stackFrame);
			return true;
		}

		return false;
	}
}
