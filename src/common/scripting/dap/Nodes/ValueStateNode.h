#pragma once

#include <common/scripting/dap/GameInterfaces.h>

#include <dap/protocol.h>
#include "StateNodeBase.h"

namespace DebugServer
{
	class ValueStateNode : public StateNodeBase, public IProtocolVariableSerializable
	{
		std::string m_name;
		const VMValue* m_variable;
		const int m_type;

	public:
		ValueStateNode(std::string name, const VMValue* variable, const int type);
		bool SerializeToProtocol(dap::Variable& variable) override;
	};
}
