#include "DebugExecutionManager.h"
#include <thread>
// #include "Window.h"
#include "GameInterfaces.h"
namespace DebugServer
{
	// using namespace RE::BSScript::Internal;
	static const char *pauseReasonStrings[] = {
		"none",
		"step",
		"breakpoint",
		"paused",
		"exception"};

	DebugExecutionManager::pauseReason DebugExecutionManager::CheckState(VMFrameStack *stack, VMReturn *ret, int numret, const VMOP *pc) {
		switch(m_state) {
			case DebuggerState::kPaused:
			{
				return pauseReason::paused;
			}
			case DebuggerState::kRunning:
			{
				if (m_breakpointManager->GetExecutionIsAtValidBreakpoint(stack, ret, numret, pc)) {
					return pauseReason::breakpoint;
				}
			} break;
			case DebuggerState::kStepping:
			{
				std::lock_guard<std::mutex> lock(m_instructionMutex);
				if (m_breakpointManager->GetExecutionIsAtValidBreakpoint(stack, ret, numret, pc)) {
					return pauseReason::breakpoint;
				} else if (!RuntimeState::GetStack(m_currentStepStackId)) {
					return pauseReason::CONTINUING;
				}
				else if (m_currentStepStackFrame) {
					std::vector<VMFrame *> currentFrames;
					RuntimeState::GetStackFrames(stack, currentFrames);
					// TODO: Handle granularity
					if (!currentFrames.empty()) {
						ptrdiff_t stepFrameIndex = -1;
						if (m_currentVMFunction && m_currentStepStackFrame->Func == m_currentVMFunction) {
							const auto stepFrameIter = std::find(currentFrames.begin(), currentFrames.end(), m_currentStepStackFrame);
							if (stepFrameIter != currentFrames.end()) {
								stepFrameIndex = std::distance(currentFrames.begin(), stepFrameIter);
							}
						}
						switch (m_currentStepType) {
							case StepType::STEP_IN:
								return pauseReason::step;
								break;
							case StepType::STEP_OUT:
								// If the stack exists, but the original frame is gone, we know we're in a previous frame now.
								if (stepFrameIndex == -1) {
									return pauseReason::step;
								}
								break;
							case StepType::STEP_OVER:
								if (stepFrameIndex <= 0) {
									return pauseReason::step;
								}
								break;
						}
					}
					// we deliberately don't set shouldContinue here in an else here, as we want to continue until we hit the next step point
				}
				else {
					// no more frames on stack, should continue
					if (!stack->HasFrames()) {
						return pauseReason::CONTINUING;
					}
				}
			} break;
		}
		return pauseReason::NONE;
	}

	void DebugExecutionManager::HandleInstruction(VMFrameStack *stack, VMReturn *ret, int numret, const VMOP *pc)
	{
		if (m_closed)
		{
			return;
		}


		pauseReason pauseReason = CheckState(stack, ret, numret, pc);
		switch(pauseReason){
			case pauseReason::NONE:
				break;
			case pauseReason::CONTINUING:
			{
				std::lock_guard<std::mutex> lock(m_instructionMutex);
				m_state = DebuggerState::kRunning;
				m_currentStepStackId = 0;
				m_currentStepStackFrame = nullptr;
				m_currentVMFunction = nullptr;
				if (m_session)
				{
					m_session->send(dap::ContinuedEvent{
									.allThreadsContinued = true,
									.threadId = 1});
				}
			} break;
			default: // not NONE
			{
				std::lock_guard<std::mutex> lock(m_instructionMutex);
				// `stack` is thread_local, we're currently on that thread,
				// and the debugger will be running in a separate thread, so we need to set it here.
				RuntimeState::m_GlobalVMStack = stack;
				m_state = DebuggerState::kPaused;
				m_currentStepStackId = 0;
				m_currentStepStackFrame = nullptr;
				m_currentVMFunction = nullptr;

				if (m_session)
				{
					m_session->send(dap::StoppedEvent{
									.reason = pauseReasonStrings[(int)pauseReason],
									.threadId = 1});
				}
				// Window::ReleaseFocus();

			} break;
		}

		while (m_state == DebuggerState::kPaused)
		{
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(100ms);
		}
		// If we were the thread that paused, regain focus
		if (pauseReason != pauseReason::NONE)
		{
			std::lock_guard<std::mutex> lock(m_instructionMutex);
			// Window::RegainFocus();
			// also reset the state
			m_runtimeState->Reset();
			if (m_state != DebuggerState::kRunning)
			{
				RuntimeState::m_GlobalVMStack = stack;
			}
		}
	}

	void DebugExecutionManager::Open(std::shared_ptr<dap::Session> ses)
	{
		std::lock_guard<std::mutex> lock(m_instructionMutex);
		m_closed = false;
		m_session = ses;
//		std::lock_guard<std::mutex> lock(m_instructionMutex);
	}

	void DebugExecutionManager::Close()
	{
		std::lock_guard<std::mutex> lock(m_instructionMutex);
		m_state = DebuggerState::kRunning;
		m_closed = true;
		m_session = nullptr;
	}

	bool DebugExecutionManager::Continue()
	{
		std::lock_guard<std::mutex> lock(m_instructionMutex);
		m_state = DebuggerState::kRunning;
		m_session->send(dap::ContinuedEvent());

		return true;
	}

	bool DebugExecutionManager::Pause()
	{
		if (m_state == DebuggerState::kPaused)
		{
			return false;
		}
		std::lock_guard<std::mutex> lock(m_instructionMutex);
		m_state = DebuggerState::kPaused;
		return true;
	}

	bool DebugExecutionManager::Step(uint32_t stackId, const StepType stepType)
	{
		if (m_state != DebuggerState::kPaused)
		{
			return false;
		}
		std::lock_guard<std::mutex> lock(m_instructionMutex);
		const auto stack = RuntimeState::GetStack(stackId);
		if (stack)
		{
			if (stack->HasFrames())
			{
				m_currentStepStackFrame = stack->TopFrame();
				if (m_currentStepStackFrame)
				{
					m_currentVMFunction = m_currentStepStackFrame->Func;
				}
			}
		}
		else
		{
			return false;
		}

		m_currentStepStackId = stackId;
		m_currentStepType = stepType;
		m_state = DebuggerState::kStepping;

		return true;
	}
}
