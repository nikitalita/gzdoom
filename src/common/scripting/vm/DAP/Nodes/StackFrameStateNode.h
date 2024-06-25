#pragma once

#include "common/scripting/vm/vmintern.h"

#include <dap/protocol.h>
#include "StateNodeBase.h"
#include "../PexCache.h"

namespace DebugServer
{
	class StackFrameStateNode : public StateNodeBase, public IStructuredState
	{
		VMFrame* m_stackFrame;

	public:
		explicit StackFrameStateNode(VMFrame* stackFrame);

		bool SerializeToProtocol(dap::StackFrame& stackFrame, PexCache* pexCache) const;

		bool GetChildNames(std::vector<std::string>& names) override;
		bool GetChildNode(std::string name, std::shared_ptr<StateNodeBase>& node) override;
	};
}
