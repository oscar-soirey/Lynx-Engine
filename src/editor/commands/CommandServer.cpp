#include "CommandServer.h"

#include "CommandRegistry.h"

#include <chrono>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace lynx::editor::command_server
{
	namespace
	{
#ifdef _WIN32
		using Socket = SOCKET;
		const Socket kInvalidSocket = INVALID_SOCKET;

		void CloseSocket(Socket s) { closesocket(s); }

		bool WouldBlock()
		{
			const int error = WSAGetLastError();
			return error == WSAEWOULDBLOCK || error == WSAEINTR;
		}

		void SetNonBlocking(Socket s)
		{
			u_long mode = 1;
			ioctlsocket(s, FIONBIO, &mode);
		}

		int ProcessId() { return static_cast<int>(GetCurrentProcessId()); }
#else
		using Socket = int;
		const Socket kInvalidSocket = -1;

		void CloseSocket(Socket s) { close(s); }

		bool WouldBlock()
		{
			return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
		}

		void SetNonBlocking(Socket s)
		{
			const int flags = fcntl(s, F_GETFL, 0);
			fcntl(s, F_SETFL, flags | O_NONBLOCK);
		}

		int ProcessId() { return static_cast<int>(getpid()); }
#endif

		using commands::Json;
		using Clock = std::chrono::steady_clock;

		// A line longer than this is refused (asset.write sends whole files).
		constexpr size_t kMaxLineSize = 256u * 1024u * 1024u;

		struct Client
		{
			int id = 0;
			Socket socket = kInvalidSocket;
			std::string input;
			std::string output;
			size_t output_sent = 0;
			bool authenticated = false;
			bool closing = false;      // close once the output is sent
			bool dead = false;
			int open_groups = 0;       // undo.begin_group not ended yet
		};

		Socket g_listen = kInvalidSocket;
		int g_port = 0;
		std::string g_token;
		fs::path g_connection_file;
		bool g_wsa_started = false;

		std::map<int, std::unique_ptr<Client>> g_clients;
		bool g_yield = false;
		int g_next_client = 1;

		std::string MakeToken()
		{
			std::random_device device;
			std::mt19937_64 generator(
				(static_cast<uint64_t>(device()) << 32) ^ device() ^
				static_cast<uint64_t>(Clock::now().time_since_epoch().count()));

			static const char kHex[] = "0123456789abcdef";
			std::string token;

			for (int i = 0; i < 32; ++i)
				token += kHex[generator() & 15u];

			return token;
		}

		void Send(Client& client, const Json& message)
		{
			client.output += message.dump(-1, ' ', false, Json::error_handler_t::replace);
			client.output += '\n';
		}

		void Reply(int client_id, const Json& id, bool ok, Json value)
		{
			auto it = g_clients.find(client_id);

			// Disconnected meanwhile (async command) : nothing to do.
			if (it == g_clients.end() || it->second->dead)
				return;

			Json message = { { "id", id }, { "ok", ok } };
			message[ok ? "result" : "error"] = std::move(value);

			Send(*it->second, message);
		}

		void HandleLine(Client& client, const std::string& line)
		{
			if (line.empty() || line == "\r")
				return;

			Json request;

			try
			{
				request = Json::parse(line);
			}
			catch (const Json::exception& error)
			{
				Send(client, { { "id", nullptr }, { "ok", false }, { "error", std::string("invalid JSON : ") + error.what() } });
				return;
			}

			const Json id = request.contains("id") ? request["id"] : Json(nullptr);

			if (!request.is_object() || !request.contains("command") || !request["command"].is_string())
			{
				Send(client, { { "id", id }, { "ok", false }, { "error", "a request is {\"id\":.., \"command\": \"name\", \"params\": {..}}" } });
				return;
			}

			const std::string command = request["command"].get<std::string>();
			const Json params = (request.contains("params") && request["params"].is_object())
				? request["params"] : Json::object();

			// --- Connection commands ------------------------------------------

			if (command == "auth")
			{
				const bool ok =
					params.contains("token") && params["token"].is_string() &&
					params["token"].get<std::string>() == g_token;

				client.authenticated = client.authenticated || ok;

				if (ok)
					Send(client, { { "id", id }, { "ok", true }, { "result", { { "client", client.id } } } });
				else
					Send(client, { { "id", id }, { "ok", false }, { "error", "invalid token" } });

				return;
			}

			if (!client.authenticated)
			{
				Send(client, { { "id", id }, { "ok", false }, { "error", "not authenticated : send {\"command\":\"auth\",\"params\":{\"token\":..}} first (token in .lynx/editor.json)" } });
				return;
			}

			if (command == "ping")
			{
				Send(client, { { "id", id }, { "ok", true }, { "result", "pong" } });
				return;
			}

			if (command == "bye")
			{
				Send(client, { { "id", id }, { "ok", true }, { "result", Json::object() } });
				client.closing = true;
				return;
			}

			// --- Editor commands ----------------------------------------------

			commands::CommandContext context;
			context.client = client.id;
			context.source = "tcp:" + std::to_string(client.id);

			if (command != "help")
				commands::Log("[" + context.source + "] " + command);

			const int client_id = client.id;

			commands::Execute(command, params, context,
				[client_id, id, command](bool ok, Json value)
				{
					auto it = g_clients.find(client_id);

					if (ok && it != g_clients.end())
					{
						if (command == "undo.begin_group")
							++it->second->open_groups;
						else if (command == "undo.end_group" && it->second->open_groups > 0)
							--it->second->open_groups;
					}

					if (!ok)
						commands::Log("    error : " + (value.is_string() ? value.get<std::string>() : value.dump()));

					Reply(client_id, id, ok, std::move(value));
				});
		}

		// Reads what arrived. false = connection closed / broken.
		bool Receive(Client& client)
		{
			char buffer[65536];

			for (;;)
			{
				const int received = static_cast<int>(recv(client.socket, buffer, sizeof(buffer), 0));

				if (received > 0)
				{
					client.input.append(buffer, static_cast<size_t>(received));

					if (client.input.size() > kMaxLineSize)
					{
						Send(client, { { "id", nullptr }, { "ok", false }, { "error", "request too big" } });
						client.closing = true;
						client.input.clear();
						return true;
					}

					continue;
				}

				if (received == 0)
					return false;

				return WouldBlock();
			}
		}

		bool Flush(Client& client)
		{
			while (client.output_sent < client.output.size())
			{
				const size_t left = client.output.size() - client.output_sent;
				const int chunk = static_cast<int>(std::min<size_t>(left, 1u << 20));

#ifdef MSG_NOSIGNAL
				const int flags = MSG_NOSIGNAL;
#else
				const int flags = 0;
#endif
				const int sent = static_cast<int>(
					send(client.socket, client.output.data() + client.output_sent, chunk, flags));

				if (sent > 0)
				{
					client.output_sent += static_cast<size_t>(sent);
					continue;
				}

				if (sent < 0 && WouldBlock())
					break;

				return false;
			}

			if (client.output_sent >= client.output.size())
			{
				client.output.clear();
				client.output_sent = 0;
			}
			else if (client.output_sent > (1u << 20))
			{
				client.output.erase(0, client.output_sent);
				client.output_sent = 0;
			}

			return true;
		}

		void Disconnect(Client& client)
		{
			if (client.dead)
				return;

			client.dead = true;

			// Groups left open by a script that stopped / crashed.
			while (client.open_groups > 0)
			{
				--client.open_groups;
				commands::EndUndoGroup();
			}

			if (client.socket != kInvalidSocket)
				CloseSocket(client.socket);

			client.socket = kInvalidSocket;

			commands::Log("[tcp:" + std::to_string(client.id) + "] disconnected");
		}

		void WriteConnectionFile(const fs::path& project_root)
		{
			Json info = {
				{ "port", g_port },
				{ "token", g_token },
				{ "pid", ProcessId() },
				{ "project", project_root.generic_string() },
				{ "project_name", project_root.filename().string() },
				{ "protocol", 1 },
			};

			std::error_code ec;

			g_connection_file = project_root / ".lynx" / "editor.json";
			fs::create_directories(g_connection_file.parent_path(), ec);

			{
				std::ofstream file(g_connection_file, std::ios::trunc);
				file << info.dump(4) << "\n";
			}

			const fs::path last = LastEditorFile();

			if (!last.empty())
			{
				fs::create_directories(last.parent_path(), ec);
				std::ofstream file(last, std::ios::trunc);
				file << info.dump(4) << "\n";
			}
		}

		void RemoveConnectionFiles()
		{
			std::error_code ec;

			if (!g_connection_file.empty())
				fs::remove(g_connection_file, ec);

			// The "last editor" file may belong to another editor started after.
			const fs::path last = LastEditorFile();

			try
			{
				std::ifstream file(last);

				if (file)
				{
					const Json info = Json::parse(file);
					file.close();

					if (info.value("pid", 0) == ProcessId())
						fs::remove(last, ec);
				}
			}
			catch (...)
			{
			}
		}
	}


	fs::path ConnectionFile()
	{
		return g_connection_file;
	}


	fs::path LastEditorFile()
	{
		std::error_code ec;
		const fs::path temp = fs::temp_directory_path(ec);

		if (ec)
			return {};

		return temp / "LynxEditor" / "last_editor.json";
	}


	bool Start(const fs::path& project_root, int preferred_port)
	{
		if (g_listen != kInvalidSocket)
			return true;

#ifdef _WIN32
		if (!g_wsa_started)
		{
			WSADATA data;

			if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
			{
				std::cerr << "[COMMANDS] WSAStartup failed : no command server\n";
				return false;
			}

			g_wsa_started = true;
		}
#endif

		for (int port = preferred_port; port < preferred_port + 20; ++port)
		{
			Socket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

			if (s == kInvalidSocket)
				break;

#ifdef _WIN32
			// Two editors must not share a port.
			BOOL exclusive = TRUE;
			setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#else
			int reuse = 1;
			setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_port = htons(static_cast<uint16_t>(port));
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // local only

			if (bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 &&
				listen(s, 8) == 0)
			{
				SetNonBlocking(s);

				g_listen = s;
				g_port = port;
				break;
			}

			CloseSocket(s);
		}

		if (g_listen == kInvalidSocket)
		{
			std::cerr << "[COMMANDS] No free port from " << preferred_port << " : no command server\n";
			return false;
		}

		g_token = MakeToken();
		WriteConnectionFile(project_root);

		std::cout << "[COMMANDS] Command server on 127.0.0.1:" << g_port << "\n";
		commands::Log("Command server on 127.0.0.1:" + std::to_string(g_port));

		return true;
	}


	void Poll(double budget_ms)
	{
		if (g_listen == kInvalidSocket)
			return;

		const Clock::time_point start = Clock::now();
		g_yield = false;

		const auto over_budget = [&]()
		{
			return g_yield ||
			       std::chrono::duration<double, std::milli>(Clock::now() - start).count() >= budget_ms;
		};

		// New clients.
		for (;;)
		{
			sockaddr_in address{};
#ifdef _WIN32
			int length = sizeof(address);
#else
			socklen_t length = sizeof(address);
#endif
			const Socket s = accept(g_listen, reinterpret_cast<sockaddr*>(&address), &length);

			if (s == kInvalidSocket)
				break;

			SetNonBlocking(s);

			int no_delay = 1;
			setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&no_delay), sizeof(no_delay));

			auto client = std::make_unique<Client>();
			client->id = g_next_client++;
			client->socket = s;

			commands::Log("[tcp:" + std::to_string(client->id) + "] connected");

			g_clients[client->id] = std::move(client);
		}

		// Requests. The map may change during a command (none removes clients
		// here, but async answers look clients up) : iterate on ids.
		std::vector<int> ids;
		ids.reserve(g_clients.size());

		for (const auto& [id, client] : g_clients)
			ids.push_back(id);

		for (int id : ids)
		{
			auto it = g_clients.find(id);

			if (it == g_clients.end())
				continue;

			Client& client = *it->second;

			if (client.dead)
				continue;

			if (!client.closing && !Receive(client))
			{
				Disconnect(client);
				continue;
			}

			size_t consumed = 0;

			while (!client.closing)
			{
				const size_t end = client.input.find('\n', consumed);

				if (end == std::string::npos)
					break;

				const std::string line = client.input.substr(consumed, end - consumed);
				consumed = end + 1;

				HandleLine(client, line);

				if (over_budget())
					break;
			}

			if (consumed > 0)
				client.input.erase(0, consumed);

			if (!Flush(client))
			{
				Disconnect(client);
				continue;
			}

			if (client.closing && client.output.empty())
				Disconnect(client);

			if (over_budget())
				break;
		}

		// Flush answers given by commands of other clients / async ones.
		for (auto& [id, client] : g_clients)
		{
			if (!client->dead && !client->output.empty() && !Flush(*client))
				Disconnect(*client);
		}

		for (auto it = g_clients.begin(); it != g_clients.end();)
		{
			if (it->second->dead)
				it = g_clients.erase(it);
			else
				++it;
		}
	}


	void YieldFrame()
	{
		g_yield = true;
	}


	void Stop()
	{
		for (auto& [id, client] : g_clients)
		{
			Flush(*client);
			Disconnect(*client);
		}

		g_clients.clear();

		if (g_listen != kInvalidSocket)
		{
			CloseSocket(g_listen);
			g_listen = kInvalidSocket;
		}

		RemoveConnectionFiles();
		g_connection_file.clear();
		g_port = 0;

#ifdef _WIN32
		if (g_wsa_started)
		{
			WSACleanup();
			g_wsa_started = false;
		}
#endif
	}


	bool IsRunning()
	{
		return g_listen != kInvalidSocket;
	}


	int Port()
	{
		return g_port;
	}


	const std::string& Token()
	{
		return g_token;
	}


	int ClientCount()
	{
		int count = 0;

		for (const auto& [id, client] : g_clients)
		{
			if (!client->dead)
				++count;
		}

		return count;
	}
}
