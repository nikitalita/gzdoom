#include "ZScriptDebugger.h"

#include <functional>
#include <string>
#include <dap/protocol.h>
#include <dap/session.h>

#include "Utilities.h"
#include "GameInterfaces.h"
#include "Nodes/StackStateNode.h"
#include "Nodes/StackFrameStateNode.h"

#include "Nodes/StateNodeBase.h"
#include "engineerrors.h"
#include "fmt/format.h"

#include <common/engine/filesystem.h>
#include <filesystem>

namespace DebugServer
{
	ZScriptDebugger::ZScriptDebugger()
	{
		m_pexCache = std::make_shared<PexCache>();

		m_breakpointManager = std::make_shared<BreakpointManager>(m_pexCache.get());

		m_idProvider = std::make_shared<IdProvider>();
		m_runtimeState = std::make_shared<RuntimeState>(m_idProvider);

		m_executionManager = std::make_shared<DebugExecutionManager>(m_runtimeState.get(), m_breakpointManager.get());

	}

	void ZScriptDebugger::StartSession(std::shared_ptr<dap::Session> session) {
		m_closed = false;
		m_session = session;
		m_executionManager->Open(session);
		m_createStackEventHandle =
			RuntimeEvents::SubscribeToCreateStack(std::bind(&ZScriptDebugger::StackCreated, this, std::placeholders::_1));

		m_cleanupStackEventHandle =
			RuntimeEvents::SubscribeToCleanupStack(std::bind(&ZScriptDebugger::StackCleanedUp, this, std::placeholders::_1));

		m_instructionExecutionEventHandle =
			RuntimeEvents::SubscribeToInstructionExecution(
				std::bind(&ZScriptDebugger::InstructionExecution, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));

		// m_initScriptEventHandle = RuntimeEvents::SubscribeToInitScript(std::bind(&ZScriptDebugger::InitScriptEvent, this, std::placeholders::_1));
		m_logEventHandle =
			RuntimeEvents::SubscribeToLog(std::bind(&ZScriptDebugger::EventLogged, this, std::placeholders::_1, std::placeholders::_2));

		m_breakpointChangedEventHandle =
			RuntimeEvents::SubscribeToBreakpointChanged(std::bind(&ZScriptDebugger::BreakpointChanged, this, std::placeholders::_1, std::placeholders::_2));

		RegisterSessionHandlers();
	}
	void ZScriptDebugger::EndSession() {
		m_executionManager->Close();
		m_session = nullptr;
		m_closed = true;

		RuntimeEvents::UnsubscribeFromLog(m_logEventHandle);
		// RuntimeEvents::UnsubscribeFromInitScript(m_initScriptEventHandle);
		RuntimeEvents::UnsubscribeFromInstructionExecution(m_instructionExecutionEventHandle);
		RuntimeEvents::UnsubscribeFromCreateStack(m_createStackEventHandle);
		RuntimeEvents::UnsubscribeFromCleanupStack(m_cleanupStackEventHandle);
		RuntimeEvents::UnsubscribeFromBreakpointChanged(m_breakpointChangedEventHandle);

		// clear session data
		m_projectArchive = "";
		m_projectPath = "";
		m_projectSources.clear();
		m_breakpointManager->ClearBreakpoints();
		
	}

	void ZScriptDebugger::RegisterSessionHandlers() {
		// The Initialize request is the first message sent from the client and the response reports debugger capabilities.
		// https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Initialize
		m_session->registerHandler([](const dap::InitializeRequest& request) {
			dap::InitializeResponse response;
			response.supportsConfigurationDoneRequest = true;
			response.supportsLoadedSourcesRequest = true;
			return response;
		});
		m_session->onError([this](const char* msg) {
			Printf(msg);
		});
		m_session->registerSentHandler(
			// After an intialize response is sent, we send an initialized event to indicate that the client can now send requests.
			[this](const dap::ResponseOrError<dap::InitializeResponse>&) {
				SendEvent(dap::InitializedEvent());
		});

		// Client is done configuring.
		m_session->registerHandler([this](const dap::ConfigurationDoneRequest&) {
			return dap::ConfigurationDoneResponse{};
		});

		// The Disconnect request is sent by the client before it disconnects from the server.
		// https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Disconnect
		m_session->registerHandler([this](const dap::DisconnectRequest&) {
			// Client wants to disconnect.
			return dap::DisconnectResponse{};
		});
		m_session->registerHandler([this](const dap::PDSLaunchRequest& request) {
			return Launch(request);
		});
		m_session->registerHandler([this](const dap::PDSAttachRequest& request) {
			return Attach(request);
		});
		m_session->registerHandler([this](const dap::PauseRequest& request) {
			return Pause(request);
		});
		m_session->registerHandler([this](const dap::ContinueRequest& request) {
			return Continue(request);
		});
		m_session->registerHandler([this](const dap::ThreadsRequest& request) {
			return GetThreads(request);
		});
		m_session->registerHandler([this](const dap::SetBreakpointsRequest& request) {
			return SetBreakpoints(request);
		});
		m_session->registerHandler([this](const dap::SetFunctionBreakpointsRequest& request) {
			return SetFunctionBreakpoints(request);
		});
		m_session->registerHandler([this](const dap::StackTraceRequest& request) {
			return GetStackTrace(request);
		});
		m_session->registerHandler([this](const dap::StepInRequest& request) {
			return StepIn(request);
		});
		m_session->registerHandler([this](const dap::StepOutRequest& request) {
			return StepOut(request);
		});
		m_session->registerHandler([this](const dap::NextRequest& request) {
			return Next(request);
		});
		m_session->registerHandler([this](const dap::ScopesRequest& request) {
			return GetScopes(request);
		});
		m_session->registerHandler([this](const dap::VariablesRequest& request) {
			return GetVariables(request);
		});
		m_session->registerHandler([this](const dap::SourceRequest& request) {
			return GetSource(request);
		});
		m_session->registerHandler([this](const dap::LoadedSourcesRequest& request) {
			return GetLoadedSources(request);
		});
	}

	dap::Error ZScriptDebugger::Error(const std::string &msg)
	{
		Printf("%s", msg.c_str());
		return dap::Error(msg);
	}

	template <typename T, typename>
	void ZScriptDebugger::SendEvent(const T& event) const{
		if (m_session)
			m_session->send(event);
	}

	std::string LogSeverityEnumStr(Severity severity) {
		if (severity == Severity::kInfo) {
			return std::string("INFO");
		} else if (severity == Severity::kWarning) {
			return std::string("WARNING");
		} else if (severity == Severity::kError) {
			return std::string("ERROR");
		}
		return std::string("UNKNOWN_ENUM_LEVEL");
	}

	// EVENTS
	void ZScriptDebugger::LogGameOutput(Severity severity, const std::string &msg) const {
		// constexpr const char* format = "GAME EVENT -- {}";
		// if (severity == Severity::kInfo) {
		// 	logger::info(format, msg);
		// }
		// else if (severity == Severity::kWarning) {
		// 	logger::warn(format, msg);
		// }
		// else if (severity == Severity::kError) {
		// 	logger::error(format, msg);
		// }
		// else if (severity == Severity::kFatal) {
		// 	logger::critical(format, msg);
		// }
	}

	void ZScriptDebugger::EventLogged(int severity, const char* msg) const
	{
		dap::OutputEvent output;
		output.category = "console";
		output.output = std::string(msg) + "\r\n";
		// LogGameOutput(logEvent->severity, output.output);
		SendEvent(output);
	}

	void ZScriptDebugger::StackCreated(VMFrameStack * stack)
	{
#if 0
		const auto stackId = 0; // only one stack
		SendEvent(dap::ThreadEvent{
				.reason = "started",
				.threadId = stackId
				});

#endif
	}
	
	void ZScriptDebugger::StackCleanedUp(uint32_t stackId)
	{
#if 0
		SendEvent(dap::ThreadEvent{
			.reason = "exited",
			.threadId = stackId
		});
#endif
	}

	void ZScriptDebugger::InstructionExecution(VMFrameStack *stack, VMReturn *ret, int numret, const VMOP *pc) const
	{
		m_executionManager->HandleInstruction(stack, ret, numret, pc);
	}

	void ZScriptDebugger::CheckSourceLoaded(const std::string &scriptName) const{
		if (!m_pexCache->HasScript(scriptName))
		{
			dap::Source source;
			if (!m_pexCache->GetSourceData(scriptName, source))
			{
				return;
			}
			// TODO: Get the modified times from the unlinked objects?
			auto ref = GetSourceReference(source);
			if (m_projectSources.find(ref) != m_projectSources.end()) {
				source = m_projectSources.at(ref);
			}
			SendEvent(dap::LoadedSourceEvent{
				.reason = "new",
				.source = source
				});
		}
	}

	void ZScriptDebugger::BreakpointChanged(const dap::Breakpoint& bpoint, const std::string& reason) const
	{
		// TODO: make this multi-threaded
		// XSE::GetTaskInterface()->AddTask([this, bpoint, reason]() {
			SendEvent(dap::BreakpointEvent{
				.breakpoint = bpoint,
				.reason = reason
				});
		// });
	}

	ZScriptDebugger::~ZScriptDebugger()
	{
		m_closed = true;

		RuntimeEvents::UnsubscribeFromLog(m_logEventHandle);
		// RuntimeEvents::UnsubscribeFromInitScript(m_initScriptEventHandle);
		RuntimeEvents::UnsubscribeFromInstructionExecution(m_instructionExecutionEventHandle);
		RuntimeEvents::UnsubscribeFromCreateStack(m_createStackEventHandle);
		RuntimeEvents::UnsubscribeFromCleanupStack(m_cleanupStackEventHandle);

		m_executionManager->Close();
	}
	
	dap::ResponseOrError<dap::InitializeResponse> ZScriptDebugger::Initialize(const dap::InitializeRequest& request)
	{
		return dap::ResponseOrError<dap::InitializeResponse>();
	}

	dap::ResponseOrError<dap::LaunchResponse> ZScriptDebugger::Launch(const dap::PDSLaunchRequest& request)
	{
		auto resp = Attach(dap::PDSAttachRequest{
			.name = request.name,
			.type = request.type,
			.request = request.request,
			.projectPath = request.projectPath,
			.projectArchive = request.projectArchive,
			.projectSources = request.projectSources
			});
		if (resp.error) {
			RETURN_DAP_ERROR(resp.error.message.c_str());
		}
		return dap::ResponseOrError<dap::LaunchResponse>();
	}


	dap::ResponseOrError<dap::AttachResponse> ZScriptDebugger::Attach(const dap::PDSAttachRequest& request)
	{
		m_projectPath = request.projectPath.value("");
		m_projectArchive = request.projectArchive.value("");
		if (!request.restart.has_value())
		{
			m_pexCache->Clear();
		}
		auto thing = GetLoadedSources(dap::LoadedSourcesRequest());
		std::filesystem::path projectPath = m_projectPath;
		if (projectPath.empty()) {
			RETURN_DAP_ERROR("No project path");
		}
		if (!std::filesystem::exists(projectPath)) {
			RETURN_DAP_ERROR("Project path does not exist");
		}
		if (!std::filesystem::is_directory(projectPath)) {
			RETURN_DAP_ERROR("Project path is not a directory");
		}
		for (const auto& entry : std::filesystem::recursive_directory_iterator(projectPath)) {
			if (entry.is_regular_file()) {
				std::string ext = entry.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
				if (ext == ".zs" || ext == ".zsc" || ext == ".zc" || ext == ".acs" || ext == ".dec") {
					std::string scriptName = entry.path().lexically_relative(projectPath);
					if (!m_projectArchive.empty()){
						scriptName = m_projectArchive + ":" + scriptName;
					}
					CheckSourceLoaded(scriptName);
				// check if the file is named DECORATE or ACS
				} else if (entry.path().filename() == "DECORATE" || entry.path().filename() == "ACS") {
					std::string scriptName = entry.path().lexically_relative(projectPath);
					if (!m_projectArchive.empty()){
						scriptName = m_projectArchive + ":" + scriptName;
					}
					CheckSourceLoaded(scriptName);
				}
			}
		}
		return dap::AttachResponse();
	}

	dap::ResponseOrError<dap::ContinueResponse> ZScriptDebugger::Continue(const dap::ContinueRequest& request)
	{
		if (m_executionManager->Continue())
			return dap::ContinueResponse();
		RETURN_DAP_ERROR("Could not Continue");
	}

	dap::ResponseOrError<dap::PauseResponse> ZScriptDebugger::Pause(const dap::PauseRequest& request)
	{
		if (m_executionManager->Pause())
			return dap::PauseResponse();
		RETURN_DAP_ERROR("Could not Pause");
	}

	dap::ResponseOrError<dap::ThreadsResponse> ZScriptDebugger::GetThreads(const dap::ThreadsRequest& request)
	{
		dap::ThreadsResponse response;
		// const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		// RE::BSSpinLockGuard lock(vm->runningStacksLock);

		// std::vector<std::string> stackIdPaths;

		// for (auto& elem : vm->allRunningStacks)
		// {
		// 	const auto stack = elem.second.get();
		// 	if (!stack || !stack->top)
		// 	{
		// 		continue;
		// 	}

		// 	stackIdPaths.push_back(std::to_string(stack->stackID));
		// }

		// for (auto& path : stackIdPaths)
		// {
		// 	std::shared_ptr<StateNodeBase> stateNode;
		// 	if (!m_runtimeState->ResolveStateByPath(path, stateNode))
		// 	{
		// 		continue;
		// 	}

		// 	const auto node = dynamic_cast<StackStateNode*>(stateNode.get());

		// 	if (node->SerializeToProtocol(thread))
		// 	{
		// 		response.threads.push_back(thread);
		// 	}
		// }
		dap::Thread thread;
		thread.id = 1;
		thread.name = "Main Thread";
		response.threads.push_back(thread);
		return response;
	}



	dap::ResponseOrError<dap::SetBreakpointsResponse> ZScriptDebugger::SetBreakpoints(const dap::SetBreakpointsRequest& request)
	{
		dap::Source source = request.source;
		std::filesystem::path src_path = source.path.value("");
		src_path = src_path.lexically_relative(m_projectPath);
		auto ref = GetScriptReference(m_projectArchive + ":" + src_path.string());
		if (m_projectSources.find(ref) != m_projectSources.end()) {
			// if (!CompareSourceModifiedTime(request.source, m_projectSources[ref])) {
			// 	RETURN_DAP_ERROR("Setting Breakpoints failed: script has been modified after load");
			// }
			source = m_projectSources[ref];
		} else if (ref > 0){
			// It's not part of the project's imported sources, they have to get the decompiled source from us,
			// So we set sourceReference to make the debugger request the source from us
			// TODO: Enable this when we start loading the project's sources
			source.sourceReference = ref;
		}
		return m_breakpointManager->SetBreakpoints(source, request.breakpoints.value(std::vector<dap::SourceBreakpoint>()));;
	}

	dap::ResponseOrError<dap::SetFunctionBreakpointsResponse> ZScriptDebugger::SetFunctionBreakpoints(const dap::SetFunctionBreakpointsRequest& request)
	{
		RETURN_DAP_ERROR("unimplemented");
	}
	dap::ResponseOrError<dap::StackTraceResponse> ZScriptDebugger::GetStackTrace(const dap::StackTraceRequest& request)
	{
		dap::StackTraceResponse response;
		// const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		// RE::BSSpinLockGuard lock(vm->runningStacksLock);

		if (request.threadId <= -1)
		{
			response.totalFrames = 0;
			RETURN_DAP_ERROR("No threadId specified");
		}
		auto frameVal = request.startFrame.value(0);
		auto levelVal = request.levels.value(0);
		std::vector<std::shared_ptr<StateNodeBase>> frameNodes;
		if (!m_runtimeState->ResolveChildrenByParentPath(std::to_string(request.threadId), frameNodes))
		{
			RETURN_DAP_ERROR("Could not find ThreadId");
		}
		uint32_t startFrame = static_cast<uint32_t>(frameVal > 0 ? frameVal : dap::integer(0));
		uint32_t levels = static_cast<uint32_t>(levelVal > 0 ? levelVal : dap::integer(0));

		for (uint32_t frameIndex = startFrame; frameIndex < frameNodes.size() && frameIndex < startFrame + levels; frameIndex++)
		{
			const auto node = dynamic_cast<StackFrameStateNode*>(frameNodes.at(frameIndex).get());

			dap::StackFrame frame;
			if (!node->SerializeToProtocol(frame, m_pexCache.get())) {
				RETURN_DAP_ERROR("Serialization error");
			}

			response.stackFrames.push_back(frame);
		}
		return response;
	}
	dap::ResponseOrError<dap::StepInResponse> ZScriptDebugger::StepIn(const dap::StepInRequest& request)
	{
		// TODO: Support `granularity` and `target`
		if (m_executionManager->Step(static_cast<uint32_t>(request.threadId), STEP_IN)) {
			return dap::StepInResponse();
		}
		RETURN_DAP_ERROR("Could not StepIn");
	}
	dap::ResponseOrError<dap::StepOutResponse> ZScriptDebugger::StepOut(const dap::StepOutRequest& request)
	{
		if (m_executionManager->Step(static_cast<uint32_t>(request.threadId), STEP_OUT)) {
			return dap::StepOutResponse();
		}
		RETURN_DAP_ERROR("Could not StepOut");
	}
	dap::ResponseOrError<dap::NextResponse> ZScriptDebugger::Next(const dap::NextRequest& request)
	{
		if (m_executionManager->Step(static_cast<uint32_t>(request.threadId), STEP_OVER)) {
			return dap::NextResponse();
		}
		RETURN_DAP_ERROR("Could not Next");
	}
	dap::ResponseOrError<dap::ScopesResponse> ZScriptDebugger::GetScopes(const dap::ScopesRequest& request)
	{
		dap::ScopesResponse response;
		// const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		// RE::BSSpinLockGuard lock(vm->runningStacksLock);

		std::vector<std::shared_ptr<StateNodeBase>> frameScopes;
		if (request.frameId < 0) {
			RETURN_DAP_ERROR(fmt::format("invalid frameId {}", static_cast<int64_t>(request.frameId)).c_str());
		}
		auto frameId = static_cast<uint32_t>(request.frameId);
		if (!m_runtimeState->ResolveChildrenByParentId(frameId, frameScopes)) {
			RETURN_DAP_ERROR( fmt::format("No scopes for frameId {}", frameId).c_str() );
		}

		for (const auto& frameScope : frameScopes)
		{
			auto asScopeSerializable = dynamic_cast<IProtocolScopeSerializable*>(frameScope.get());
			if (!asScopeSerializable)
			{
				continue;
			}

			dap::Scope scope;
			if (!asScopeSerializable->SerializeToProtocol(scope))
			{
				continue;
			}
			
			response.scopes.push_back(scope);
		}

		return response;
	}

	dap::ResponseOrError<dap::VariablesResponse> ZScriptDebugger::GetVariables(const dap::VariablesRequest& request)
	{
		dap::VariablesResponse response;

		// const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		// RE::BSSpinLockGuard lock(vm->runningStacksLock);

		std::vector<std::shared_ptr<StateNodeBase>> variableNodes;
		if (!m_runtimeState->ResolveChildrenByParentId(static_cast<uint32_t>(request.variablesReference), variableNodes)) {
			RETURN_DAP_ERROR(fmt::format("No such variable reference {}", static_cast<uint32_t>(request.variablesReference)).c_str());
		}

		// TODO: support `start`, `filter`, parameter
		int64_t count = 0;
		int64_t maxCount = request.count.value(variableNodes.size());
		for (const auto& variableNode : variableNodes)
		{
			if (count > maxCount) {
				break;
			}
			auto asVariableSerializable = dynamic_cast<IProtocolVariableSerializable*>(variableNode.get());
			if (!asVariableSerializable)
			{
				continue;
			}

			dap::Variable variable;
			if (!asVariableSerializable->SerializeToProtocol(variable))
			{
				continue;
			}
			
			response.variables.push_back(variable);
			count++;
		}

		return response;
	}
	dap::ResponseOrError<dap::SourceResponse> ZScriptDebugger::GetSource(const dap::SourceRequest& request)
	{
		if (!request.source.has_value() || !request.source.value().name.has_value()) {
			RETURN_DAP_ERROR("No source name");
		}
		std::string name = request.source.value().name.value();
		dap::SourceResponse response;
		if (m_pexCache->GetDecompiledSource(name, response.content)) {
			return response;
		}
		RETURN_DAP_ERROR(fmt::format("Could not find source {}", name).c_str());
	}
	dap::ResponseOrError<dap::LoadedSourcesResponse> ZScriptDebugger::GetLoadedSources(const dap::LoadedSourcesRequest& request)
	{
		dap::LoadedSourcesResponse response;
		int lump, lastlump = 0;

		while ((lump = fileSystem.FindLump("ZSCRIPT", &lastlump)) != -1)
		{
			dap::Source source;
			std::string path = fileSystem.GetFileFullPath(lump).c_str();
			if (m_pexCache->GetSourceData(path, source))
			{
				auto ref = GetSourceReference(source);
				// TODO: Get the modified times from the unlinked objects?
				if (m_projectSources.find(ref) != m_projectSources.end()) {
					response.sources.push_back(m_projectSources[ref]);
				} else {
					response.sources.push_back(source);
				}			
			}
		}
		return response;
	}
}
