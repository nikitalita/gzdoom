#pragma once


#include "GameInterfaces.h"

#include "BreakpointManager.h"
#include "RuntimeState.h"
#include <dap/session.h>

#include <mutex>

namespace DebugServer
{
	enum StepType {
		STEP_IN = 0,
		STEP_OVER,
		STEP_OUT
	};


	class DebugExecutionManager
	{
		enum class DebuggerState
		{
			kRunning = 0,
			kPaused,
			kStepping
		};
		enum class pauseReason
		{
			NONE = 0,
			step,
			breakpoint,
			paused,
			exception
		};

		std::mutex m_instructionMutex;
		bool m_closed;

		std::shared_ptr<dap::Session> m_session;
		RuntimeState* m_runtimeState;
		BreakpointManager* m_breakpointManager;

		DebuggerState m_state = DebuggerState::kRunning;
		uint32_t m_currentStepStackId = 0;
		StepType m_currentStepType = StepType::STEP_IN;
		VMFrame* m_currentStepStackFrame;
    VMFunction* m_currentVMFunction;
	public:
		explicit DebugExecutionManager(RuntimeState* runtimeState,
									   BreakpointManager* breakpointManager)
			: m_closed(true), m_runtimeState(runtimeState),
			  m_breakpointManager(breakpointManager), m_currentStepStackFrame(nullptr)
		{
		}

		void Close();
		void HandleInstruction(VMFrameStack *stack, VMReturn *ret, int numret, const VMOP *pc);
		void Open(std::shared_ptr<dap::Session> ses);
		bool Continue();
		bool Pause();
		bool Step(uint32_t stackId, StepType stepType);
	};
}
