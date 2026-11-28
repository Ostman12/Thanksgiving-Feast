// Feast Mode server: serves the client files over HTTP and the game over a
// WebSocket at /ws, on one port (docs/design.md).
//
//   feast_server [--port 8080] [--test-mode] [--no-around-the-table] [--client-dir client]
//
// Threading: Crow runs handlers on several threads. Every handler and the
// timer thread take `lock` before touching the RoomManager or the
// connection maps, so game state is only ever touched by one thread at a time.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <random>
#include <regex>
#include <string>
#include <thread>
#include <unordered_map>

#include <crow.h>

#include "server/room_manager.h"

namespace {

std::int64_t nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

struct Options {
  int port = 8080;
  bool testMode = false;
  bool aroundTheTable = true;  // --no-around-the-table turns Game 3 off
  std::string clientDir = "client";
};

Options parseArgs(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--test-mode") {
      opt.testMode = true;
    } else if (arg == "--no-around-the-table") {
      opt.aroundTheTable = false;
    } else if (arg == "--port" && i + 1 < argc) {
      opt.port = std::atoi(argv[++i]);
    } else if (arg == "--client-dir" && i + 1 < argc) {
      opt.clientDir = argv[++i];
    } else {
      std::cerr << "Usage: feast_server [--port N] [--test-mode] [--no-around-the-table]"
                   " [--client-dir DIR]\n";
      std::exit(2);
    }
  }
  return opt;
}

// Only called with a fixed name or one that passed kSafeName below, so the
// path can't escape the client directory. (Crow's sanitizing version would
// mangle the ':' and '\' in a Windows path like C:\...\client.)
crow::response serveFile(const std::string& path) {
  crow::response res;
  res.set_static_file_info_unsafe(path);
  res.add_header("Cache-Control", "no-cache");  // Fixes deploy without stale phones.
  return res;
}

}  // namespace

int main(int argc, char** argv) {
  const Options opt = parseArgs(argc, argv);

  // recursive_mutex: closing a socket can run its onclose handler right away
  // on the same thread, which then takes the lock again.
  std::recursive_mutex lock;
  feast::RoomManager manager(opt.testMode, std::random_device{}(), opt.aroundTheTable);
  std::unordered_map<crow::websocket::connection*, feast::ConnId> idOf;
  std::unordered_map<feast::ConnId, crow::websocket::connection*> connOf;
  feast::ConnId nextId = 1;  // 0 means "no connection" in RoomManager.

  // Caller holds `lock`. send_text only queues the write for Crow's network
  // thread, so calling it here is safe.
  auto deliver = [&](const feast::Actions& actions) {
    for (const feast::OutgoingText& s : actions.sends) {
      auto it = connOf.find(s.conn);
      if (it != connOf.end()) it->second->send_text(s.text);
    }
    for (feast::ConnId id : actions.closes) {
      auto it = connOf.find(id);
      if (it == connOf.end()) continue;
      crow::websocket::connection* conn = it->second;
      connOf.erase(it);
      idOf.erase(conn);
      conn->close("replaced by a newer connection");
    }
  };

  crow::SimpleApp app;
  app.loglevel(crow::LogLevel::Warning);

  CROW_WEBSOCKET_ROUTE(app, "/ws")
      .max_payload(4096)  // Our messages are tiny; refuse anything big.
      .onopen([&](crow::websocket::connection& conn) {
        std::lock_guard<std::recursive_mutex> guard(lock);
        feast::ConnId id = nextId++;
        idOf[&conn] = id;
        connOf[id] = &conn;
      })
      .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool isBinary) {
        std::lock_guard<std::recursive_mutex> guard(lock);
        auto it = idOf.find(&conn);
        if (it == idOf.end()) return;
        try {
          std::string text = isBinary ? std::string() : data;  // binary -> invalid message
          deliver(manager.handleMessage(it->second, text, nowMs()));
        } catch (const std::exception& e) {
          std::cerr << "error handling message: " << e.what() << "\n";
        }
      })
      .onclose([&](crow::websocket::connection& conn, const std::string&, std::uint16_t) {
        std::lock_guard<std::recursive_mutex> guard(lock);
        auto it = idOf.find(&conn);
        if (it == idOf.end()) return;  // Already replaced by a REJOIN.
        feast::ConnId id = it->second;
        idOf.erase(it);
        connOf.erase(id);
        try {
          deliver(manager.handleClose(id, nowMs()));
        } catch (const std::exception& e) {
          std::cerr << "error handling close: " << e.what() << "\n";
        }
      });

  // Client files. Only plain file names like "app.js" are served, so no
  // request can reach outside the client directory.
  const std::string dir = opt.clientDir + "/";
  CROW_ROUTE(app, "/")([dir] { return serveFile(dir + "index.html"); });
  CROW_ROUTE(app, "/display")([dir] { return serveFile(dir + "display.html"); });
  CROW_ROUTE(app, "/<string>")([dir](const std::string& name) {
    static const std::regex kSafeName(R"([A-Za-z0-9_-]+\.(html|js|css|svg|ico))");
    if (!std::regex_match(name, kSafeName)) return crow::response(404);
    return serveFile(dir + name);
  });

  // Timer thread: fire deadlines and clean up empty rooms about 4x a second.
  std::atomic<bool> running{true};
  std::thread timer([&] {
    while (running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      std::lock_guard<std::recursive_mutex> guard(lock);
      try {
        deliver(manager.tick(nowMs()));
      } catch (const std::exception& e) {
        std::cerr << "error in tick: " << e.what() << "\n";
      }
    }
  });

  std::cout << "Feast Mode on port " << opt.port << (opt.testMode ? " (test mode)" : "")
            << (opt.aroundTheTable ? "" : " (Around the Table off)") << ", serving "
            << opt.clientDir << std::endl;
  app.bindaddr("0.0.0.0").port(static_cast<std::uint16_t>(opt.port)).multithreaded().run();

  running = false;
  timer.join();
  return 0;
}
