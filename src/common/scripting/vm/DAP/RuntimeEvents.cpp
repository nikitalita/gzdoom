#define XBYAK_NO_OP_NAMES

#include "RuntimeEvents.h"


#include <cassert>
#include <mutex>
#include <dap/protocol.h>
#include "GameInterfaces.h"

namespace DebugServer
{
	namespace RuntimeEvents
	{
#define EVENT_WRAPPER_IMPL(NAME, HANDLER_SIGNATURE) \
		eventpp::CallbackList<HANDLER_SIGNATURE> g_##NAME##Event; \
		\
		NAME##EventHandle SubscribeTo##NAME(std::function<HANDLER_SIGNATURE> handler) \
		{ \
			return g_##NAME##Event.append(handler); \
		} \
		\
		bool UnsubscribeFrom##NAME(NAME##EventHandle handle) \
		{ \
			return g_##NAME##Event.remove(handle); \
		} \

		EVENT_WRAPPER_IMPL(InstructionExecution, void(VMFrameStack *stack, VMReturn *ret, int numret, const VMOP *pc))
		EVENT_WRAPPER_IMPL(CreateStack, void(VMFrameStack *))
		EVENT_WRAPPER_IMPL(CleanupStack, void(uint32_t))
		// EVENT_WRAPPER_IMPL(InitScript, void(RE::TESInitScriptEvent*))
		EVENT_WRAPPER_IMPL(Log, void(int level, const char * msg))
		EVENT_WRAPPER_IMPL(BreakpointChanged, void(const dap::Breakpoint& bpoint, const std::string&))
#undef EVENT_WRAPPER_IMPL
		
		//class ScriptInitEventSink : public RE::BSTEventSink<RE::TESInitScriptEvent>
		//{
		//	RE::EventResult ReceiveEvent(RE::TESInitScriptEvent* evn, RE::BSTEventSource<RE::TESInitScriptEvent>* a_eventSource) override
		//	{
		//		g_InitScriptEvent(evn);

		//		return RE::EventResult::kContinue;
		//	};
		//};

		// class LogEventSink : public RE::BSTEventSink<RE::BSScript::LogEvent>
		// {
		// 	using EventResult = RE::BSEventNotifyControl;
		// 	#ifdef SKYRIM
		// 	EventResult ProcessEvent(const RE::BSScript::LogEvent* a_event, RE::BSTEventSource<RE::BSScript::LogEvent>*) override
		// 	{
		// 		g_LogEvent(a_event);
		// 		return RE::BSEventNotifyControl::kContinue;
		// 	};
		// 	#else // FALLOUT
		// 	EventResult ProcessEvent(const RE::BSScript::LogEvent& a_event, RE::BSTEventSource<RE::BSScript::LogEvent>*) override
		// 	{
		// 		g_LogEvent(&a_event);
		// 		return RE::BSEventNotifyControl::kContinue;
		// 	};
		// 	#endif

		// };

	void EmitBreakpointChangedEvent(const dap::Breakpoint& bpoint, const std::string& what)
	{
		g_BreakpointChangedEvent(bpoint, what);
	}
	void EmitInstructionExecutionEvent(VMFrameStack *stack, VMReturn *ret, int numret, const VMOP *pc)
	{
		g_InstructionExecutionEvent(stack, ret, numret, pc);
	}

}
}
