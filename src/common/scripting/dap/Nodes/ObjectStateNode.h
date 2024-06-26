#pragma once
#include <common/scripting/dap/GameInterfaces.h>
#include <dap/protocol.h>

#include "StateNodeBase.h"
#include <map>

namespace DebugServer
{
	class ObjectStateNode : public StateNodeBase, public IProtocolVariableSerializable, public IStructuredState
	{
		std::string m_name;
		bool m_subView;
		
		const VMValue m_value;
		PType* m_class;
		caseless_path_map<std::shared_ptr<StateNodeBase>> m_children;
	public:
		ObjectStateNode(std::string name, VMValue value, PType* asClass, bool subView = false);

		bool SerializeToProtocol(dap::Variable& variable) override;

		bool GetChildNames(std::vector<std::string>& names) override;
		bool GetChildNode(std::string name, std::shared_ptr<StateNodeBase>& node) override;
		void Reset();
	};
}
