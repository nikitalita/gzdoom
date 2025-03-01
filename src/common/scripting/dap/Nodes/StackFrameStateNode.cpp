#include "StackFrameStateNode.h"

#include <common/scripting/dap/Utilities.h>
#include <string>

#include "LocalScopeStateNode.h"
#include "RegistersScopeStateNode.h"

namespace DebugServer
{
	StackFrameStateNode::StackFrameStateNode(VMFrame* stackFrame) : m_stackFrame(stackFrame)
	{

	}

	bool StackFrameStateNode::SerializeToProtocol(dap::StackFrame& stackFrame, PexCache* pexCache ) const
	{
		stackFrame.id = GetId();
		dap::Source source;
		std::string ScriptName = " <Native>";
		if (IsFunctionNative(m_stackFrame->Func))
		{
			stackFrame.name = m_stackFrame->Func->PrintableName;
      stackFrame.name += " " + ScriptName;
			return true;
		}
		auto scriptFunction = static_cast<VMScriptFunction*>(m_stackFrame->Func);
		ScriptName = scriptFunction->SourceFileName.GetChars();
		if (pexCache->GetSourceData(ScriptName.c_str(), source))
		{
			stackFrame.source = source;
      if (m_stackFrame->PC){
        uint32_t lineNumber =scriptFunction->PCToLine(m_stackFrame->PC);
        if (lineNumber)
        {
          stackFrame.line = lineNumber;
          stackFrame.column = 1;
        }
//        stackFrame.instructionPointerReference = GetIPRefFromFrame(m_stackFrame);
        stackFrame.instructionPointerReference = StringFormat("%p", m_stackFrame->PC);
      }
		}
		// TODO: Something with state pointer if we can get it?
		// if (strcmp(m_stackFrame->owningFunction->GetStateName().c_str(), "") != 0)
		// {
		// 	name = StringFormat("%s (%s)", name.c_str(), m_stackFrame->owningFunction->GetStateName().c_str());
		// }

		stackFrame.name = m_stackFrame->Func->PrintableName;

		return true;
	}

	bool StackFrameStateNode::GetChildNames(std::vector<std::string>& names)
	{
		auto scriptFunction = dynamic_cast<VMScriptFunction*>(m_stackFrame->Func);
		if (scriptFunction)
		{
			names.push_back("Local");
		}
    names.push_back("Registers");

		return true;
	}

	bool StackFrameStateNode::GetChildNode(std::string name, std::shared_ptr<StateNodeBase>& node)
	{
    if (CaseInsensitiveEquals(name, "Registers"))
    {
        node = std::make_shared<RegistersScopeStateNode>(m_stackFrame);
        return true;
    }
		auto scriptFunction = dynamic_cast<VMScriptFunction*>(m_stackFrame->Func);
		if (scriptFunction && CaseInsensitiveEquals(name, "local"))
		{
			node = std::make_shared<LocalScopeStateNode>(m_stackFrame);
			return true;
		}

		return false;
	}
}
