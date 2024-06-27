#include "DebugServer.h"
#include <thread>
#include <functional>
#include <common/engine/printf.h>


namespace DebugServer
{
    DebugServer::DebugServer() { 
        terminate = false;
        restart_thread = std::thread(std::bind(&DebugServer::runRestartThread, this));
        debugger = std::unique_ptr<ZScriptDebugger>(new ZScriptDebugger());
    }

    void DebugServer::runRestartThread() {
        while (true){
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&] { return terminate; });
            terminate = false;
            debugger->EndSession();
        }
    }

    bool DebugServer::Listen()
    {
        constexpr int port = 19021;
        if (!m_server) {
            m_server = dap::net::Server::create();
        } else {
            m_server->stop();
        }

        auto onClientConnected =
        [&](const std::shared_ptr<dap::ReaderWriter>& connection) {
            std::shared_ptr<dap::Session> sess;
            sess = dap::Session::create();
            sess->bind(connection);
            // Registering a handle for when we send the DisconnectResponse;
            // After we send the disconnect response, the restart thread will stop the session and restart the server.
            sess->registerSentHandler(
                [&](const dap::ResponseOrError<dap::DisconnectResponse>&) {
                    std::unique_lock<std::mutex> lock(mutex);
                    terminate = true;
                    cv.notify_all();
            });
            debugger->StartSession(sess);
        };

        auto onError = [&](const char* msg) { 
            Printf("Server error: %s\n", msg);
        };

        m_server->start(port, onClientConnected, onError);
        return true;
    }

    DebugServer::~DebugServer()
    {
        m_server->stop();
        if (restart_thread.joinable()) {
            restart_thread.join();
        }
    }
}