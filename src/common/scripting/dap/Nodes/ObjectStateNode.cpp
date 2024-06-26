
#include "ObjectStateNode.h"
#include "common/scripting/dap/GameInterfaces.h"
#include <common/scripting/dap/Utilities.h>
#include <common/scripting/dap/RuntimeState.h>
#include <common/objects/dobject.h>
#include <memory>

namespace DebugServer
{

	ObjectStateNode::ObjectStateNode(const std::string name, VMValue value, PType* asClass, const bool subView) :
		m_name(name), m_subView(subView), m_value(value), m_class(asClass)
	{
	}

	bool ObjectStateNode::SerializeToProtocol(dap::Variable& variable)
	{
		variable.variablesReference = IsVMValueValid(&m_value) ? GetId() : 0;
		
		variable.name = m_name;
		variable.type = m_class->mDescriptiveName.GetChars();

		std::vector<std::string> childNames;
		GetChildNames(childNames);

		variable.namedVariables = childNames.size();
		auto typeval = variable.type.value("");
		// check if name begins with 'Pointer<'; if so, remove it, and the trailing '>'
		if (typeval.size() > 9 && typeval.find("Pointer<") == 0 && typeval[typeval.size() - 1] == '>')
		{
			typeval = typeval.substr(8, typeval.size() - 9);
		}
		if (!m_subView)
		{
			// TODO: turn this back on
			// if(!IsVMValueValid(&m_value)) {
			if(!m_value.a) {
				variable.value = fmt::format("{} <NULL>", typeval);
			} else {
				variable.value = fmt::format("{} (0x{:08x})", typeval, (uint64_t) m_value.a);
			}
		}
		else
		{
			variable.value = typeval;
		}
		
		return true;
	}

	bool ObjectStateNode::GetChildNames(std::vector<std::string>& names)
	{
		if (m_children.size() > 0)
		{
			for (auto& pair : m_children)
			{
				names.push_back(pair.first);
			}
			return true;
		}
		auto p_type = m_class;
		if (p_type->isObjectPointer()){
			p_type = p_type->toPointer()->PointedType;
		}
		if (p_type->isClass()){
			auto classType = PType::toClass(p_type);
			auto descriptor = classType->Descriptor;
			DObject* dobject = IsVMValValidDObject(&m_value) ? static_cast<DObject*>(m_value.a) : nullptr;
			for (auto field : descriptor->Fields){
				auto name = field->SymbolName.GetChars();
				if (!dobject) {
					m_children[name] = RuntimeState::CreateNodeForVariable(name, VMValue(), field->Type);
				} else {
					auto child_val_ptr = GetVMValueVar(dobject, field->SymbolName, field->Type);
					m_children[name] = RuntimeState::CreateNodeForVariable(name,child_val_ptr, field->Type);
				}
				names.push_back(name);
			}
			return true;
		}
		LogError("Failed to get child names for object '{}' of type {}", m_name.c_str(), p_type->mDescriptiveName.GetChars());
		return false;
	}

	bool ObjectStateNode::GetChildNode(std::string name, std::shared_ptr<StateNodeBase>& node)
	{
		if (m_children.empty())
		{
			std::vector<std::string> names;
			GetChildNames(names);
		}
		if (m_children.find(name) != m_children.end())
		{
			node = m_children[name];
			return true;
		}
		LogError("Failed to get child node '{}' for object '{}'", name, m_name);
		return false;
	}

	void ObjectStateNode::Reset()
	{
		m_children.clear();
	}
}
