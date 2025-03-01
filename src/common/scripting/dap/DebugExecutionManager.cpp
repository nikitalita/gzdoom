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
	void DebugExecutionManager::HandleInstruction(VMFrameStack *stack, VMReturn *ret, int numret, const VMOP *pc)
	{
		std::lock_guard<std::mutex> lock(m_instructionMutex);

		if (m_closed)
		{
			return;
		}

		bool shouldContinue = false;
		bool shouldSendEvent = false;
		pauseReason pauseReason = pauseReason::NONE;
		DebuggerState new_state = m_state;

		if (m_state == DebuggerState::kPaused)
		{
			pauseReason = pauseReason::paused;
		}
		else if (m_breakpointManager->GetExecutionIsAtValidBreakpoint(stack, ret, numret, pc))
		{
			pauseReason = pauseReason::breakpoint;
		}
		else if (m_state == DebuggerState::kStepping && !RuntimeState::GetStack(m_currentStepStackId))
		{
			shouldContinue = true;
		}
		else if (m_state == DebuggerState::kStepping)
		{
			if (m_currentStepStackFrame)
			{
				std::vector<VMFrame *> currentFrames;
				RuntimeState::GetStackFrames(stack, currentFrames);
				// TODO: Handle granularity
				if (!currentFrames.empty())
				{
					ptrdiff_t stepFrameIndex = -1;
					if (m_currentVMFunction && m_currentStepStackFrame->Func == m_currentVMFunction)
					{
						const auto stepFrameIter = std::find(currentFrames.begin(), currentFrames.end(), m_currentStepStackFrame);

						if (stepFrameIter != currentFrames.end())
						{
							stepFrameIndex = std::distance(currentFrames.begin(), stepFrameIter);
						}
					}
					switch (m_currentStepType)
					{
					case StepType::STEP_IN:
						pauseReason = pauseReason::step;
						break;
					case StepType::STEP_OUT:
						// If the stack exists, but the original frame is gone, we know we're in a previous frame now.
						if (stepFrameIndex == -1)
						{
							pauseReason = pauseReason::step;
						}
						break;
					case StepType::STEP_OVER:
						if (stepFrameIndex <= 0)
						{
							pauseReason = pauseReason::step;
						}
						break;
					}
				}
			}
			else
			{
				// no more frames on stack, should continue
				if (!stack->HasFrames())
				{
					shouldContinue = true;
				}
			}
		}

		if (pauseReason != pauseReason::NONE)
		{
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
		}
		else if (shouldContinue)
		{
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
		}

		while (m_state == DebuggerState::kPaused && !m_closed)
		{
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(100ms);
		}
		// If we were the thread that paused, regain focus
		if (pauseReason != pauseReason::NONE)
		{
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
		m_closed = false;
		m_session = ses;
		std::lock_guard<std::mutex> lock(m_instructionMutex);
	}

	void DebugExecutionManager::Close()
	{
		// Set m_closed to true to allow HandleInstruction to wake up.
		m_closed = true;
		// Lock the mutex so we wait until the HandleInstruction thread is done.

		std::lock_guard<std::mutex> lock(m_instructionMutex);
		m_state = DebuggerState::kRunning;
		// Now it should be safe to end the session.
		m_session = nullptr;
	}

	bool DebugExecutionManager::Continue()
	{
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

		m_state = DebuggerState::kPaused;
		return true;
	}

	bool DebugExecutionManager::Step(uint32_t stackId, const StepType stepType)
	{
		if (stackId < 0)
		{
			return false;
		}

		if (m_state != DebuggerState::kPaused)
		{
			return false;
		}

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

		m_state = DebuggerState::kStepping;
		m_currentStepStackId = stackId;
		m_currentStepType = stepType;

		return true;
	}
}
