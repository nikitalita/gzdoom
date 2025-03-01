#include "ValueStateNode.h"
#include "../Utilities.h"
#include "vm.h"

namespace DebugServer
{
	ValueStateNode::ValueStateNode(std::string name, const VMValue* variable, int type) :
		m_name(name), m_variable(variable), m_type(type)
	{
	}

	bool ValueStateNode::SerializeToProtocol(dap::Variable& variable)
	{
		variable.name = m_name;

		if (m_type == REGT_STRING){
				variable.type = "string";
				variable.value = StringFormat("\"%s\"", m_variable->s().GetChars());
		} else if (m_type == REGT_INT){
				variable.type = "int";
				variable.value = StringFormat("%d", m_variable->i);
		} else if (m_type == REGT_FLOAT){
				variable.type = "float";
				variable.value = StringFormat("%f", m_variable->f);
		} else if (m_type == REGT_NIL){
				variable.type = "None";
				variable.value = "NULL";
		} else if (m_type == REGT_POINTER){
				variable.type = "pointer";
				variable.value = StringFormat(m_variable->i ? "0x%08X" : "NULL", m_variable->i);
		} else {
			// assume biggest
			variable.type = "BigInt";
			variable.value = StringFormat("%d", m_variable->biggest);
		}
		return true;
	}
}
